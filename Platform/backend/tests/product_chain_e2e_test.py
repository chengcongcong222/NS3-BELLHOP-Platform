from __future__ import annotations

import asyncio
import json
import os
from pathlib import Path

import httpx
import pytest

from helpers import resource_catalog
from ns3_factory_backend.api import BackendSettings, create_app


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
            environment = await client.post("/authoring/drafts", json={
                "kind": "environment", "name": "e2e-water",
                "payload": {"center_frequency_hz": 25000,
                    "sound_speed_profile": [{"depth_meters": 0, "speed_meters_per_second": 1500}, {"depth_meters": 100, "speed_meters_per_second": 1490}],
                    "bathymetry": [{"range_meters": 0, "depth_meters": 100}, {"range_meters": 2500, "depth_meters": 110}],
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
                experiment = await client.post("/authoring/drafts", json={
                    "kind": "experiment", "name": f"E2E {physical_layer} {workload}",
                    "payload": {"scenario_id": scenario.json()["draft_id"],
                        "routing_mode": routing, "mac_mode": "Tdma",
                        "workload": workload, "minimum_bearing_points": "5",
                        "maximum_fusion_period_ns": "180000000000",
                        "bit_rate_bits_per_second": "60", "simulation_cycle_count": "1",
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
