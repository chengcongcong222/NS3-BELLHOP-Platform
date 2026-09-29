"""HTTP freeze/rejection gates; the gateway and published environment are fixtures.

These tests exercise the real authoring, trajectory compilation, geometry checks,
definition serialization and Run catalog. They do not claim a physical ns-3 run.
"""
from __future__ import annotations

import asyncio
from copy import deepcopy
import hashlib
import json
from pathlib import Path

import httpx
import pytest

from authoring_api_test import CapturingGateway
from helpers import resource_catalog
from ns3_factory_backend.api import BackendSettings, create_app
from ns3_factory_backend.authoring import AuthoringStore
from ns3_factory_backend.resources import ResourceCatalog, ResourceCatalogDocument


DOMAIN = {"model": "ProfileExtrusion", "x_min_m": 0, "x_max_m": 100,
          "y_min_m": -30, "y_max_m": 30, "surface_clearance_m": 2,
          "bottom_clearance_m": 5}
ENVIRONMENT_INPUTS = {
    "sound_speed_profile": [{"depth_meters": 0, "speed_meters_per_second": 1500},
                            {"depth_meters": 200, "speed_meters_per_second": 1500}],
    "bathymetry": [{"range_meters": 0, "depth_meters": 100},
                   {"range_meters": 100, "depth_meters": 100}],
}


def application(tmp_path: Path, monkeypatch: pytest.MonkeyPatch, inputs: dict):
    base = resource_catalog()
    water = base.get_environment("backend-field-v1")
    axes = water.axes.model_copy(update={
        "source_depth": water.axes.source_depth.model_copy(update={"minimum": 1.0, "maximum": 150.0, "count": "2"}),
        "receiver_depth": water.axes.receiver_depth.model_copy(update={"minimum": 1.0, "maximum": 150.0, "count": "2"}),
        "horizontal_range": water.axes.horizontal_range.model_copy(update={"minimum": 0.0, "maximum": 1000.0, "count": "2"}),
    })
    resources = ResourceCatalog(ResourceCatalogDocument(schema_version=1,
        environments=(water.model_copy(update={"axes": axes}),),
        scenarios=base.list_scenarios(), experiments=base.list_experiments()))
    monkeypatch.setattr(AuthoringStore, "environment_input_snapshot",
        lambda _store, asset_id: {"name": "Published input fixture", "revision": 1,
            "payload": deepcopy(inputs)} if asset_id == "backend-field-v1" else None)
    (tmp_path / "repo" / "backend-field-v1").mkdir(parents=True)
    gateway = CapturingGateway()
    app = create_app(BackendSettings(worker_executable=tmp_path / "worker",
        environment_repository_root=tmp_path / "repo", authoring_workspace_root=tmp_path / "workspace"),
        gateway=gateway, resource_catalog=resources)
    return app, gateway


async def composition(client: httpx.AsyncClient, path: list[tuple], domain: dict):
    end = path[-1][0]
    rows = ["track_id,time_s,x_m,y_m,z_m", "N0,0,0,10,-20", f"N0,{end},0,10,-20"]
    rows += [f"N1,{t},{x},{y},{z}" for t, x, y, z in path]
    imported = await client.post("/authoring/trajectories/import-csv", json={
        "asset_id": "domain-path", "version": 1, "name": "Scene domain API fixture",
        "content": "\n".join(rows) + "\n"})
    assert imported.status_code == 201, imported.text
    nodes = []
    for node_id, x, y, z in [(0, 0, 10, -20), (1, *path[0][1:])]:
        nodes.append({"node_id": str(node_id), "can_transmit": node_id != 0,
            "can_receive": True, "duplex_mode": "HalfDuplex",
            "initial_position": {"x_meters": x, "y_meters": y, "z_meters": z},
            "initial_velocity": {"x_meters_per_second": 0,
                "y_meters_per_second": 0, "z_meters_per_second": 0}})
    scene = await client.post("/authoring/drafts", json={"kind": "scenario",
        "name": "Explicit 2D profile domain", "payload": {
            "environment_asset_id": "backend-field-v1", "environment_version": 1,
            "fusion_center_node_id": "0", "nodes": nodes, "spatial_domain": domain,
            "trajectory_bindings": [{"node_id": str(node), "asset_id": "domain-path",
                "version": 1, "track_id": f"N{node}"} for node in [0, 1]]}})
    assert scene.status_code == 201 and scene.json()["validation_issues"] == [], scene.text
    experiment = await client.post("/authoring/drafts", json={"kind": "experiment",
        "name": "Two six-second communication cycles", "payload": {
            "scenario_id": scene.json()["draft_id"], "scenario_revision": scene.json()["revision"],
            "bit_rate_bits_per_second": "60", "simulation_cycle_count": "2",
            "slot_duration_ns": "6000000000", "guard_interval_ns": "2000000000",
            "deterministic_seed": "17", "physical_layer": "Channel1Abstract",
            "routing_mode": "DirectToFusionCenter", "mac_mode": "Tdma",
            "workload": "CommunicationOnly"}})
    assert experiment.status_code == 201 and experiment.json()["validation_issues"] == [], experiment.text
    return scene.json(), experiment.json()


