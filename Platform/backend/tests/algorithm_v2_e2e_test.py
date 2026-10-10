"""Real HTTP authoring -> actual Worker/ns-3 -> exact C++ planning replay."""
import asyncio
import json
import os
import subprocess
from pathlib import Path
import httpx
import pytest
from ns3_factory_backend.api import BackendSettings, create_app
from ns3_factory_backend.authoring import _reference_data
from product_chain_e2e_test import _terminal

pytestmark = pytest.mark.skipif(os.environ.get("PLATFORM_ALG02_E2E") != "1",
    reason="explicit ALG02 gate requires an installed v2 package and ns-3 authority adapters")


def test_flow_planning_channel1_channel2_and_exact_replay(tmp_path: Path):
    async def run():
        worker = Path(os.environ["PLATFORM_SIM_WORKER_PATH"])
        root = Path(os.environ["PLATFORM_REFERENCE_ENVIRONMENT_ASSET_ROOT"])
        repository, workspace = tmp_path / "environments", tmp_path / "workspace"
        subprocess.run([os.environ["PLATFORM_WORKER_TEST_ASSET_BUILDER_PATH"], str(repository), "backend-field-v1"], check=True)
        app = create_app(BackendSettings(worker_executable=worker, environment_repository_root=repository,
            authoring_workspace_root=workspace, authoring_reference_asset_root=root,
            environment_asset_publisher_executable=Path(os.environ["PLATFORM_ENVIRONMENT_ASSET_PUBLISHER_PATH"]),
            resource_catalog_adapter=Path(os.environ["PLATFORM_RESOURCE_CATALOG_ADAPTER_PATH"]),
            acceptance_environment_asset_id="backend-field-v1"))
        async with httpx.AsyncClient(transport=httpx.ASGITransport(app=app), base_url="http://test") as client:
            catalog = await client.get("/algorithms")
            assert catalog.status_code == 200, catalog.text
            algorithm_id = os.environ.get("PLATFORM_ALG02_TEST_ALGORITHM", "example.channel-aware")
            algorithm = next(a for a in catalog.json()["algorithms"] if a["id"] == algorithm_id)
            selection = {key: algorithm[key] for key in ("id", "version", "package_sha256", "interface")}
            selection["parameters"] = {p["id"]: p["default"] for p in algorithm["parameters"]}
            ssp, terrain = _reference_data(root)
            environment = await client.post("/authoring/drafts", json={"kind": "environment", "name": "ALG02 imported reference",
                "payload": {"center_frequency_hz": 25000, "sound_speed_profile": ssp,
                    "bathymetry": terrain, "build_source": "reference-bellhop-arrivals"}})
            assert environment.status_code == 201, environment.text
            build = await client.post("/authoring/environment-builds", json={"draft_id": environment.json()["draft_id"]})
            assert build.status_code == 202, build.text
            published = await client.post(f"/authoring/environment-builds/{build.json()['build_id']}/publish")
            assert published.status_code == 200, published.text
            environment_id = published.json()["environment_asset_id"]
            locations = [(0, 1000, 0, -60), (10, -950, 250, -65), (20, 0, -1050, -55), (99, 0, 0, -8)]
            rows = ["track_id,time_s,x_m,y_m,z_m"]
            for node, x, y, z in locations:
                rows += [f"N{node},0,{x},{y},{z}", f"N{node},200,{x+20},{y},{z}"]
            trajectory = await client.post("/authoring/trajectories/import-csv", json={"asset_id": "alg02-tracks", "version": 1,
                "name": "ALG02 moving nodes", "content": "\n".join(rows) + "\n"})
            assert trajectory.status_code == 201, trajectory.text
            scene = await client.post("/authoring/drafts", json={"kind": "scenario", "name": "ALG02 flow scene", "payload": {
                "environment_asset_id": environment_id, "environment_version": 1, "fusion_center_node_id": None,
                "nodes": [{"node_id": str(n), "can_transmit": n != 99, "can_receive": True, "duplex_mode": "HalfDuplex",
                           "initial_position": {"x_meters": x, "y_meters": y, "z_meters": z}} for n, x, y, z in locations],
                "trajectory_bindings": [{"node_id": str(n), "asset_id": "alg02-tracks", "version": 1, "track_id": f"N{n}"}
                                        for n, *_ in locations]}})
            assert scene.status_code == 201 and not scene.json()["validation_issues"], scene.text
            summaries = []
            for physical_layer in ("Channel1Abstract", "Channel2Waveform"):
                payload = {"scenario_id": scene.json()["draft_id"], "execution_mode": "PlanningV1", "routing_mode": "Direct",
                    "mac_mode": "RegisteredMac", "mac_algorithm": selection, "workload": "CommunicationOnly",
                    "planning_cycle_duration_ns": "30000000000", "network_update_interval_cycles": "3",
                    "flows": [{"flow_id": str(index), "source_node_id": str(n), "destination_node_id": "99", "packets_per_cycle": 1}
                              for index, n in enumerate([0, 0, 10, 20])],
                    "bit_rate_bits_per_second": "60", "center_frequency_hz": 25000, "occupied_bandwidth_hz": 4000,
                    "source_level_db_re_1upa_at_1m": 110, "simulation_cycle_count": "2", "deterministic_seed": "23",
                    "physical_layer": physical_layer, "waveform_modulation": "Bpsk", "waveform_noise": "None"}
                draft = await client.post("/authoring/drafts", json={"kind": "experiment", "name": f"ALG02 {physical_layer}", "payload": payload})
                assert draft.status_code == 201 and not draft.json()["validation_issues"], draft.text
                started = await client.post(f"/authoring/experiments/{draft.json()['draft_id']}/runs")
                assert started.status_code == 201, started.text
                terminal = await _terminal(client, started.json()["run_id"])
                assert terminal["lifecycle"] == "Completed", terminal
                run_id = terminal["run_id"]
                directory = workspace / "planning-evidence" / run_id
                for _ in range(200):
                    if (directory / "index.json").exists(): break
                    await asyncio.sleep(0.02)
                index = json.loads((directory / "index.json").read_text())
                assert index["status"] == "Available" and len(index["cycles"]) == 2, index
                cycles = [json.loads((directory / name).read_text()) for name in index["cycles"]]
                assert cycles[0]["context"]["previous"] is None
                assert cycles[1]["context"]["structure_origin"]["cycle_id"] == "0"
                assert cycles[1]["context"]["nodes"][0]["position"][0] > cycles[0]["context"]["nodes"][0]["position"][0]
                for name, cycle in zip(index["cycles"], cycles):
                    assert {link["environment_id"] for link in cycle["context"]["links"]} == {environment_id}
                    planned = {row["grant_id"]: row for row in cycle["plan"]["grants"]}
                    assert len(planned) == 4 and len(cycle["executions"]) == 4
                    assert len({row["packet_id"] for row in cycle["executions"]}) == 4
                    for actual in cycle["executions"]:
                        expected = planned[actual["grant_id"]]
                        assert actual["status"] == "Executed" and actual["transmission_id"] is not None
                        for key in ("flow_id", "owner", "next_hop", "destination", "snapshot_version", "context_fingerprint"):
                            assert actual[key] == expected[key]
                        assert actual["planned_at_ns"] == actual["actual_at_ns"] == expected["start_ns"]
                    replay = subprocess.run([str(worker), "--replay-planning-context", str(directory / name)], capture_output=True, text=True)
                    assert replay.returncode == 0, replay.stderr
                    assert json.loads(replay.stdout)["status"] == "Matched"
                feedback = cycles[1]["context"]["previous"]["links"]
                targets = [row for row in feedback if row["intended_target"]]
                assert sum(int(row["attempts"]) for row in targets) == 4
                if physical_layer == "Channel2Waveform":
                    assert sum(int(row["compared_bits"]) for row in targets) > 0
                    assert sum(int(row["crc_passed"]) + int(row["crc_failed"]) for row in targets) > 0
                else:
                    assert all(row["compared_bits"] == "0" and row["crc_passed"] == "0" for row in targets)
                context = await client.get(f"/runs/{run_id}/definition-context")
                assert context.status_code == 200, context.text
                assert context.json()["experiment"]["mac_algorithm"]["interface"] == "planning-mac-v2"
                summaries.append({"algorithm": algorithm_id, "mode": physical_layer, "run_id": run_id, "status": terminal["lifecycle"],
                    "evidence": str(directory), "grants": sum(len(c["plan"]["grants"]) for c in cycles), "replay": "Matched"})
            (tmp_path / "alg02-e2e-summary.json").write_text(json.dumps(summaries, indent=2))
            print(json.dumps(summaries, indent=2))
    asyncio.run(run())
