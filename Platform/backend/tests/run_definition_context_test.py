from __future__ import annotations

import asyncio
import json
from pathlib import Path

import httpx

from authoring_api_test import CapturingGateway
from helpers import resource_catalog
from ns3_factory_backend.api import BackendSettings, create_app


async def _authored(client: httpx.AsyncClient) -> tuple[dict, dict, dict]:
    imported = await client.post("/authoring/trajectories/import-csv", json={
        "asset_id": "context-track", "version": 1, "name": "Context test trajectory",
        "content": "track_id,time_s,x_m,y_m,z_m\nN0,0,3,4,-8\nN0,100,3,4,-8\nN1,0,50,0,-10\nN1,100,150,0,-10\n",
    })
    assert imported.status_code == 201
    nodes = [{"node_id": str(node_id), "can_transmit": True, "can_receive": True,
              "duplex_mode": "HalfDuplex", "initial_position": {
                  "x_meters": x, "y_meters": y, "z_meters": z},
              # Deliberately stale compatibility input: projection must use
              # the actual trajectory segment, not this old authoring field.
              "initial_velocity": {"x_meters_per_second": 999,
                                   "y_meters_per_second": 999,
                                   "z_meters_per_second": 999}}
             for node_id, x, y, z in [(0, 3, 4, -8), (1, 50, 0, -10)]]
    scenario = (await client.post("/authoring/drafts", json={
        "kind": "scenario", "name": "Exact scene", "payload": {
            "environment_asset_id": "context-environment", "environment_version": 1,
            "fusion_center_node_id": "0", "nodes": nodes,
            "trajectory_bindings": [{"node_id": str(node_id), "asset_id": "context-track",
                                     "version": 1, "track_id": f"N{node_id}"} for node_id in [0, 1]],
        },
    })).json()
    experiment = (await client.post("/authoring/drafts", json={
        "kind": "experiment", "name": "Exact experiment", "payload": {
            "scenario_id": scenario["draft_id"], "scenario_revision": scenario["revision"],
            "bit_rate_bits_per_second": "60", "simulation_cycle_count": "7",
            "deterministic_seed": "123", "physical_layer": "Channel2Waveform",
            "waveform_modulation": "Bfsk", "waveform_noise": "Awgn",
            "waveform_noise_psd_db_re_1upa2_per_hz": 55.0,
            "routing_mode": "ShortestPath", "mac_mode": "Tdma", "workload": "CommunicationOnly",
        },
    })).json()
    response = await client.post(f"/authoring/experiments/{experiment['draft_id']}/runs")
    assert response.status_code == 201, response.text
    return scenario, experiment, response.json()


def _application(tmp_path: Path):
    (tmp_path / "repo" / "context-environment").mkdir(parents=True)
    gateway = CapturingGateway()
    app = create_app(BackendSettings(
        worker_executable=tmp_path / "worker", environment_repository_root=tmp_path / "repo",
        authoring_workspace_root=tmp_path / "workspace"), gateway=gateway,
        resource_catalog=resource_catalog())
    return app, gateway


def test_authored_run_context_uses_frozen_inputs_not_current_drafts(tmp_path: Path):
    async def run():
        app, gateway = _application(tmp_path)
        async with httpx.AsyncClient(transport=httpx.ASGITransport(app=app), base_url="http://test") as client:
            scenario, experiment, created = await _authored(client)
            await gateway.called.wait()
            endpoint = f"/runs/{created['run_id']}/definition-context"
            response = await client.get(endpoint)
            assert response.status_code == 200, response.text
            context = response.json()
            assert context["source"] == "AuthoredDefinition"
            assert context["scenario"]["scenario_id"] == created["scenario_id"]
            assert context["scenario"]["mobility"]["model"] == "TrajectoryDriven"
            assert context["scenario"]["trajectory_bindings"][1]["version"] == "1"
            assert context["scenario"]["nodes"][1]["initial_position"]["x_meters"] == 50
            assert context["scenario"]["nodes"][1]["initial_velocity"]["x_meters_per_second"] == 1
            assert context["experiment"]["simulation_cycle_count"] == "7"
            assert context["experiment"]["routing"]["mode"] == "ShortestPath"
            assert context["experiment"]["fusion"]["workload"] == "CommunicationOnly"
            assert context["experiment"]["fusion"]["acceptance_profile"] is None
            assert context["experiment"]["phy"]["physical_layer"] == "Channel2Waveform"
            assert context["execution"]["waveform_modulation"] == "Bfsk"
            assert context["definition_sha256"] == created["provenance"]["definition_sha256"]
            assert str(tmp_path) not in response.text
            # The old global resource catalog still has no such preset. This
            # endpoint must resolve the exact Run, not manufacture a preset.
            assert (await client.get(f"/scenarios/{created['scenario_id']}/versions/1")).status_code == 404
            for draft in [scenario, experiment]:
                saved = await client.put(f"/authoring/drafts/{draft['kind']}/{draft['draft_id']}",
                    headers={"If-Match": str(draft["revision"])}, json={
                        "kind": draft["kind"], "name": "Changed after run", "payload": {}})
                assert saved.status_code == 200
            changed = (await client.get(endpoint)).json()
            assert changed["actions"]["edit"]["current_revision"] == 2
            assert changed["actions"]["edit"]["captured_revision"] == 1
            assert {key: value for key, value in changed.items() if key != "actions"} == {
                key: value for key, value in context.items() if key != "actions"}
            assert len((await client.get("/runs")).json()) == 1
    asyncio.run(run())


