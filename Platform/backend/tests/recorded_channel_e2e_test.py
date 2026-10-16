from __future__ import annotations
import asyncio
import json
import os
from pathlib import Path
import shutil
import httpx
import pytest
from helpers import resource_catalog
from ns3_factory_backend.api import BackendSettings, create_app
from product_chain_e2e_test import _terminal

pytestmark=pytest.mark.skipif(not os.environ.get("PLATFORM_RECORDED_TEST_ASSET") or
    not os.environ.get("PLATFORM_SIM_WORKER_PATH"),reason="requires real worker and imported recording")


@pytest.mark.parametrize("with_noise", [False, True])
def test_actual_recording_through_authored_worker_and_repeat(tmp_path, with_noise):
    if with_noise and not os.environ.get("PLATFORM_RECORDED_TEST_NOISE"):
        pytest.skip("requires official continuous noise fixture")
    async def run():
        source=Path(os.environ["PLATFORM_RECORDED_TEST_ASSET"])
        manifest=json.loads((source/"manifest.json").read_text())
        aid=manifest["asset_id"]
        workspace=tmp_path/"workspace"
        shutil.copytree(source,workspace/"recorded-channels"/aid/"1")
        if with_noise:
            ns=Path(os.environ["PLATFORM_RECORDED_TEST_NOISE"])
            nm=json.loads((ns/"manifest.json").read_text())
            shutil.copytree(ns,workspace/"recorded-noise"/nm["asset_id"]/"1")
        repository=tmp_path/"empty-bellhop-repository";repository.mkdir()
        app=create_app(BackendSettings(worker_executable=Path(os.environ["PLATFORM_SIM_WORKER_PATH"]),
            environment_repository_root=repository,authoring_workspace_root=workspace),resource_catalog=resource_catalog())
        async with httpx.AsyncClient(transport=httpx.ASGITransport(app=app),base_url="http://test") as client:
            catalog=(await client.get("/recorded-channels")).json()
            assert catalog[0]["manifest"]==manifest
            content="track_id,time_s,x_m,y_m,z_m\nTX,0,0,0,-10\nTX,20,0,0,-10\nRX,0,1000,0,-10\nRX,20,1000,0,-10\n"
            tr=await client.post("/authoring/trajectories/import-csv",json={"asset_id":"recorded-layout","version":1,"name":"示意端点","content":content})
            assert tr.status_code==201,tr.text
            nodes=[{"node_id":str(i),"can_transmit":i==0,"can_receive":i==1,"duplex_mode":"HalfDuplex",
                "initial_position":{"x_meters":i*1000,"y_meters":0,"z_meters":-10},
                "initial_velocity":{"x_meters_per_second":0,"y_meters_per_second":0,"z_meters_per_second":0}} for i in (0,1)]
            scenario=await client.post("/authoring/drafts",json={"kind":"scenario","name":"Blue 单向录音示意",
                "payload":{"environment_asset_id":aid,"environment_version":1,"environment_source":"Recorded",
                    "fusion_center_node_id":None,"nodes":nodes,"trajectory_bindings":[{"node_id":str(i),
                        "asset_id":"recorded-layout","version":1,"track_id":"TX" if i==0 else "RX"} for i in (0,1)]}})
            assert scenario.json()["validation_issues"]==[],scenario.text
            payload={"scenario_id":scenario.json()["draft_id"],"scenario_revision":scenario.json()["revision"],
                "execution_mode":"PlanningV1","routing_mode":"Direct","mac_mode":"AdaptiveTdma",
                "adaptive_tdma":{"safety_margin_ns":"100000000"},"planning_cycle_duration_ns":"5000000000",
                "workload":"CommunicationOnly","flows":[{"source_node_id":"0","destination_node_id":"1","packets_per_cycle":1}],
                "fusion_receiver_node_id":None,"bit_rate_bits_per_second":"6000","simulation_cycle_count":"2",
                "deterministic_seed":"23","physical_layer":"Channel3Recorded","waveform_modulation":"Osdm",
                "waveform_noise":"None","waveform_noise_psd_db_re_1upa2_per_hz":0.,"source_level_db":0.,
                "center_frequency_hz":manifest["fc"],"occupied_bandwidth_hz":4000.,
                "osdm":{"block_length":256,"vector_length":16,"cyclic_prefix":128,"constellation":"Qpsk",
                    "minimum_sync_correlation":.05,"maximum_sync_search":256},
                "recorded_channel":{"asset_id":aid,"version":1,"start_time_s":0.,"modeled_delay_s":2.,
                    "relative_amplitude":1.,"noise":{"mode":"None"}}}
            if with_noise: payload["recorded_channel"]["noise"]={"mode":"ContinuousLibrary","asset_id":nm["asset_id"],"version":1}
            # Missing version and out-of-recording time reject before creating a Run.
            for invalid in ({"version": 99}, {"start_time_s": manifest["duration_s"]}):
                bad=json.loads(json.dumps(payload));bad["recorded_channel"].update(invalid)
                saved_bad=await client.post("/authoring/drafts",json={"kind":"experiment","name":"拒绝非法录音参数","payload":bad})
                failed=await client.post(f"/authoring/experiments/{saved_bad.json()['draft_id']}/runs")
                assert 400 <= failed.status_code < 500,failed.text
            experiment=await client.post("/authoring/drafts",json={"kind":"experiment","name":"真实 Blue 录音 OSDM","payload":payload})
            assert experiment.json()["validation_issues"]==[],experiment.text
            started=await client.post(f"/authoring/experiments/{experiment.json()['draft_id']}/runs")
            assert started.status_code==201,started.text
            terminal=await _terminal(client,started.json()["run_id"])
            assert terminal["lifecycle"]=="Completed",terminal
            rid=terminal["run_id"]
            context=await client.get(f"/runs/{rid}/definition-context")
            assert context.status_code==200,context.text
            assert context.json()["recorded_channel"]["manifest"]==manifest
            assert terminal["provenance"]["recorded_channel"]["manifest"]["source_sha256"]==manifest["source_sha256"]
            assert context.json()["actions"]["rerun"]["available"] is True,context.text
            stream=(await client.get(f"/runs/{rid}/events")).text
            events=[json.loads(line[6:])["trace"] for line in stream.splitlines() if line.startswith("data: ") and "trace" in json.loads(line[6:])]
            channels=[e["payload"]["outcome"] for e in events if e["kind"]=="ChannelOutcome"]
            assert len(channels)==2 and all(c["type"]=="Recorded" for c in channels)
            assert all("aggregate_transmission_loss_db" not in c for c in channels)
            receptions=[e["payload"] for e in events if e["kind"]=="Reception"]
            assert len(receptions)==2
            for reception in receptions:
                w=reception["waveform"]
                if w:
                    assert "signal_power_relative" in w and "signal_power_upa2" not in w
                    assert (w["noise_power_relative"] > 0) == with_noise
                    assert w["crc_ok"]==(reception["disposition"]=="LocalDelivery")
                else: assert reception["disposition"]=="NotDecoded"
            repeated=await client.post(f"/runs/{rid}/rerun")
            assert repeated.status_code==201,repeated.text
            assert (await _terminal(client,repeated.json()["run_id"]))["lifecycle"]=="Completed"
            second=(await client.get(f"/runs/{repeated.json()['run_id']}/events")).text
            again=[json.loads(line[6:])["trace"] for line in second.splitlines() if line.startswith("data: ") and "trace" in json.loads(line[6:])]
            assert events==again
            if with_noise:
                nb=workspace/"recorded-noise"/nm["asset_id"]/"1"/"noise.f64le"
                with nb.open("r+b") as f:f.write(b"corrupt!")
                failed=await client.post(f"/runs/{rid}/rerun")
                assert 400 <= failed.status_code < 500,failed.text
            # A corrupt selected binary must fail before Run creation; no Bellhop fallback.
            binary=workspace/"recorded-channels"/aid/"1"/"phase.f64le"
            with binary.open("r+b") as f:f.write(b"corrupt!")
            failed=await client.post(f"/runs/{rid}/rerun")
            assert failed.status_code>=400,failed.text
            print(f"Channel C real recording noise={with_noise}: Tx=2, Recorded=2, Rx=2, CRC={sum(bool(r['waveform'] and r['waveform']['crc_ok']) for r in receptions)}/2, exact repeat, invalid coverage/version and corrupt asset rejected")
    asyncio.run(run())
