"""Real Worker + official imported Blue; no HTTP/receiver mocks."""
import asyncio
import json
import os
from pathlib import Path
import shutil
import resource
import time

import httpx
import pytest

from helpers import resource_catalog
from ns3_factory_backend.api import BackendSettings, create_app

pytestmark = pytest.mark.skipif(
    not os.environ.get("PLATFORM_RECORDED_TEST_ASSET") or not os.environ.get("PLATFORM_SIM_WORKER_PATH"),
    reason="requires imported recording and actual ns-3 Worker")

def _backend_rss_kib():
    # ASGI backend resides in this test process; this is not Worker peak RSS.
    return int(next(line.split()[1] for line in Path("/proc/self/status").read_text().splitlines()
                    if line.startswith("VmRSS:")))

async def _terminal(client, run_id):
    # Real multi-link replay is CPU work; the single-link helper's 4-second
    # wall-clock budget is not a simulation-time or completion guarantee.
    for _ in range(600):
        response = await client.get(f"/runs/{run_id}")
        response.raise_for_status()
        body = response.json()
        if body["lifecycle"] in {"Completed", "Failed"}:
            return body
        await asyncio.sleep(.1)
    raise AssertionError("real recorded Worker exceeded 60-second test deadline")


@pytest.mark.parametrize("overlap,adaptive,with_noise,node_count,mixed", [
    (False,False,False,4,False),(True,False,False,4,False),(False,True,False,4,False),
    (True,False,True,4,False),(False,False,False,6,False),(False,True,False,4,True),
    (True,False,False,4,True)])
