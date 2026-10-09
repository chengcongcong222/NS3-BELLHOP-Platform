from __future__ import annotations

import asyncio
import json
from copy import deepcopy
from pathlib import Path

import httpx
import pytest

from ns3_factory_backend.authoring import validate_payload
from run_definition_context_test import _application


def _payload(scenario_id: str = "scenario") -> dict:
    return {
        "scenario_id": scenario_id, "execution_mode": "PlanningV1",
        "routing_mode": "Direct", "mac_mode": "AdaptiveTdma",
        "planning_cycle_duration_ns": "30000000000",
        "adaptive_tdma": {"safety_margin_ns": "0"},
        "workload": "CommunicationOnly", "bit_rate_bits_per_second": "60",
        "simulation_cycle_count": "2", "deterministic_seed": "23",
        "flows": [
            {"source_node_id": "1", "destination_node_id": "0", "packets_per_cycle": 1},
            {"source_node_id": "0", "destination_node_id": "2", "packets_per_cycle": 1},
            {"source_node_id": "0", "destination_node_id": "1", "packets_per_cycle": 2},
        ],
    }


async def _scene(client, *, end_seconds=100, center=None, cannot_transmit=None):
    rows = ["track_id,time_s,x_m,y_m,z_m"]
    nodes = []
    for node in range(3):
        rows.extend([f"N{node},0,{node * 100},0,-20", f"N{node},{end_seconds},{node * 100},0,-20"])
        nodes.append({"node_id": str(node), "can_transmit": node != cannot_transmit,
                      "can_receive": True, "duplex_mode": "HalfDuplex",
                      "initial_position": {"x_meters": node * 100, "y_meters": 0, "z_meters": -20}})
    response = await client.post("/authoring/trajectories/import-csv", json={
        "asset_id": "planning-tracks", "version": 1, "name": "Planning tracks",
        "content": "\n".join(rows) + "\n"})
    assert response.status_code == 201, response.text
    response = await client.post("/authoring/drafts", json={
        "kind": "scenario", "name": "No protocol sink", "payload": {
            "environment_asset_id": "context-environment", "environment_version": 1,
            "fusion_center_node_id": center, "nodes": nodes,
            "trajectory_bindings": [{"node_id": str(node), "asset_id": "planning-tracks",
                                     "version": 1, "track_id": f"N{node}"} for node in range(3)],
        }})
    assert response.status_code == 201, response.text
    return response.json()


async def _experiment(client, payload):
    response = await client.post("/authoring/drafts", json={
        "kind": "experiment", "name": "Explicit planning", "payload": payload})
    assert response.status_code == 201, response.text
    return response.json()


def test_planning_freezes_bidirectional_multi_destination_without_sink(tmp_path: Path):
    async def run():
        app, gateway = _application(tmp_path)
        async with httpx.AsyncClient(transport=httpx.ASGITransport(app=app), base_url="http://test") as client:
            scene = await _scene(client)
            experiment = await _experiment(client, _payload(scene["draft_id"]))
            assert experiment["validation_issues"] == []
            response = await client.post(f"/authoring/experiments/{experiment['draft_id']}/runs")
            assert response.status_code == 201, response.text
            await gateway.called.wait()
            definition = json.loads(Path(gateway.command.authored_definition_path).read_bytes())
            assert definition["schema"] == "ns3-factory-authored-run-v2"
            assert definition["execution_mode"] == "PlanningV1"
            assert definition["fusion_center_node_id"] is None
            config = definition["experiment"]
            assert config["mac_mode"] == "AdaptiveTdma"
            assert config["planning_cycle_duration_ns"] == 30_000_000_000
            assert config["adaptive_tdma"] == {"safety_margin_ns": 0}
            assert config["network_update_interval_cycles"] == 1
            assert config["fusion_receiver_node_id"] is None
            assert "slot_duration_ns" not in config and "guard_interval_ns" not in config
            assert config["flows"] == [
                {"source_node_id": 0, "destination_node_id": 1, "packets_per_cycle": 2},
                {"source_node_id": 0, "destination_node_id": 2, "packets_per_cycle": 1},
                {"source_node_id": 1, "destination_node_id": 0, "packets_per_cycle": 1},
            ]
            run_id = response.json()["run_id"]
            context = await client.get(f"/runs/{run_id}/definition-context")
            assert context.status_code == 200, context.text
            captured = context.json()
            assert captured["scenario"]["fusion_center_node_id"] is None
            assert captured["experiment"]["flows"][0]["source_node_id"] == "0"
            assert captured["experiment"]["flows"][0]["packets_per_cycle"] == 2
            assert captured["experiment"]["mac"] == {"mode": "AdaptiveTdma", "slot_duration_ns": None, "guard_interval_ns": None}
            assert captured["actions"]["timing"] is None
            edited = await client.put(f"/authoring/drafts/experiment/{experiment['draft_id']}",
                headers={"If-Match": "1"}, json={"kind": "experiment", "name": "Changed", "payload": {}})
            assert edited.status_code == 200
            after = (await client.get(f"/runs/{run_id}/definition-context")).json()
            assert after["experiment"] == captured["experiment"]
    asyncio.run(run())


