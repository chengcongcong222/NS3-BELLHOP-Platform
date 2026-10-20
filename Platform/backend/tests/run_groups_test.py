import asyncio
import json
from pathlib import Path

import httpx
import pytest

from ns3_factory_backend.catalog import CatalogError
from ns3_factory_backend.run_groups import RunGroups, RunGroupStore, digest
from run_batch_catalog_test import SerialGateway, catalog
from gateway_test import _command
from planning_authoring_test import _scene, _experiment, _payload
from run_definition_context_test import _application


async def prepared():
    return (_command, _command), {"scene": "fixture"}, ({"member": "A"}, {"member": "B"}), {"packets": []}


async def settle(groups):
    await asyncio.wait_for(groups.shutdown(), 2)


@pytest.mark.parametrize("fail_first", [False, True])
def test_archive_completion_idempotence_and_restart(tmp_path, fail_first):
    async def check():
        gateway = SerialGateway(fail_first)
        runs = catalog(gateway)
        groups = RunGroups(runs, tmp_path)
        a, b = await asyncio.gather(groups.create("same", {"revision": 1}, prepared),
                                    groups.create("same", {"revision": 1}, prepared))
        assert a["group_id"] == b["group_id"]
        assert len(await runs.list_runs()) == 2
        with pytest.raises(CatalogError):
            await groups.create("same", {"revision": 2}, prepared)
        gateway.release[0].set()
        gateway.release[1].set()
        await settle(groups)
        result = await groups.view(a["group_id"])
        assert result["state"] == ("PartialFailed" if fail_first else "Completed")
        assert result["members"][1]["result"] is not None
        restored = RunGroups(catalog(SerialGateway()), tmp_path)
        archive = await restored.view(a["group_id"])
        assert archive["archived"] and not archive["events_available"]
        assert archive["members"] == result["members"]
        assert archive["workload_sha256"] == digest(archive["workload"])
        await runs.shutdown()
    asyncio.run(check())


def test_cancel_b_retains_a_and_restart_marks_unknown(tmp_path):
    async def check():
        gateway = SerialGateway()
        runs = catalog(gateway)
        groups = RunGroups(runs, tmp_path)
        group = await groups.create("cancel", {}, prepared)
        # Read restart normalization before original process progresses; no workers
        # are resumed by reading the archive. Simulated crash evidence only.
        recovered = RunGroupStore(tmp_path).get(group["group_id"])
        assert all(m["state"] == "Interrupted" for m in recovered["members"])
        canceled = await groups.cancel_pending(group["group_id"])
        assert canceled["members"][1]["state"] == "Canceled"
        gateway.release[0].set()
        await settle(groups)
        result = await groups.view(group["group_id"])
        assert result["state"] == "PartialFailed"
        assert result["members"][0]["result"] is not None
        assert result["members"][1]["result"] is None
        assert gateway.calls == ["run-a"]
        await runs.shutdown()
    asyncio.run(check())


def test_failed_durable_publication_starts_zero_workers(tmp_path, monkeypatch):
    async def check():
        gateway = SerialGateway()
        runs = catalog(gateway)
        groups = RunGroups(runs, tmp_path)
        def fail(record):
            raise OSError("fixture disk full")
        monkeypatch.setattr(groups.store, "save", fail)
        with pytest.raises(OSError):
            await groups.create("disk-full", {}, prepared)
        assert await runs.list_runs() == ()
        assert gateway.calls == []
        await runs.shutdown()
    asyncio.run(check())


def test_corrupt_archive_fails_closed(tmp_path):
    async def check():
        gateway = SerialGateway()
        runs = catalog(gateway)
        groups = RunGroups(runs, tmp_path)
        group = await groups.create("corrupt", {}, prepared)
        gateway.release[0].set()
        gateway.release[1].set()
        await settle(groups)
        path = tmp_path / f'{group["group_id"]}.json'
        envelope = json.loads(path.read_bytes())
        envelope["record"]["common_snapshot"]["scene"] = "tampered"
        path.write_text(json.dumps(envelope))
        with pytest.raises(ValueError, match="checksum"):
            RunGroupStore(tmp_path)
        await runs.shutdown()
    asyncio.run(check())


def test_result_archive_failure_is_visible_not_false_completion(tmp_path, monkeypatch):
    async def check():
        gateway = SerialGateway()
        runs = catalog(gateway)
        groups = RunGroups(runs, tmp_path)
        group = await groups.create("late-disk-full", {}, prepared)
        def fail(record):
            raise OSError("private filesystem path")
        monkeypatch.setattr(groups.store, "save", fail)
        gateway.release[0].set()
        gateway.release[1].set()
        await settle(groups)
        result = await groups.view(group["group_id"])
        assert result["state"] == "ArchiveFailed"
        assert result["archive_failure"]["code"] == "ArchiveWriteFailed"
        assert "private filesystem" not in json.dumps(result)
        restored = RunGroupStore(tmp_path).get(group["group_id"])
        assert all(m["state"] == "Interrupted" for m in restored["members"])
        await runs.shutdown()
    asyncio.run(check())


def test_real_authoring_api_freezes_two_definitions_without_mutating_draft(tmp_path):
    async def check():
        app, gateway = _application(tmp_path)
        async with httpx.AsyncClient(transport=httpx.ASGITransport(app=app), base_url="http://test") as client:
            scene = await _scene(client)
            experiment = await _experiment(client, _payload(scene["draft_id"]))
            request = {"draft_id": experiment["draft_id"], "expected_revision": "1",
                       "request_idempotency_key": "paired-test"}
            created = await client.post("/run-groups", json=request)
            assert created.status_code == 201, created.text
            record = created.json()
            assert len(record["members"]) == 2
            assert record["members"][0]["definition_sha256"] != record["members"][1]["definition_sha256"]
            assert record["workload"]["count"] == "8"
            assert record["workload"]["packets"][0]["packet_id"] == "1000000"
            assert record["workload"]["packets"][-1]["packet_id"] == "1000007"
            assert record["members"][0]["definition"]["experiment"]["physical_layer"] == "Channel1Abstract"
            assert record["members"][1]["definition"]["experiment"]["physical_layer"] == "Channel2Waveform"
            duplicate = await client.post("/run-groups", json=request)
            assert duplicate.json()["group_id"] == record["group_id"]
            assert len((await client.get("/runs")).json()) == 2
            assert str(tmp_path) not in created.text
            unchanged = await client.get(f'/authoring/drafts/experiment/{experiment["draft_id"]}')
            assert unchanged.json()["payload"] == experiment["payload"]
            export = await client.get(f'/run-groups/{record["group_id"]}/export')
            assert export.json()["workload_sha256"] == record["workload_sha256"]
        await app.state.catalog.shutdown()
    asyncio.run(check())