def test_proxy_official_recording_worker_and_exact_repeat(tmp_path, overlap, adaptive, with_noise, node_count, mixed):
    if with_noise and not os.environ.get("PLATFORM_RECORDED_TEST_NOISE"):
        pytest.skip("requires pinned continuous noise")
    async def run():
        source = Path(os.environ["PLATFORM_RECORDED_TEST_ASSET"])
        m = json.loads((source/"manifest.json").read_text())
        aid = m["asset_id"]
        workspace = tmp_path/"workspace"
        shutil.copytree(source, workspace/"recorded-channels"/aid/"1")
        if with_noise:
            ns = Path(os.environ["PLATFORM_RECORDED_TEST_NOISE"])
            nm = json.loads((ns/"manifest.json").read_text())
            shutil.copytree(ns, workspace/"recorded-noise"/nm["asset_id"]/"1")
        repository = tmp_path/"empty-bellhop"; repository.mkdir()
        settings = BackendSettings(
            worker_executable=Path(os.environ["PLATFORM_SIM_WORKER_PATH"]),
            environment_repository_root=repository, authoring_workspace_root=workspace)
        app = create_app(settings, resource_catalog=resource_catalog())
        async with httpx.AsyncClient(transport=httpx.ASGITransport(app=app), base_url="http://test") as c:
            csv = "track_id,time_s,x_m,y_m,z_m\n" + "".join(
                f"N{i},{t},0,0,-1\n" for i in range(node_count) for t in (0, 40))
            tr = await c.post("/authoring/trajectories/import-csv", json={
                "asset_id": "logical", "version": 1, "name": "逻辑位置", "content": csv})
            assert tr.status_code == 201, tr.text
            tx_count = 3 if node_count == 4 else 4
            nodes = [{"node_id": str(i), "can_transmit": i < tx_count, "can_receive": i >= tx_count,
                      "duplex_mode": "HalfDuplex",
                      "initial_position": dict(x_meters=0, y_meters=0, z_meters=-1),
                      "initial_velocity": dict(x_meters_per_second=0, y_meters_per_second=0, z_meters_per_second=0)}
                     for i in range(node_count)]
            scenario = await c.post("/authoring/drafts", json={"kind": "scenario", "name": "三发一收代理",
                "payload": {"environment_asset_id": aid, "environment_version": 1, "environment_source": "Recorded",
                            "fusion_center_node_id": None, "nodes": nodes,
                            "trajectory_bindings": [{"node_id": str(i), "asset_id": "logical", "version": 1,
                                                     "track_id": f"N{i}"} for i in range(node_count)]}})
            assert scenario.status_code == 201, scenario.text
            sample = dict(asset_id=aid, version=1, start_sample="0",
                          modeled_reference_delay_ns="2048000000", relative_amplitude=1.)
            bindings = {"schema": "ns3-factory-recorded-proxy-bindings-v1",
                        "nodes": [{k: n[k] for k in ("node_id", "can_transmit", "can_receive")} for n in nodes],
                        "allocation": "GlobalSame", "samples": [sample],
                        "overrides": ([{"tx_id": "0", "rx_id": "3",
                                       "sample": {**sample, "modeled_reference_delay_ns": "6144000000"}}]
                                      if overlap else []),
                        "receiver_noise": [{"rx_id": "3", "mode": "None"}],
                        "carrier_hz": int(m["fc"]), "baseband_rate_hz": 3000,
                        "occupied_bandwidth_hz": 4000., "duration_ns": "32768000000"}
            payload = {"scenario_id": scenario.json()["draft_id"], "scenario_revision": scenario.json()["revision"],
                       "execution_mode": "PlanningV1", "routing_mode": "Direct", "mac_mode": "Tdma",
                       "slot_duration_ns": "2048000000", "guard_interval_ns": "0",
                       "planning_cycle_duration_ns": "16384000000", "workload": "CommunicationOnly",
                       "flows": [{"source_node_id": str(i), "destination_node_id": "3", "packets_per_cycle": 1}
                                 for i in range(3)], "fusion_receiver_node_id": None,
                       "bit_rate_bits_per_second": "6000", "simulation_cycle_count": "2", "deterministic_seed": "23",
                       "physical_layer": "Channel3Recorded", "waveform_modulation": "Osdm",
                       "waveform_noise": "None", "waveform_noise_psd_db_re_1upa2_per_hz": 0., "source_level_db": 0.,
                       "center_frequency_hz": m["fc"], "occupied_bandwidth_hz": 4000.,
                       "osdm": {"block_length": 256, "vector_length": 16, "cyclic_prefix": 128,
                                "constellation": "Qpsk", "minimum_sync_correlation": .05, "maximum_sync_search": 256},
                       "recorded_proxy": bindings}
            if adaptive:
                payload.pop("slot_duration_ns")
                payload.pop("guard_interval_ns")
                payload.update(mac_mode="AdaptiveTdma",adaptive_tdma={"safety_margin_ns":"100000000"})
            if mixed:
                bindings["transmitter_modulations"] = [
                    {"node_id":"0","modulation":"Bpsk"},{"node_id":"1","modulation":"Bfsk"}]
                payload.update(planning_cycle_duration_ns="32768000000",simulation_cycle_count="1")
                if overlap:
                    payload["slot_duration_ns"] = "3072000000"
                    bindings["overrides"][0]["sample"]["modeled_reference_delay_ns"] = "5120000000"
            if node_count == 6:
                payload.update(planning_cycle_duration_ns="32768000000", simulation_cycle_count="1",
                    # Four business destinations; all eight physical paths are
                    # still exercised, including each packet's other receiver.
                    flows=[{"source_node_id":str(tx),"destination_node_id":str(tx_count+tx%2),"packets_per_cycle":1}
                           for tx in range(tx_count)])
                bindings["receiver_noise"] = [{"rx_id":str(rx),"mode":"None"} for rx in range(tx_count,node_count)]
            if with_noise:
                payload.update(slot_duration_ns="1024000000", planning_cycle_duration_ns="8192000000",
                               simulation_cycle_count="1")
                bindings["duration_ns"] = "8192000000"
                bindings["samples"][0]["modeled_reference_delay_ns"] = "1024000000"
                bindings["overrides"][0]["sample"]["modeled_reference_delay_ns"] = "3072000000"
                bindings["receiver_noise"] = [{"rx_id":"3","mode":"ContinuousLibrary","asset_id":nm["asset_id"],"version":1}]
            draft = await c.post("/authoring/drafts", json={"kind": "experiment", "name": "正式代理混合测试", "payload": payload})
            assert draft.status_code == 201 and not draft.json()["validation_issues"], draft.text
            rss_before = _backend_rss_kib()
            start_time = time.monotonic()
            started = await c.post(f"/authoring/experiments/{draft.json()['draft_id']}/runs")
            admission_seconds = time.monotonic()-start_time
            assert started.status_code == 201, started.text
            rid = started.json()["run_id"]
            terminal = await _terminal(c, rid)
            assert terminal["lifecycle"] == "Completed", json.dumps(terminal, ensure_ascii=False)
            run_seconds = time.monotonic()-start_time
            rss_after_first = _backend_rss_kib()
            context = await c.get(f"/runs/{rid}/definition-context")
            assert context.status_code == 200, context.text
            assert len(context.json()["recorded_proxy"]["resolved_links"]) == tx_count*(node_count-tx_count)
            async def events(run_id):
                text = (await c.get(f"/runs/{run_id}/events")).text
                return [j["trace"] for line in text.splitlines() if line.startswith("data: ")
                        for j in [json.loads(line[6:])] if "trace" in j]
            first = await events(rid)
            count = len(payload["flows"])*int(payload["simulation_cycle_count"])
            assert sum(e["kind"] == "Transmission" for e in first) == count
            if mixed:
                durations = {e["payload"]["sender_node_id"]:
                    int(e["payload"]["ended_at_ns"])-int(e["payload"]["started_at_ns"])
                    for e in first if e["kind"]=="Transmission"}
                assert durations["0"] == durations["1"] > durations["2"]
            assert sum(e["kind"] == "Reception" for e in first) == count*(node_count-tx_count)
            receptions = [e["payload"] for e in first if e["kind"] == "Reception"]
            assert all(int(r["mixture"]["sample_count"]) > 0 for r in receptions)
            assert max(int(r["mixture"]["signal_count"]) for r in receptions) == (2 if overlap else 1)
            assert all((r["mixture"]["noise_power_relative"] > 0) == with_noise for r in receptions)
            public_result = (await c.get(f"/runs/{rid}/results")).json()
            evidence = public_result["provenance"]["proxy_receptions"]
            assert len(evidence) == count*(node_count-tx_count)
            assert {r["sender_node_id"] for r in evidence} == {str(i) for i in range(tx_count)}
            assert all("waveform" in r and "mixture" in r for r in evidence)
            repeated = await c.post(f"/runs/{rid}/rerun")
            assert repeated.status_code == 201, repeated.text
            assert (await _terminal(c, repeated.json()["run_id"]))["lifecycle"] == "Completed"
            assert first == await events(repeated.json()["run_id"])
            # The single-C archive uses the same admission owner as A/B. It must
            # retain the complete mapping and exact result after backend restart.
            async with app.router.lifespan_context(app):
                pass
            archives = (await c.get("/run-groups")).json()
            original = next(g for g in archives if g["members"][0]["run_id"] == rid)
            assert original["state"] == "Completed"
            assert original["members"][0]["result"]["provenance"]["recorded_proxy"] == context.json()["recorded_proxy"]
            assert original["members"][0]["result"]["provenance"]["proxy_receptions"] == evidence
            if os.environ.get("PLATFORM_PROXY_EVIDENCE_DIR"):
                output = Path(os.environ["PLATFORM_PROXY_EVIDENCE_DIR"])
                output.mkdir(parents=True,exist_ok=True)
                label=f"nodes{node_count}-overlap{overlap}-adaptive{adaptive}-noise{with_noise}-mixed{mixed}"
                (output/f"{label}.json").write_text(json.dumps({
                    "executed_by":"real ASGI API and ns-3 Worker; official Blue element 0",
                    "case":label,"node_count":node_count,"physical_link_count":tx_count*(node_count-tx_count),
                    "admission_seconds":admission_seconds,"first_run_wall_seconds":run_seconds,
                    "cumulative_child_peak_rss_kib":resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss,
                    "memory_scope":"maximum of all completed child processes in this pytest invocation, not isolated instantaneous Worker RSS",
                    "backend_test_process_rss_kib":{"before":rss_before,"after_first":rss_after_first,
                                                    "after_repeat":_backend_rss_kib()},
                    "trace_bytes":len(json.dumps(first,separators=(",",":")).encode()),
                    "events":first,"group":original,
                    "repeat_event_identity":True,
                },ensure_ascii=False,indent=2))
            restarted = create_app(settings, resource_catalog=resource_catalog())
            async with httpx.AsyncClient(transport=httpx.ASGITransport(app=restarted),base_url="http://test") as second:
                restored = (await second.get(f"/run-groups/{original['group_id']}")).json()
                assert restored["archived"] and restored["members"][0]["result"] == original["members"][0]["result"]
                again = await second.post(f"/run-groups/{original['group_id']}/rerun",
                                          json={"request_idempotency_key":"restored-proxy"})
                assert again.status_code == 201, again.text
                assert (await _terminal(second,again.json()["members"][0]["run_id"]))["lifecycle"] == "Completed"
                async with restarted.router.lifespan_context(restarted):
                    pass
            binary = workspace/"recorded-channels"/aid/"1/channel.f64le"
            with binary.open("r+b") as f:
                f.write(b"corrupt!")
            refused = await c.post(f"/runs/{rid}/rerun")
            assert refused.status_code >= 400, refused.text
    asyncio.run(run())