@pytest.mark.parametrize("change", [
    {"routing_mode": "DirectToFusionCenter"}, {"execution_mode": "PlanningV2"},
    {"execution_mode": ""}, {"execution_mode": "Legacy"},
    {"network_update_interval_cycles": "10"}, {"network_update_interval_cycles": 1},
    {"slot_duration_ns": None}, {"guard_interval_ns": "0"},
    {"planning_cycle_duration_ns": "01"}, {"planning_cycle_duration_ns": "9223372036854775807"},
    {"adaptive_tdma": {"safety_margin_ns": "-1"}},
    {"adaptive_tdma": {"safety_margin_ns": "0", "unknown": 1}},
    {"flows": []}, {"flows": [{"source_node_id": "00", "destination_node_id": "1", "packets_per_cycle": 1}]},
    {"flows": [{"source_node_id": "0", "destination_node_id": "0", "packets_per_cycle": 1}]},
    {"flows": [{"source_node_id": "0", "destination_node_id": "1", "packets_per_cycle": True}]},
    {"flows": [{"source_node_id": "0", "destination_node_id": "1", "packets_per_cycle": 1001}]},
    {"flows": [{"source_node_id": "0", "destination_node_id": "1", "packets_per_cycle": 1}] * 2},
    {"workload": "BearingFusion", "flows": []},
])
def test_invalid_planning_configuration_is_rejected(change):
    assert validate_payload("experiment", {**_payload(), **change})


@pytest.mark.parametrize(("field", "bad_value"), [
    ("minimum_bearing_points", "bad"), ("minimum_bearing_points", "05"),
    ("minimum_bearing_points", True), ("minimum_bearing_points", 5),
    ("minimum_bearing_points", "4"), ("minimum_bearing_points", "4294967296"),
    ("maximum_fusion_period_ns", "bad"), ("maximum_fusion_period_ns", "0"),
    ("maximum_fusion_period_ns", "9223372036854775808"),
    ("target_x_m", "bad"), ("target_y_m", "nan"),
    ("target_x_m", None), ("target_y_m", False),
    ("maximum_ber", "0.01"), ("maximum_ber", 0), ("maximum_ber", 1),
    ("maximum_ber", -0.1),
])
def test_communication_rejects_invalid_optional_fusion_fields_before_freeze(tmp_path: Path, field, bad_value):
    async def run():
        app, gateway = _application(tmp_path)
        async with httpx.AsyncClient(transport=httpx.ASGITransport(app=app), base_url="http://test") as client:
            experiment = await _experiment(client, {**_payload(), field: bad_value})
            assert any(field in issue for issue in experiment["validation_issues"])
            response = await client.post(f"/authoring/experiments/{experiment['draft_id']}/runs")
            assert response.status_code == 400, response.text
            assert response.json()["error"]["code"] == "DraftValidationFailed"
            assert field in response.json()["error"]["message"]
            assert gateway.command is None
            assert (await client.get("/runs")).json() == []
            assert not (tmp_path / "workspace" / "run-definitions").exists()
    asyncio.run(run())


@pytest.mark.parametrize("bad_value", [float("inf"), float("-inf"), float("nan"), 10**400])
@pytest.mark.parametrize("field", ["target_x_m", "target_y_m", "maximum_ber"])
def test_optional_fusion_fields_reject_nonfinite_and_overflow_numbers(field, bad_value):
    assert any(field in issue for issue in validate_payload("experiment", {**_payload(), field: bad_value}))


def test_optional_fusion_values_are_preserved_when_valid():
    assert validate_payload("experiment", {**_payload(), "network_update_interval_cycles": "1", "minimum_bearing_points": "5",
        "maximum_fusion_period_ns": "180000000000", "target_x_m": -20.5,
        "target_y_m": 0, "maximum_ber": 0.0001}) == []


@pytest.mark.parametrize("case", ["missing-node", "capability", "short-trajectory", "duplicate-binding"])
def test_planning_cross_references_fail_before_worker_creation(tmp_path: Path, case):
    async def run():
        app, gateway = _application(tmp_path)
        async with httpx.AsyncClient(transport=httpx.ASGITransport(app=app), base_url="http://test") as client:
            scene = await _scene(client, end_seconds=50 if case == "short-trajectory" else 100,
                                 cannot_transmit=0 if case == "capability" else None)
            payload = _payload(scene["draft_id"])
            if case == "missing-node":
                payload["flows"][0]["destination_node_id"] = "999"
            if case == "duplicate-binding":
                changed = deepcopy(scene["payload"])
                changed["trajectory_bindings"].append(changed["trajectory_bindings"][0])
                await client.put(f"/authoring/drafts/scenario/{scene['draft_id']}", headers={"If-Match": "1"},
                                 json={"kind": "scenario", "name": scene["name"], "payload": changed})
            experiment = await _experiment(client, payload)
            response = await client.post(f"/authoring/experiments/{experiment['draft_id']}/runs")
            assert response.status_code == (409 if case == "duplicate-binding" else 400), response.text
            assert gateway.command is None
            assert (await client.get("/runs")).json() == []
    asyncio.run(run())