def test_missing_frozen_definition_is_explicit_without_preset_fallback(tmp_path: Path):
    async def run():
        app, gateway = _application(tmp_path)
        async with httpx.AsyncClient(transport=httpx.ASGITransport(app=app), base_url="http://test") as client:
            _, _, created = await _authored(client)
            await gateway.called.wait()
            definition = Path(gateway.command.authored_definition_path)
            definition.rename(definition.with_suffix(".held"))
            response = await client.get(f"/runs/{created['run_id']}/definition-context")
            assert response.status_code == 409
            assert response.json()["error"]["code"] == "RunDefinitionUnavailable"
            assert str(tmp_path) not in response.text
            assert (await client.get(f"/runs/{created['run_id']}")).json()["provenance"]["state"] == "DefinitionUnavailable"
    asyncio.run(run())


def test_corrupt_frozen_definition_is_rejected_even_if_json_is_valid(tmp_path: Path):
    async def run():
        app, gateway = _application(tmp_path)
        async with httpx.AsyncClient(transport=httpx.ASGITransport(app=app), base_url="http://test") as client:
            _, _, created = await _authored(client)
            await gateway.called.wait()
            definition = Path(gateway.command.authored_definition_path)
            definition.chmod(0o644)
            data = json.loads(definition.read_bytes())
            data["nodes"][0]["x_m"] = 1234
            definition.write_text(json.dumps(data))
            response = await client.get(f"/runs/{created['run_id']}/definition-context")
            assert response.status_code == 409
            assert response.json()["error"]["code"] == "RunDefinitionUnavailable"
    asyncio.run(run())


def test_preset_context_preserves_exact_execution_override(tmp_path: Path):
    async def run():
        app, gateway = _application(tmp_path)
        async with httpx.AsyncClient(transport=httpx.ASGITransport(app=app), base_url="http://test") as client:
            draft = (await client.post("/authoring/drafts", json={"kind": "experiment", "name": "Preset override",
                "payload": {"scenario_id": "acceptance4-scenario", "bit_rate_bits_per_second": "60",
                            "simulation_cycle_count": "3", "deterministic_seed": "99",
                            "physical_layer": "Channel2Waveform", "waveform_modulation": "Bfsk"}})).json()
            response = await client.post(f"/authoring/experiments/{draft['draft_id']}/runs")
            assert response.status_code == 201, response.text
            context = await client.get(f"/runs/{response.json()['run_id']}/definition-context")
            assert context.status_code == 200, context.text
            data = context.json()
            assert data["source"] == "Preset"
            assert data["scenario"]["scenario_id"] == "acceptance4-scenario"
            assert data["experiment"]["simulation_cycle_count"] == "3"
            assert data["experiment"]["phy"]["physical_layer"] == "Channel2Waveform"
            assert data["experiment"]["mac"]["slot_duration_ns"] == "6000000000"
            assert data["execution"]["deterministic_seed"] == "99"
            assert data["scenario"]["trajectory_bindings"] == []
    asyncio.run(run())


def test_unknown_run_has_no_context_or_fallback(tmp_path: Path):
    async def run():
        app, _ = _application(tmp_path)
        async with httpx.AsyncClient(transport=httpx.ASGITransport(app=app), base_url="http://test") as client:
            response = await client.get("/runs/not-a-run/definition-context")
            assert response.status_code == 404
            assert response.json()["error"]["code"] == "NotFound"
            assert (await client.get("/runs")).json() == []
    asyncio.run(run())
