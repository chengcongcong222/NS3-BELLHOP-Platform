"""Actual ns-3 workers, not a browser acceptance test or a mocked decoder."""
import asyncio
import json
import os
import subprocess
from pathlib import Path

import httpx
import pytest

from helpers import resource_catalog
from ns3_factory_backend.api import BackendSettings, create_app
from ns3_factory_backend.run_groups import RunGroupStore
from planning_authoring_test import _scene, _experiment, _payload


pytestmark = pytest.mark.skipif(
    not all(key in os.environ for key in (
        "PLATFORM_SIM_WORKER_PATH", "PLATFORM_REFERENCE_ENVIRONMENT_BUILDER_PATH")),
    reason="requires freshly built ns-3 worker and reference asset builder",
)


@pytest.mark.parametrize("modulation", ["Bpsk", "Bfsk", "Osdm"])
def test_paired_workers_and_durable_results(tmp_path, modulation):
    async def check():
        repository = tmp_path / "repository"
        repository.mkdir()
        subprocess.run([os.environ["PLATFORM_REFERENCE_ENVIRONMENT_BUILDER_PATH"],
            str(Path(__file__).resolve().parents[2] / "environment/assets/reference_shallow_water_v1"),
            str(repository)], check=True)
        settings = BackendSettings(worker_executable=Path(os.environ["PLATFORM_SIM_WORKER_PATH"]),
            environment_repository_root=repository, authoring_workspace_root=tmp_path / "workspace",
            resource_catalog_adapter=Path(os.environ["PLATFORM_SIM_WORKER_PATH"]).with_name("platform_resource_catalog_adapter"),
            acceptance_environment_asset_id="reference-shallow-water-v1")
        app = create_app(settings)
        async with httpx.AsyncClient(transport=httpx.ASGITransport(app=app), base_url="http://test") as client:
            scene = await _scene(client, end_seconds=1000)
            payload = scene["payload"]
            payload["environment_asset_id"] = "reference-shallow-water-v1"
            changed = await client.put(f'/authoring/drafts/scenario/{scene["draft_id"]}',
                headers={"If-Match": "1"}, json={"kind": "scenario", "name": "Paired runtime", "payload": payload})
            assert changed.status_code == 200, changed.text
            experiment = await _experiment(client, {**_payload(scene["draft_id"]),
                "physical_layer": "Channel2Waveform", "planning_cycle_duration_ns": "120000000000",
                "waveform_modulation": modulation, "waveform_noise": "None",
                **({"osdm": {"block_length": 256, "vector_length": 16, "cyclic_prefix": 64,
                    "constellation": "Qpsk", "minimum_sync_correlation": 0.05,
                    "maximum_sync_search": 128}} if modulation == "Osdm" else {})})
            response = await client.post("/run-groups", json={"draft_id": experiment["draft_id"],
                "expected_revision": "1", "request_idempotency_key": "runtime-pair"})
            assert response.status_code == 201, response.text
            group_id = response.json()["group_id"]
            for _ in range(1500):
                group = (await client.get(f"/run-groups/{group_id}")).json()
                if group["state"] not in {"Queued", "RunningA", "RunningB"}:
                    break
                await asyncio.sleep(0.02)
            assert group["state"] == "Completed", group
            expected = {p["packet_id"]: p for p in group["workload"]["packets"]}
            for member in group["members"]:
                result_resource = (await client.get(f'/runs/{member["run_id"]}/results')).json()
                for field in ("experiment_id", "experiment_version", "scenario_id",
                              "scenario_version", "environment_asset_id", "environment_format_version"):
                    assert member["result"][field] == result_resource[field]
                response = await client.get(f'/runs/{member["run_id"]}/events')
                events = [json.loads(line[6:]) for line in response.text.splitlines() if line.startswith("data: ")]
                transmissions = [e["trace"]["payload"] for e in events if e.get("trace", {}).get("kind") == "Transmission"]
                assert {t["packet_id"] for t in transmissions} == set(expected)
                assert len(transmissions) == len(expected)
                for transmission in transmissions:
                    packet = expected[transmission["packet_id"]]
                    assert transmission["sender_node_id"] == packet["source_node_id"]
                    assert transmission["target"] == {"type": "Unicast", "node_id": packet["destination_node_id"]}
                assert member["result"] is not None
            assert group["members"][1]["result"]["visualization"]["waveform"]
            restored = RunGroupStore(tmp_path / "workspace/run-groups").get(group_id)
            assert restored["members"] == group["members"]
            assert restored["common_snapshot_sha256"] == group["common_snapshot_sha256"]
        await app.state.catalog.shutdown()
        restarted = create_app(settings)
        async with httpx.AsyncClient(transport=httpx.ASGITransport(app=restarted), base_url="http://test") as client:
            response = await client.get(f"/run-groups/{group_id}/results")
            assert response.status_code == 200
            assert response.json()["archived"]
            assert response.json()["members"] == group["members"]
            # Restart does not spawn workers or manufacture an event stream.
            assert (await client.get("/runs")).json() == []
            # Mutable drafts cannot affect a historical group repetition.
            changed = await client.put(f'/authoring/drafts/experiment/{experiment["draft_id"]}',
                headers={"If-Match": "1"}, json={"kind": "experiment", "name": "Unrelated edits", "payload": {}})
            assert changed.status_code == 200
            repeated = await client.post(f"/run-groups/{group_id}/rerun",
                json={"request_idempotency_key": "repeat-frozen"})
            assert repeated.status_code == 201, repeated.text
            repeated_id = repeated.json()["group_id"]
            assert repeated_id != group_id
            assert repeated.json()["workload_sha256"] == group["workload_sha256"]
            duplicate = await client.post(f"/run-groups/{group_id}/rerun",
                json={"request_idempotency_key": "repeat-frozen"})
            assert duplicate.json()["group_id"] == repeated_id
            for _ in range(1500):
                completed = (await client.get(f"/run-groups/{repeated_id}")).json()
                if completed["state"] not in {"Queued", "RunningA", "RunningB"}:
                    break
                await asyncio.sleep(0.02)
            assert completed["state"] == "Completed", completed
            for before, after in zip(group["members"], completed["members"], strict=True):
                assert before["run_id"] != after["run_id"]
                assert before["definition_sha256"] == after["definition_sha256"]
                assert before["result"]["projection"] == after["result"]["projection"]
                assert before["result"]["visualization"] == after["result"]["visualization"]
            # Integrity failure must reject the whole pair before admission.
            definition_path = tmp_path / "workspace/run-definitions" / f'{group["members"][1]["definition_sha256"]}.json'
            definition_path.chmod(0o644)
            definition_path.write_text("{}")
            refused = await client.post(f"/run-groups/{group_id}/rerun",
                json={"request_idempotency_key": "damaged-definition"})
            assert refused.status_code != 201
            assert len((await client.get("/runs")).json()) == 2
        await restarted.state.catalog.shutdown()
    asyncio.run(check())