def test_bearing_receiver_is_explicit_and_independent_of_scenario_center(tmp_path: Path):
    async def run():
        app, gateway = _application(tmp_path)
        async with httpx.AsyncClient(transport=httpx.ASGITransport(app=app), base_url="http://test") as client:
            scene = await _scene(client, center="0")
            payload = {**_payload(scene["draft_id"]), "workload": "BearingFusion", "flows": [],
                       "fusion_receiver_node_id": "2", "mac_mode": "Tdma",
                       "slot_duration_ns": "6000000000", "guard_interval_ns": "2000000000"}
            payload.pop("adaptive_tdma")
            experiment = await _experiment(client, payload)
            response = await client.post(f"/authoring/experiments/{experiment['draft_id']}/runs")
            assert response.status_code == 201, response.text
            await gateway.called.wait()
            definition = json.loads(Path(gateway.command.authored_definition_path).read_bytes())
            assert definition["fusion_center_node_id"] == 0
            assert definition["experiment"]["fusion_receiver_node_id"] == 2
            context = (await client.get(f"/runs/{response.json()['run_id']}/definition-context")).json()
            assert context["experiment"]["fusion_receiver_node_id"] == "2"
            assert context["experiment"]["mac"]["slot_duration_ns"] == "6000000000"
            assert context["actions"]["timing"]["planned_payload_bytes"] == "15"
    asyncio.run(run())


def test_planning_cannot_silently_use_preset_execution(tmp_path: Path):
    async def run():
        app, gateway = _application(tmp_path)
        async with httpx.AsyncClient(transport=httpx.ASGITransport(app=app), base_url="http://test") as client:
            experiment = await _experiment(client, _payload("acceptance4-scenario"))
            response = await client.post(f"/authoring/experiments/{experiment['draft_id']}/runs")
            assert response.status_code == 409
            assert "派生" in response.json()["error"]["message"]
            assert gateway.command is None
    asyncio.run(run())


def test_legacy_draft_still_freezes_v1_with_integer_center(tmp_path: Path):
    async def run():
        app, gateway = _application(tmp_path)
        async with httpx.AsyncClient(transport=httpx.ASGITransport(app=app), base_url="http://test") as client:
            scene = await _scene(client, center=0)
            experiment = await _experiment(client, {
                "scenario_id": scene["draft_id"], "workload": "CommunicationOnly",
                "bit_rate_bits_per_second": "60", "simulation_cycle_count": "2", "deterministic_seed": "1"})
            response = await client.post(f"/authoring/experiments/{experiment['draft_id']}/runs")
            assert response.status_code == 201, response.text
            await gateway.called.wait()
            definition = json.loads(Path(gateway.command.authored_definition_path).read_bytes())
            assert definition["schema"] == "ns3-factory-authored-run-v1"
            assert definition["fusion_center_node_id"] == 0
            assert definition["experiment"]["mac_mode"] == "Tdma"
            assert "execution_mode" not in definition
    asyncio.run(run())


def test_planning_rerun_preserves_exact_frozen_flows_after_draft_edit(tmp_path: Path):
    from api_test import _wait_terminal
    from run_actions_test import _setup

    async def run():
        app, gateway = _setup(tmp_path)
        async with httpx.AsyncClient(transport=httpx.ASGITransport(app=app), base_url="http://test") as client:
            scene = await _scene(client)
            experiment = await _experiment(client, _payload(scene["draft_id"]))
            created = await client.post(f"/authoring/experiments/{experiment['draft_id']}/runs")
            assert created.status_code == 201, created.text
            run_id = created.json()["run_id"]
            await _wait_terminal(client, run_id)
            original = (await client.get(f"/runs/{run_id}/definition-context")).json()
            edited = await client.put(f"/authoring/drafts/experiment/{experiment['draft_id']}",
                headers={"If-Match": "1"}, json={"kind": "experiment", "name": "Changed", "payload": {}})
            assert edited.status_code == 200
            repeated = await client.post(f"/runs/{run_id}/rerun")
            assert repeated.status_code == 201, repeated.text
            new_id = repeated.json()["run_id"]
            await _wait_terminal(client, new_id)
            first, second = gateway.commands
            assert first.model_dump(exclude={"run_id"}) == second.model_dump(exclude={"run_id"})
            context = (await client.get(f"/runs/{new_id}/definition-context")).json()
            assert context["experiment"] == original["experiment"]
            assert context["definition_sha256"] == original["definition_sha256"]
    asyncio.run(run())