def test_api_freezes_exact_profile_domain_and_uses_only_selected_run_interval(tmp_path, monkeypatch):
    async def run():
        app, gateway = application(tmp_path, monkeypatch, ENVIRONMENT_INPUTS)
        async with httpx.AsyncClient(transport=httpx.ASGITransport(app=app), base_url="http://test") as client:
            # Movement after 12 s leaves the domain; it is outside this Run and
            # must not cause an otherwise valid experiment to be rejected.
            scene, experiment = await composition(client,
                [(0, 10, 0, -50), (12, 90, 0, -50), (20, 200, 0, -50)], DOMAIN)
            response = await client.post(f"/authoring/experiments/{experiment['draft_id']}/runs")
            assert response.status_code == 201, response.text
            await asyncio.wait_for(gateway.called.wait(), 1)
            definition_path = Path(gateway.command.authored_definition_path)
            encoded = definition_path.read_bytes()
            definition = json.loads(encoded)
            assert definition["spatial_domain"] == DOMAIN
            assert hashlib.sha256(encoded).hexdigest() == gateway.command.authored_definition_sha256
            assert len((await client.get("/runs")).json()) == 1
            endpoint = f"/runs/{response.json()['run_id']}/definition-context"
            context = await client.get(endpoint)
            assert context.status_code == 200, context.text
            assert context.json()["scenario"]["spatial_domain"] == DOMAIN
            changed = await client.put(f"/authoring/drafts/scenario/{scene['draft_id']}",
                headers={"If-Match": str(scene["revision"])}, json={"kind": "scenario",
                    "name": scene["name"], "payload": {**scene["payload"],
                        "spatial_domain": {**DOMAIN, "x_max_m": 60}}})
            assert changed.status_code == 200
            assert (await client.get(endpoint)).json()["scenario"]["spatial_domain"] == DOMAIN
            assert definition_path.read_bytes() == encoded
        await app.state.catalog.shutdown()
    asyncio.run(run())


@pytest.mark.parametrize("path,patch,inputs,message", [
    ([(0, 10, 0, -50), (12, 101, 0, -50)], {}, ENVIRONMENT_INPUTS, "移出"),
    ([(0, 10, 0, -50), (12, 90, 31, -50)], {}, ENVIRONMENT_INPUTS, "移出"),
    ([(0, 10, 0, -50), (12, 90, 0, 1)], {}, ENVIRONMENT_INPUTS, "水面"),
    ([(0, 10, 0, -50), (12, 90, 0, -98)], {}, ENVIRONMENT_INPUTS, "离底"),
    ([(0, 10, 0, -50), (11, 90, 0, -50)], {}, ENVIRONMENT_INPUTS, "覆盖"),
    ([(0, 10, 0, -50), (1e308, 90, 0, -50)], {}, ENVIRONMENT_INPUTS, "时间"),
    ([(0, 10, 0, -50), (1e10, 90, 0, -50)], {}, ENVIRONMENT_INPUTS, "时间"),
    ([(0, 10, 0, -50), (12, 90, 0, -50)], {"model": "Implicit3D"}, ENVIRONMENT_INPUTS, "二维剖面"),
    ([(0, 10, 0, -50), (12, 90, 0, -50)], {"x_max_m": 10**400}, ENVIRONMENT_INPUTS, "有限数字"),
    ([(0, 10, 0, -50), (12, 90, 0, -50)], {}, {
        **ENVIRONMENT_INPUTS, "bathymetry": [{"range_meters": x, "depth_meters": d}
            for x, d in [(0, 100), (50, 30), (100, 100)]]}, "海底"),
])
def test_invalid_scene_paths_return_400_before_run_creation(tmp_path, monkeypatch, path, patch, inputs, message):
    async def run():
        app, gateway = application(tmp_path, monkeypatch, inputs)
        async with httpx.AsyncClient(transport=httpx.ASGITransport(app=app, raise_app_exceptions=False), base_url="http://test") as client:
            _, experiment = await composition(client, path, {**DOMAIN, **patch})
            response = await client.post(f"/authoring/experiments/{experiment['draft_id']}/runs")
            assert response.status_code == 400, response.text
            assert response.json()["error"]["code"] == "SceneDomainViolation"
            assert message in response.json()["error"]["message"]
            assert (await client.get("/runs")).json() == []
            assert not gateway.called.is_set()
            assert not list((tmp_path / "workspace" / "run-definitions").glob("*.json"))
        await app.state.catalog.shutdown()
    asyncio.run(run())
