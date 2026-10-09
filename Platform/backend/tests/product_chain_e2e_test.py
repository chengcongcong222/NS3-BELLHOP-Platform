from __future__ import annotations

import asyncio
import json
import os
from pathlib import Path

import httpx
import pytest

from helpers import resource_catalog
from ns3_factory_backend.api import BackendSettings, create_app
from ns3_factory_backend.authoring import _reference_data


pytestmark = pytest.mark.skipif(
    "PLATFORM_SIM_WORKER_PATH" not in os.environ,
    reason="authored Runtime E2E requires the ns-3 worker",
)


async def _terminal(client: httpx.AsyncClient, run_id: str) -> dict:
    for _ in range(200):
        response = await client.get(f"/runs/{run_id}")
        body = response.json()
        if body["lifecycle"] in {"Completed", "Failed"}:
            return body
        await asyncio.sleep(0.02)
    raise AssertionError("authored run did not terminate")


def test_authored_channel1_channel2_and_missing_reference_fail_fast(tmp_path: Path) -> None:
    async def run() -> None:
        repository = tmp_path / "environment-repository"
        workspace = tmp_path / "workspace"
        app = create_app(
            BackendSettings(
                worker_executable=Path(os.environ["PLATFORM_SIM_WORKER_PATH"]),
                environment_repository_root=repository,
                authoring_workspace_root=workspace,
                authoring_reference_asset_root=Path(os.environ["PLATFORM_REFERENCE_ENVIRONMENT_ASSET_ROOT"]),
                environment_asset_publisher_executable=Path(os.environ["PLATFORM_ENVIRONMENT_ASSET_PUBLISHER_PATH"]),
            ),
            resource_catalog=resource_catalog(),
        )
        async with httpx.AsyncClient(
            transport=httpx.ASGITransport(app=app), base_url="http://test"
        ) as client:
            # This gate explicitly imports a reference asset. The separate real
            # Bellhop gate verifies changed authored inputs, not just new IDs.
            reference_ssp, reference_terrain = _reference_data(Path(os.environ["PLATFORM_REFERENCE_ENVIRONMENT_ASSET_ROOT"]))
            environment = await client.post("/authoring/drafts", json={
                "kind": "environment", "name": "e2e-water",
                "payload": {"center_frequency_hz": 25000,
                    "sound_speed_profile": reference_ssp,
                    "bathymetry": reference_terrain,
                    "build_source": "reference-bellhop-arrivals"},
            })
            build = await client.post("/authoring/environment-builds", json={"draft_id": environment.json()["draft_id"]})
            published = await client.post(f"/authoring/environment-builds/{build.json()['build_id']}/publish")
            assert published.status_code == 200
            environment_id = published.json()["environment_asset_id"]

            rows = ["track_id,time_s,x_m,y_m,z_m"]
            node_rows = [(10, 1000, 0, -60), (20, -950, 250, -65),
                         (30, 0, -1050, -55), (99, 0, 0, -8)]
            for node, x, y, z in node_rows:
                rows += [f"N{node},0,{x},{y},{z}", f"N{node},200,{x + 20},{y},{z}"]
            trajectory = await client.post("/authoring/trajectories/import-csv", json={
                "asset_id": "e2e-tracks", "version": 1, "name": "E2E tracks",
                "content": "\n".join(rows) + "\n"})
            assert trajectory.status_code == 201
            nodes = [{"node_id": str(node), "can_transmit": node != 99,
                      "can_receive": True, "duplex_mode": "HalfDuplex",
                      "initial_position": {"x_meters": x, "y_meters": y, "z_meters": z},
                      "initial_velocity": {"x_meters_per_second": 0, "y_meters_per_second": 0, "z_meters_per_second": 0}}
                     for node, x, y, z in node_rows]
            bindings = [{"node_id": str(node), "asset_id": "e2e-tracks",
                         "version": 1, "track_id": f"N{node}"}
                        for node, *_ in node_rows]
            scenario = await client.post("/authoring/drafts", json={
                "kind": "scenario", "name": "E2E scenario",
                "payload": {"environment_asset_id": environment_id,
                    "environment_version": 1, "fusion_center_node_id": "99",
                    "nodes": nodes, "trajectory_bindings": bindings},
            })
            assert scenario.json()["validation_issues"] == []

            async def execute(physical_layer: str, workload: str,
                              routing: str) -> tuple[dict, dict]:
                # COMM is 4 bytes, not the 15-byte feature workload. These
                # slots fit its real airtime (+ CRC for mode B) and 2 s guard,
                # but deliberately do not fit feature-packet airtime.
                slot_ns = (3_000_000_000 if physical_layer == "Channel1Abstract"
                           else 3_200_000_000) if workload == "CommunicationOnly" else 6_000_000_000
                experiment = await client.post("/authoring/drafts", json={
                    "kind": "experiment", "name": f"E2E {physical_layer} {workload}",
                    "payload": {"scenario_id": scenario.json()["draft_id"],
                        "routing_mode": routing, "mac_mode": "Tdma",
                        "workload": workload, "minimum_bearing_points": "5",
                        "maximum_fusion_period_ns": "180000000000",
                        "bit_rate_bits_per_second": "60", "simulation_cycle_count": "1",
                        "slot_duration_ns": str(slot_ns), "guard_interval_ns": "2000000000",
                        "deterministic_seed": "23", "physical_layer": physical_layer,
                        "waveform_modulation": "Bpsk", "waveform_noise": "None",
                        "waveform_noise_psd_db_re_1upa2_per_hz": 0.0},
                })
                started = await client.post(f"/authoring/experiments/{experiment.json()['draft_id']}/runs")
                assert started.status_code == 201
                terminal = await _terminal(client, started.json()["run_id"])
                assert terminal["lifecycle"] == "Completed", terminal
                assert terminal["environment_asset_id"] == environment_id
                assert terminal["provenance"]["trajectory_bindings"] == [
                    {**binding, "node_id": int(binding["node_id"])} for binding in bindings]
                result = (await client.get(f"/runs/{terminal['run_id']}/results")).json()
                assert int(result["projection"]["simulation_duration_ns"]) == 3 * slot_ns
                context_response = await client.get(f"/runs/{terminal['run_id']}/definition-context")
                assert context_response.status_code == 200, context_response.text
                context = context_response.json()
                assert context["scenario"]["scenario_id"] == terminal["scenario_id"]
                assert context["scenario"]["environment"]["environment_asset_id"] == environment_id
                assert context["scenario"]["nodes"][0]["initial_position"]["x_meters"] == 1000
                assert context["experiment"]["routing"]["mode"] == routing
                assert context["experiment"]["fusion"]["workload"] == workload
                assert context["execution"]["physical_layer"] == physical_layer
                assert context["definition_sha256"] == result["provenance"]["definition_sha256"]
                # Same measured payload reaches observation after actual reception,
                # never read backwards from the final result by the UI.
                if physical_layer == "Channel2Waveform":
                    stream = (await client.get(f"/runs/{terminal['run_id']}/events")).text
                    records = [json.loads(line[6:]) for line in stream.splitlines() if line.startswith("data: ")]
                    receptions = [r["trace"]["payload"] for r in records if r.get("trace", {}).get("kind") == "Reception"]
                    for measured in result["visualization"]["waveform"]:
                        observed = next(r["waveform"] for r in receptions if r["transmission_id"] == measured["transmission_id"] and r["receiver_node_id"] == measured["receiver_node_id"])
                        assert {k: observed[k] for k in ("payload_bit_count", "bit_error_count", "crc_ok")} == {k: measured[k] for k in ("payload_bit_count", "bit_error_count", "crc_ok")}
                        assert observed["noise_power_upa2"] == 0
                return terminal, result

            _, communication_channel1 = await execute(
                "Channel1Abstract", "CommunicationOnly", "DirectToFusionCenter")
            assert communication_channel1["visualization"]["positions"]
            assert communication_channel1["acceptance_report"] is None
            assert communication_channel1["fusion_results"] == []
            _, communication_channel2 = await execute(
                "Channel2Waveform", "CommunicationOnly", "ShortestPath")
            assert communication_channel2["visualization"]["waveform"]
            assert communication_channel2["fusion_results"] == []
            _, fusion_channel1 = await execute(
                "Channel1Abstract", "BearingFusion", "ShortestPath")
            assert int(fusion_channel1["visualization"]["generated_feature_count"]) > 0
            _, fusion_channel2 = await execute(
                "Channel2Waveform", "BearingFusion", "DirectToFusionCenter")
            assert fusion_channel2["visualization"]["waveform"]

            definitions = [json.loads(path.read_text()) for path in
                           (workspace / "run-definitions").glob("*.json")]
            assert {item["experiment"]["workload"] for item in definitions} == {
                "CommunicationOnly", "BearingFusion"}
            assert {item["experiment"]["routing_mode"] for item in definitions} == {
                "DirectToFusionCenter", "ShortestPath"}
            assert {item["experiment"]["mac_mode"] for item in definitions} == {"Tdma"}

            broken = await client.post("/authoring/drafts", json={
                "kind": "scenario", "name": "Broken",
                "payload": {"environment_asset_id": environment_id,
                    "fusion_center_node_id": "99", "nodes": nodes,
                    "trajectory_bindings": [{**item, "version": 999} for item in bindings]},
            })
            experiment = await client.post("/authoring/drafts", json={
                "kind": "experiment", "name": "Broken run",
                "payload": {"scenario_id": broken.json()["draft_id"],
                    "bit_rate_bits_per_second": "60", "simulation_cycle_count": "1",
                    "deterministic_seed": "1"},
            })
            failed = await client.post(f"/authoring/experiments/{experiment.json()['draft_id']}/runs")
            assert failed.status_code == 404
            assert failed.json()["error"]["code"] == "TrajectoryNotFound"

    asyncio.run(run())


