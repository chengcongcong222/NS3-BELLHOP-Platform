from __future__ import annotations

import asyncio
import uuid
from collections.abc import AsyncIterator, Callable
from dataclasses import dataclass, field
from typing import Protocol

from .gateway import GatewayFailure, ObservationCallback, WorkerGatewayResult
from .wire import (
    RunRecord,
    RunResult,
    StartRunCommand,
    WorkerRunEvent,
    WorkerStarted,
)


class Gateway(Protocol):
    async def run(
        self,
        command: StartRunCommand,
        on_observation: ObservationCallback,
    ) -> WorkerGatewayResult: ...

    async def shutdown(self) -> None: ...


@dataclass(frozen=True)
class BackendFailure:
    code: str
    message: str


@dataclass(frozen=True)
class RunSnapshot:
    run_id: str
    lifecycle: str
    event_stream_complete: bool | None
    failure: BackendFailure | None
    terminal_run: RunRecord | None


@dataclass
class _RunEntry:
    command: StartRunCommand
    lifecycle: str = "Created"
    events: list[WorkerRunEvent] = field(default_factory=list)
    result: RunResult | None = None
    terminal_run: RunRecord | None = None
    failure: BackendFailure | None = None
    stderr_diagnostics: str = ""
    task: asyncio.Task[None] | None = None


class CatalogError(RuntimeError):
    def __init__(self, code: str, message: str) -> None:
        super().__init__(message)
        self.code = code


class InMemoryRunCatalog:
    """Single-active-run P0 catalog; all state is process-local and volatile."""

    def __init__(
        self,
        gateway: Gateway,
        id_factory: Callable[[], str] | None = None,
    ) -> None:
        self._gateway = gateway
        self._id_factory = id_factory or (
            lambda: f"run-{uuid.uuid4().hex}"
        )
        self._entries: dict[str, _RunEntry] = {}
        self._active_run_id: str | None = None
        self._condition = asyncio.Condition()

    async def create(
        self,
        command_factory: Callable[[str], StartRunCommand],
    ) -> RunSnapshot:
        async with self._condition:
            if self._active_run_id is not None:
                raise CatalogError(
                    "BackendBusy", "The P0 backend already has an active Run."
                )
            run_id = self._id_factory()
            if run_id in self._entries:
                raise CatalogError("AlreadyExists", "Generated RunId already exists.")
            command = command_factory(run_id)
            entry = _RunEntry(command=command)
            self._entries[run_id] = entry
            self._active_run_id = run_id
            created = self._snapshot(entry)
            entry.task = asyncio.create_task(self._execute(run_id, entry))
            return created

    async def get(self, run_id: str) -> RunSnapshot:
        async with self._condition:
            return self._snapshot(self._find(run_id))

    async def get_result(self, run_id: str) -> RunResult:
        async with self._condition:
            entry = self._find(run_id)
            if entry.lifecycle in {"Created", "Running"}:
                raise CatalogError(
                    "RunNotReady", "The Run has not completed yet."
                )
            if entry.lifecycle == "Failed":
                raise CatalogError("RunFailed", "The Run did not produce a result.")
            assert entry.result is not None
            return entry.result

    async def shutdown(self) -> None:
        """Stop accepting work and release every active worker owned by the app."""

        shutdown = getattr(self._gateway, "shutdown", None)
        if shutdown is not None:
            await shutdown()
        async with self._condition:
            tasks = [
                entry.task
                for entry in self._entries.values()
                if entry.task is not None and not entry.task.done()
            ]
        if not tasks:
            return
        done, pending = await asyncio.wait(tasks, timeout=2.0)
        for task in pending:
            task.cancel()
        if pending:
            await asyncio.gather(*pending, return_exceptions=True)
        for task in done:
            task.exception()

    async def validate_cursor(self, run_id: str, cursor: int) -> None:
        async with self._condition:
            entry = self._find(run_id)
            latest = self._latest(entry)
            if cursor > latest:
                raise CatalogError(
                    "InvalidRequest",
                    "Last-Event-ID is beyond the latest RunEventSequence.",
                )

    async def stream_after(
        self, run_id: str, cursor: int
    ) -> AsyncIterator[WorkerRunEvent]:
        current = cursor
        while True:
            async with self._condition:
                entry = self._find(run_id)
                latest = self._latest(entry)
                if current > latest:
                    raise CatalogError(
                        "InvalidRequest",
                        "Run event cursor advanced beyond the latest sequence.",
                    )
                backlog = [
                    event for event in entry.events if int(event.sequence) > current
                ]
                terminal = entry.lifecycle in {"Completed", "Failed"}
                if not backlog and terminal:
                    return
                if not backlog:
                    await self._condition.wait()
                    continue
            for event in backlog:
                current = int(event.sequence)
                yield event

    async def _execute(self, run_id: str, entry: _RunEntry) -> None:
        async with self._condition:
            entry.lifecycle = "Running"
            self._condition.notify_all()

        async def observe(message: WorkerStarted | WorkerRunEvent) -> None:
            if isinstance(message, WorkerRunEvent):
                async with self._condition:
                    expected = len(entry.events) + 1
                    if int(message.sequence) != expected:
                        raise CatalogError(
                            "WorkerProtocolFailure",
                            "Worker event sequence is not contiguous.",
                        )
                    entry.events.append(message)
                    self._condition.notify_all()

        try:
            outcome = await self._gateway.run(entry.command, observe)
            async with self._condition:
                entry.stderr_diagnostics = outcome.stderr_diagnostics
                if outcome.completed is not None:
                    entry.lifecycle = "Completed"
                    entry.terminal_run = outcome.completed.run
                    entry.result = outcome.completed.result
                else:
                    assert outcome.failed is not None
                    entry.lifecycle = "Failed"
                    entry.terminal_run = outcome.failed.run
                    entry.failure = BackendFailure(
                        "RunFailed", outcome.failed.error.message
                    )
                self._condition.notify_all()
        except GatewayFailure as error:
            async with self._condition:
                entry.lifecycle = "Failed"
                entry.failure = BackendFailure(error.code, str(error))
                entry.stderr_diagnostics = getattr(
                    self._gateway, "last_stderr_diagnostics", ""
                )
                self._condition.notify_all()
        except Exception:
            async with self._condition:
                entry.lifecycle = "Failed"
                entry.failure = BackendFailure(
                    "WorkerProcessFailure",
                    "The backend could not complete the worker process.",
                )
                self._condition.notify_all()
        finally:
            async with self._condition:
                if self._active_run_id == run_id:
                    self._active_run_id = None
                self._condition.notify_all()

    def _find(self, run_id: str) -> _RunEntry:
        entry = self._entries.get(run_id)
        if entry is None:
            raise CatalogError("NotFound", "Run was not found.")
        return entry

    @staticmethod
    def _latest(entry: _RunEntry) -> int:
        return int(entry.events[-1].sequence) if entry.events else 0

    @staticmethod
    def _snapshot(entry: _RunEntry) -> RunSnapshot:
        complete = (
            entry.terminal_run.event_stream_complete
            if entry.terminal_run is not None
            else None
        )
        return RunSnapshot(
            run_id=entry.command.run_id,
            lifecycle=entry.lifecycle,
            event_stream_complete=complete,
            failure=entry.failure,
            terminal_run=entry.terminal_run,
        )
