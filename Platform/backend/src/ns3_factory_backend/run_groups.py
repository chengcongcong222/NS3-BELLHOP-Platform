"""Durable comparison metadata; never a simulation scheduler or result estimator."""
from __future__ import annotations

import asyncio
import hashlib
import json
import os
import tempfile
import uuid
from copy import deepcopy
from pathlib import Path

from .catalog import CatalogError, InMemoryRunCatalog


def canonical(value) -> bytes:
    return json.dumps(value, ensure_ascii=False, sort_keys=True,
                      separators=(",", ":"), allow_nan=False).encode()


def digest(value) -> str:
    return hashlib.sha256(canonical(value)).hexdigest()


def group_state(members) -> str:
    states = [m["state"] for m in members]
    if "Running" in states:
        return "Running" + members[states.index("Running")]["member"]
    if "Queued" in states:
        return "Queued"
    if all(s == "Completed" for s in states):
        return "Completed"
    if "Completed" in states:
        return "PartialFailed"
    if all(s == "Canceled" for s in states):
        return "Canceled"
    if "Interrupted" in states:
        return "Interrupted"
    return "Failed"


class RunGroupStore:
    """One backend owns this directory; envelope checksum detects corruption."""
    def __init__(self, root: Path):
        self.root = root
        self.records = {}
        self.keys = {}
        for path in sorted(root.glob("group-*.json")):
            if path.stat().st_size > 32 * 1024 * 1024:
                raise ValueError("Run group archive exceeds budget")
            envelope = json.loads(path.read_bytes())
            record = envelope["record"]
            if (envelope["sha256"] != digest(record) or
                    record["schema"] != "ns3-factory-run-group-v1" or
                    path.stem != record["group_id"] or
                    record["request_key"] in self.keys):
                raise ValueError("Invalid run group archive identity or checksum")
            for member in record["members"]:
                if member["state"] in {"Queued", "Running"}:
                    member["state"] = "Interrupted"
                    member["failure"] = {"code": "Interrupted", "message": "Backend restarted; no automatic resume."}
            record["archived"] = True
            self.save(record)

    def save(self, record):
        encoded = canonical({"record": record, "sha256": digest(record)})
        if len(encoded) > 32 * 1024 * 1024:
            raise CatalogError("ArchiveBudgetExceeded", "Comparison archive exceeds 32 MiB.")
        self.root.mkdir(parents=True, exist_ok=True)
        fd, temporary = tempfile.mkstemp(prefix=".group-", dir=self.root)
        try:
            with os.fdopen(fd, "wb") as stream:
                stream.write(encoded)
                stream.flush()
                os.fsync(stream.fileno())
            os.replace(temporary, self.root / f'{record["group_id"]}.json')
            directory = os.open(self.root, os.O_RDONLY | os.O_DIRECTORY)
            try:
                os.fsync(directory)
            finally:
                os.close(directory)
        finally:
            if os.path.exists(temporary):
                os.unlink(temporary)
        self.records[record["group_id"]] = deepcopy(record)
        self.keys[record["request_key"]] = record["group_id"]

    def get(self, group_id):
        if group_id not in self.records:
            raise CatalogError("NotFound", "Comparison group was not found.")
        return deepcopy(self.records[group_id])


class RunGroups:
    def __init__(self, catalog: InMemoryRunCatalog, root: Path):
        self.catalog = catalog
        self.store = RunGroupStore(root)
        self.lock = asyncio.Lock()
        self.tasks = set()
        self.archive_errors = {}

    async def create(self, request_key, request_identity, prepare):
        # Serialize only control-plane admissions, not ns-3 callbacks. Preparation
        # is performed once, inside idempotency protection, before catalog mutation.
        async with self.lock:
            prior = self.store.keys.get(request_key)
            identity = digest(request_identity)
            if prior:
                record = self.store.get(prior)
                if record["request_sha256"] != identity:
                    raise CatalogError("IdempotencyConflict", "Request key already belongs to different inputs.")
                return await self.view(prior)
            factories, common, definitions, workload = await prepare()
            record = {"schema": "ns3-factory-run-group-v1", "group_id": f"group-{uuid.uuid4().hex}",
                "request_key": request_key, "request_sha256": identity, "archived": False,
                "common_snapshot": common, "common_snapshot_sha256": digest(common),
                "workload": workload, "workload_sha256": digest(workload),
                "events_available": False, "members": []}

            def persist(commands):
                record["members"] = [{"member": label, "run_id": command.run_id,
                    "state": "Queued", "definition": definition,
                    "definition_sha256": command.authored_definition_sha256,
                    "execution": command.execution.model_dump(mode="json"),
                    "result": None, "failure": None}
                    for label, command, definition in zip(("A", "B"), commands, definitions, strict=True)]
                self.store.save(record)

            await self.catalog.create_batch(factories, before_start=persist)
            task = asyncio.create_task(self._archive_checked(record["group_id"]))
            self.tasks.add(task)
            task.add_done_callback(self.tasks.discard)
            return await self.view(record["group_id"])

    async def _archive_checked(self, group_id):
        try:
            await self._archive(group_id)
        except (OSError, CatalogError) as error:
            # Do not let a completed simulation masquerade as a durable result.
            # Existing on-disk members remain intact; restart treats unfinished
            # records as Interrupted. Do not expose filesystem paths to clients.
            self.archive_errors[group_id] = {
                "code": "ArchiveWriteFailed", "message": "运行归档未完成，请检查存储空间；运行结果尚未可靠保存。",
            }

    async def _archive(self, group_id):
        for index in range(2):
            record = self.store.get(group_id)
            run_id = record["members"][index]["run_id"]
            snapshot = await self.catalog.wait_terminal(run_id)
            async with self.lock:
                record = self.store.get(group_id)
                member = record["members"][index]
                member["state"] = snapshot.lifecycle
                if snapshot.failure:
                    member["failure"] = {"code": snapshot.failure.code, "message": snapshot.failure.message}
                    if snapshot.failure.code == "CanceledBeforeStart":
                        member["state"] = "Canceled"
                    elif snapshot.failure.code == "Interrupted":
                        member["state"] = "Interrupted"
                if snapshot.result_available:
                    member["result"] = (await self.catalog.get_result(run_id)).model_dump(mode="json")
                self.store.save(record)

    async def view(self, group_id):
        record = self.store.get(group_id)
        if not record["archived"]:
            for member in record["members"]:
                if member["state"] == "Queued":
                    snapshot = await self.catalog.get(member["run_id"])
                    if snapshot.lifecycle == "Running":
                        member["state"] = "Running"
                    # Terminal state is published only when its archive is saved.
        record["state"] = group_state(record["members"])
        if group_id in self.archive_errors:
            record["state"] = "ArchiveFailed"
            record["archive_failure"] = self.archive_errors[group_id]
        return record

    async def cancel_pending(self, group_id):
        async with self.lock:
            record = self.store.get(group_id)
            if record["archived"]:
                raise CatalogError("RunNotPending", "Archived groups cannot be canceled.")
            member = record["members"][1]
            await self.catalog.cancel_pending(member["run_id"])
            member["state"] = "Canceled"
            member["failure"] = {"code": "CanceledBeforeStart", "message": "Canceled before Worker start."}
            self.store.save(record)
        return await self.view(group_id)

    async def shutdown(self):
        # Catalog shutdown must happen first, making every pending/active member terminal.
        if self.tasks:
            await asyncio.gather(*tuple(self.tasks))