def test_planning_v2_no_sink_flows_and_independent_bearing_receiver(tmp_path: Path) -> None:
    """Exercise the real worker for both PHYs, including a two-node network."""
    async def run() -> None:
        workspace = tmp_path / "workspace"
        app = create_app(BackendSettings(
            worker_executable=Path(os.environ["PLATFORM_SIM_WORKER_PATH"]),
            environment_repository_root=tmp_path / "repository",
            authoring_workspace_root=workspace,
            authoring_reference_asset_root=Path(os.environ["PLATFORM_REFERENCE_ENVIRONMENT_ASSET_ROOT"]),
            environment_asset_publisher_executable=Path(os.environ["PLATFORM_ENVIRONMENT_ASSET_PUBLISHER_PATH"]),
        ), resource_catalog=resource_catalog())
        async with httpx.AsyncClient(transport=httpx.ASGITransport(app=app), base_url="http://test") as client:
            ssp, terrain = _reference_data(Path(os.environ["PLATFORM_REFERENCE_ENVIRONMENT_ASSET_ROOT"]))
            environment = await client.post("/authoring/drafts", json={
                "kind": "environment", "name": "Planning water", "payload": {
                    "center_frequency_hz": 25000, "sound_speed_profile": ssp,
                    "bathymetry": terrain, "build_source": "reference-bellhop-arrivals"}})
            build = await client.post("/authoring/environment-builds", json={"draft_id": environment.json()["draft_id"]})
            published = await client.post(f"/authoring/environment-builds/{build.json()['build_id']}/publish")
            assert published.status_code == 200, published.text
            environment_id = published.json()["environment_asset_id"]
            node_rows = [(0, 1000, 0, -60), (1, -950, 250, -65), (2, 0, -1050, -55), (99, 0, 0, -8)]
            rows = ["track_id,time_s,x_m,y_m,z_m"]
            for node, x, y, z in node_rows:
                rows.extend([f"N{node},0,{x},{y},{z}", f"N{node},200,{x + 20},{y},{z}"])
            imported = await client.post("/authoring/trajectories/import-csv", json={
                "asset_id": "planning-e2e-tracks", "version": 1, "name": "Planning E2E tracks",
                "content": "\n".join(rows) + "\n"})
            assert imported.status_code == 201, imported.text

            async def execute(physical_layer: str, node_count: int, workload: str,
                              mac_mode: str = "AdaptiveTdma"):
                chosen = node_rows[:node_count]
                scenario = await client.post("/authoring/drafts", json={
                    "kind": "scenario", "name": f"Planning {node_count} nodes", "payload": {
                        "environment_asset_id": environment_id, "environment_version": 1,
                        # A legacy display center is deliberately unrelated to
                        # BearingFusion's receiver; communication has no center.
                        "fusion_center_node_id": "0" if workload == "BearingFusion" else None,
                        "nodes": [{"node_id": str(node), "can_transmit": node != 99,
                            "can_receive": True, "duplex_mode": "HalfDuplex",
                            "initial_position": {"x_meters": x, "y_meters": y, "z_meters": z}}
                            for node, x, y, z in chosen],
                        "trajectory_bindings": [{"node_id": str(node), "asset_id": "planning-e2e-tracks",
                            "version": 1, "track_id": f"N{node}"} for node, *_ in chosen],
                    }})
                assert scenario.json()["validation_issues"] == []
                flows = [{"source_node_id": "0", "destination_node_id": "1", "packets_per_cycle": 1},
                         {"source_node_id": "1", "destination_node_id": "0", "packets_per_cycle": 1}]
                if node_count == 3:
                    flows[0]["packets_per_cycle"] = 2
                    flows.append({"source_node_id": "0", "destination_node_id": "2", "packets_per_cycle": 1})
                payload = {
                    "scenario_id": scenario.json()["draft_id"], "execution_mode": "PlanningV1",
                    "routing_mode": "Direct", "mac_mode": mac_mode,
                    "planning_cycle_duration_ns": "60000000000",
                    "workload": workload, "flows": flows if workload == "CommunicationOnly" else [],
                    "fusion_receiver_node_id": "99" if workload == "BearingFusion" else None,
                    "minimum_bearing_points": "5", "maximum_fusion_period_ns": "180000000000",
                    "bit_rate_bits_per_second": "60", "simulation_cycle_count": "2",
                    "deterministic_seed": "23", "physical_layer": physical_layer,
                    "waveform_modulation": "Bpsk", "waveform_noise": "None",
                }
                if mac_mode == "AdaptiveTdma":
                    payload["adaptive_tdma"] = {"safety_margin_ns": "0"}
                else:
                    payload.update(slot_duration_ns="16000000000", guard_interval_ns="2000000000")
                experiment = await client.post("/authoring/drafts", json={
                    "kind": "experiment", "name": f"{physical_layer} {workload} {mac_mode}", "payload": payload})
                assert experiment.json()["validation_issues"] == [], experiment.text
                started = await client.post(f"/authoring/experiments/{experiment.json()['draft_id']}/runs")
                assert started.status_code == 201, started.text
                terminal = await _terminal(client, started.json()["run_id"])
                assert terminal["lifecycle"] == "Completed", terminal
                run_id = terminal["run_id"]
                result = (await client.get(f"/runs/{run_id}/results")).json()
                assert result["acceptance_report"] is None
                assert result["projection"]["simulation_duration_ns"] == "120000000000"
                assert len(result["nodes"]) == node_count
                context_response = await client.get(f"/runs/{run_id}/definition-context")
                assert context_response.status_code == 200, context_response.text
                context = context_response.json()
                assert context["experiment"]["execution_mode"] == "PlanningV1"
                assert context["experiment"]["planning_cycle_duration_ns"] == "60000000000"
                assert context["experiment"]["mac"]["mode"] == mac_mode
                stream = (await client.get(f"/runs/{run_id}/events")).text
                records = [json.loads(line[6:]) for line in stream.splitlines() if line.startswith("data: ")]
                traces = [record["trace"] for record in records if "trace" in record]
                transmissions = [trace["payload"] for trace in traces if trace["kind"] == "Transmission"]
                if workload == "CommunicationOnly":
                    assert context["scenario"]["fusion_center_node_id"] is None
                    assert result["fusion_results"] == []
                    assert all(not node["is_fusion_center"] for node in result["nodes"])
                    expected = {(flow["source_node_id"], flow["destination_node_id"]) for flow in flows}
                    actual = {(tx["sender_node_id"], tx["target"]["node_id"]) for tx in transmissions}
                    assert actual == expected
                    assert len(transmissions) == 2 * sum(flow["packets_per_cycle"] for flow in flows)
                    receptions = [trace["payload"] for trace in traces if trace["kind"] == "Reception"]
                    assert int(result["projection"]["local_delivery_count"]) == sum(
                        rx["disposition"] == "LocalDelivery" for rx in receptions)
                    for tx in transmissions:
                        target = [rx for rx in receptions if rx["transmission_id"] == tx["transmission_id"]
                                  and rx["receiver_node_id"] == tx["target"]["node_id"]]
                        assert len(target) == 1
                        if physical_layer == "Channel1Abstract":
                            assert target[0]["disposition"] == "LocalDelivery"
                        else:
                            # The actual multipath waveform can fail CRC even
                            # without added noise; scheduling does not force it.
                            assert target[0]["disposition"] in {"LocalDelivery", "NotDecoded"}
                            assert target[0]["waveform"]["crc_ok"] == (target[0]["disposition"] == "LocalDelivery")
                else:
                    assert context["scenario"]["fusion_center_node_id"] == "0"
                    assert context["experiment"]["fusion_receiver_node_id"] == "99"
                    fusion = [trace["payload"] for trace in traces if trace["kind"] == "Fusion"]
                    assert len(fusion) == len(result["fusion_results"])
                    assert all(event["fusion_center_node_id"] == "99" for event in fusion)
                    if physical_layer == "Channel1Abstract":
                        assert fusion
                    elif not fusion:
                        delivered = [trace for trace in traces if trace["kind"] == "Reception"
                                     and trace["payload"]["receiver_node_id"] == "99"
                                     and trace["payload"]["disposition"] == "LocalDelivery"]
                        assert len(delivered) < 5
                    assert {node["node_id"] for node in result["nodes"] if node["is_fusion_center"]} == {"99"}
                if mac_mode == "AdaptiveTdma":
                    assert context["experiment"]["mac"]["slot_duration_ns"] is None
                    assert context["actions"]["timing"] is None
                if physical_layer == "Channel2Waveform":
                    assert result["visualization"]["waveform"]
                measured = result["visualization"]["waveform"]
                print(f"PlanningV1 {physical_layer} {node_count}nodes {workload} {mac_mode}: "
                      f"Tx={len(transmissions)}, LocalDelivery={result['projection']['local_delivery_count']}, "
                      f"CRC={sum(item['crc_ok'] for item in measured)}/{len(measured)}, Fusion={len(result['fusion_results'])}")

            for physical_layer in ("Channel1Abstract", "Channel2Waveform"):
                await execute(physical_layer, 2, "CommunicationOnly")
                await execute(physical_layer, 3, "CommunicationOnly")
                await execute(physical_layer, 4, "BearingFusion")
            await execute("Channel1Abstract", 2, "CommunicationOnly", "Tdma")
    asyncio.run(run())
