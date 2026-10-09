from __future__ import annotations

import asyncio
import json
from pathlib import Path

import httpx
import pytest

from api_test import ControlledGateway, _wait_terminal
from helpers import failure_result, resource_catalog
from ns3_factory_backend.api import BackendSettings, create_app
from run_definition_context_test import _authored


class CapturingFailureGateway:
    def __init__(self):
        self.commands = []

    async def run(self, command, _callback):
        self.commands.append(command)
        return failure_result(command.run_id)

    async def shutdown(self):
        pass


def _setup(tmp_path: Path, gateway=None):
    resources = resource_catalog()
    # Extend the explicitly injected test catalog; no fallback from authored IDs.
    from ns3_factory_backend.resources import ResourceCatalog, ResourceCatalogDocument
    environment = resources.list_environments()[0].model_copy(update={"environment_asset_id": "context-environment"})
    resources = ResourceCatalog(ResourceCatalogDocument(schema_version=1,
        environments=(*resources.list_environments(), environment),
        scenarios=resources.list_scenarios(), experiments=resources.list_experiments()))
    for asset_id in ["context-environment", "backend-field-v1"]:
        (tmp_path / "repo" / asset_id).mkdir(parents=True)
    gateway = gateway or CapturingFailureGateway()
    app = create_app(BackendSettings(worker_executable=tmp_path / "worker",
        environment_repository_root=tmp_path / "repo", authoring_workspace_root=tmp_path / "workspace"),
        gateway=gateway, resource_catalog=resources)
    return app, gateway


def test_failed_run_actions_and_exact_rerun_ignore_later_draft_edits(tmp_path: Path):
    async def run():
        app, gateway = _setup(tmp_path)
        async with httpx.AsyncClient(transport=httpx.ASGITransport(app=app), base_url="http://test") as client:
            scene, experiment, created = await _authored(client)
            old_id = created["run_id"]
            original = await _wait_terminal(client, old_id)
            assert original["lifecycle"] == "Failed"
            before = (await client.get(f"/runs/{old_id}/actions")).json()
            assert before["experiment_name"] == "Exact experiment"
            assert before["scenario_name"] == "Exact scene"
            assert before["edit"]["experiment_draft_id"] == experiment["draft_id"]
            assert before["edit"]["scenario_draft_id"] == scene["draft_id"]
            assert before["edit"]["captured_revision"] == 1
            assert before["edit"]["available"] and before["rerun"]["available"]
            assert before["timing"]["planned_payload_bytes"] == "4"
            assert before["timing"]["encoded_bytes"] == "8"
            assert before["timing"]["minimum_airtime_ns"] == "1066666667"
            assert before["timing"]["minimum_slot_duration_ns"] == "3066666667"
            for draft in [scene, experiment]:
                response = await client.put(f"/authoring/drafts/{draft['kind']}/{draft['draft_id']}",
                    headers={"If-Match": "1"}, json={"kind": draft["kind"], "name": "Later edits", "payload": {}})
                assert response.status_code == 200
            actions = (await client.get(f"/runs/{old_id}/actions")).json()
            assert actions["experiment_name"] == "Exact experiment"
            assert actions["edit"]["captured_revision"] == 1 and actions["edit"]["current_revision"] == 2
            repeated = await client.post(f"/runs/{old_id}/rerun")
            assert repeated.status_code == 201, repeated.text
            new_id = repeated.json()["run_id"]
            assert new_id != old_id
            await _wait_terminal(client, new_id)
            old_command, new_command = gateway.commands
            assert old_command.model_dump(exclude={"run_id"}) == new_command.model_dump(exclude={"run_id"})
            assert repeated.json()["provenance"] == original["provenance"]
            assert (await client.get(f"/runs/{old_id}")).json() == original
            assert (await client.get(f"/authoring/drafts/experiment/{experiment['draft_id']}")).json()["revision"] == 2
            assert len((await client.get("/runs")).json()) == 2
    asyncio.run(run())


@pytest.mark.parametrize("missing", ["definition", "environment", "trajectory"])
def test_rerun_missing_frozen_input_is_rejected_before_creating_run(tmp_path: Path, missing):
    async def run():
        app, gateway = _setup(tmp_path)
        async with httpx.AsyncClient(transport=httpx.ASGITransport(app=app), base_url="http://test") as client:
            _, _, created = await _authored(client)
            await _wait_terminal(client, created["run_id"])
            paths = {"definition": Path(gateway.commands[0].authored_definition_path),
                     "environment": tmp_path / "repo" / "context-environment",
                     "trajectory": tmp_path / "workspace/published/trajectories/context-track.v1.json"}
            target = paths[missing]
            target.rename(target.with_name(target.name + ".held"))
            actions = (await client.get(f"/runs/{created['run_id']}/actions")).json()
            assert actions["rerun"]["available"] is False
            repeated = await client.post(f"/runs/{created['run_id']}/rerun")
            assert repeated.status_code in {404, 409}
            assert len((await client.get("/runs")).json()) == len(gateway.commands) == 1
    asyncio.run(run())


def test_rerun_rejects_damaged_trajectory_without_repair_or_current_draft_fallback(tmp_path: Path):
    async def run():
        app, gateway = _setup(tmp_path)
        async with httpx.AsyncClient(transport=httpx.ASGITransport(app=app), base_url="http://test") as client:
            _, _, created = await _authored(client)
            await _wait_terminal(client, created["run_id"])
            path = tmp_path / "workspace/published/trajectories/context-track.v1.json"
            asset = json.loads(path.read_text())
            asset["tracks"][0]["points"][0]["x_m"] += 1
            path.write_text(json.dumps(asset))
            response = await client.post(f"/runs/{created['run_id']}/rerun")
            assert response.status_code == 409
            assert response.json()["error"]["code"] == "TrajectoryResourceInvalid"
            assert len(gateway.commands) == 1
    asyncio.run(run())


def test_missing_original_draft_disables_edit_but_exact_rerun_remains_available(tmp_path: Path):
    async def run():
        app, _ = _setup(tmp_path)
        async with httpx.AsyncClient(transport=httpx.ASGITransport(app=app), base_url="http://test") as client:
            _, experiment, created = await _authored(client)
            await _wait_terminal(client, created["run_id"])
            path = tmp_path / "workspace/drafts/experiment" / f"{experiment['draft_id']}.json"
            path.rename(path.with_suffix(".held"))
            actions = (await client.get(f"/runs/{created['run_id']}/actions")).json()
            assert not actions["edit"]["available"]
            assert actions["edit"]["experiment_draft_id"] == experiment["draft_id"]
            assert actions["rerun"]["available"]
            assert (await client.post(f"/runs/{created['run_id']}/rerun")).status_code == 201
    asyncio.run(run())


def test_preset_rerun_preserves_execution_and_single_active_gate(tmp_path: Path):
    async def run():
        gateway = ControlledGateway()
        app, _ = _setup(tmp_path, gateway)
        async with httpx.AsyncClient(transport=httpx.ASGITransport(app=app), base_url="http://test") as client:
            original = await client.post("/runs", json={"experiment_id": "acceptance4-experiment", "experiment_version": "1"})
            old_id = original.json()["run_id"]
            await gateway.first_event_emitted.wait()
            assert (await client.post(f"/runs/{old_id}/rerun")).status_code == 409
            gateway.release.set()
            await _wait_terminal(client, old_id)
            captured = await app.state.catalog.captured_command(old_id)
            gateway.release.clear()
            repeated = await client.post(f"/runs/{old_id}/rerun")
            assert repeated.status_code == 201
            new_id = repeated.json()["run_id"]
            assert (await app.state.catalog.captured_command(new_id)).model_dump(exclude={"run_id"}) == captured.model_dump(exclude={"run_id"})
            busy = await client.post(f"/runs/{old_id}/rerun")
            assert busy.status_code == 409 and busy.json()["error"]["code"] == "BackendBusy"
            gateway.release.set()
            await _wait_terminal(client, new_id)
    asyncio.run(run())


def test_failed_run_without_events_has_explicit_zero_stream_end(tmp_path: Path):
    async def run():
        app, _ = _setup(tmp_path)
        async with httpx.AsyncClient(transport=httpx.ASGITransport(app=app), base_url="http://test") as client:
            _, _, created = await _authored(client)
            await _wait_terminal(client, created["run_id"])
            response = await client.get(f"/runs/{created['run_id']}/events")
            assert response.text == 'event: stream-end\ndata: {"last_sequence":"0"}\n\n'
            assert (await client.get(f"/runs/{created['run_id']}")).json()["lifecycle"] == "Failed"
    asyncio.run(run())
