from __future__ import annotations

import asyncio
import inspect
from dataclasses import dataclass
from pathlib import Path
from typing import Awaitable, Callable

from pydantic import ValidationError

from .wire import (
    MAXIMUM_OUTPUT_MESSAGE_BYTES,
    StartRunCommand,
    WorkerCompleted,
    WorkerFailed,
    WorkerRunEvent,
    WorkerStarted,
    encode_start_run_command,
    parse_worker_message,
)


class GatewayFailure(RuntimeError):
    code: str

    def __init__(self, message: str) -> None:
        super().__init__(message)


class WorkerProtocolFailure(GatewayFailure):
    code = "WorkerProtocolFailure"


class WorkerProcessFailure(GatewayFailure):
    code = "WorkerProcessFailure"


GatewayObservation = WorkerStarted | WorkerRunEvent
ObservationCallback = Callable[
    [GatewayObservation], Awaitable[None] | None
]


@dataclass(frozen=True)
class WorkerGatewayResult:
    exit_code: int
    completed: WorkerCompleted | None
    failed: WorkerFailed | None
    stderr_diagnostics: str


class WorkerGateway:
    """One-use-per-call non-shell owner for platform_sim_worker."""

    def __init__(
        self,
        executable: Path,
        environment_repository_root: Path,
    ) -> None:
        self._executable = executable
        self._environment_repository_root = environment_repository_root
        self.last_stderr_diagnostics = ""
        self._active_processes: set[asyncio.subprocess.Process] = set()
        self._shutting_down = False

    @property
    def active_process_count(self) -> int:
        return len(self._active_processes)

    async def shutdown(self) -> None:
        """Terminate and reap every child still owned by this gateway."""

        self._shutting_down = True
        processes = tuple(self._active_processes)
        if processes:
            await asyncio.gather(
                *(self._terminate_and_reap(process) for process in processes)
            )

    async def run(
        self,
        command: StartRunCommand,
        on_observation: ObservationCallback,
    ) -> WorkerGatewayResult:
        if self._shutting_down:
            raise WorkerProcessFailure("worker gateway is shutting down")
        process: asyncio.subprocess.Process | None = None
        stderr_task: asyncio.Task[str] | None = None
        try:
            try:
                process = await asyncio.create_subprocess_exec(
                    str(self._executable),
                    str(self._environment_repository_root),
                    stdin=asyncio.subprocess.PIPE,
                    stdout=asyncio.subprocess.PIPE,
                    stderr=asyncio.subprocess.PIPE,
                    limit=MAXIMUM_OUTPUT_MESSAGE_BYTES + 2,
                )
                self._active_processes.add(process)
                if self._shutting_down:
                    await self._terminate_and_reap(process)
                    raise WorkerProcessFailure("worker gateway is shutting down")
            except OSError as error:
                raise WorkerProcessFailure(
                    "platform_sim_worker could not be started"
                ) from error

            assert process.stdin is not None
            assert process.stdout is not None
            assert process.stderr is not None
            stderr_task = asyncio.create_task(self._capture_stderr(process.stderr))

            frame = encode_start_run_command(command) + b"\n"
            try:
                process.stdin.write(frame)
                await process.stdin.drain()
                process.stdin.close()
                await process.stdin.wait_closed()
            except (BrokenPipeError, ConnectionResetError) as error:
                raise WorkerProcessFailure(
                    "platform_sim_worker rejected its command pipe"
                ) from error

            saw_started = False
            expected_sequence = 1
            completed: WorkerCompleted | None = None
            failed: WorkerFailed | None = None

            while True:
                try:
                    line = await process.stdout.readline()
                except (ValueError, asyncio.LimitOverrunError) as error:
                    raise WorkerProtocolFailure(
                        "worker stdout frame exceeds the 4 MiB limit"
                    ) from error
                if not line:
                    break
                if not line.endswith(b"\n"):
                    raise WorkerProtocolFailure(
                        "worker stdout ended in a partial JSON frame"
                    )
                line = line[:-1]
                if line.endswith(b"\r"):
                    line = line[:-1]
                if completed is not None or failed is not None:
                    raise WorkerProtocolFailure(
                        "worker emitted data after its terminal message"
                    )
                try:
                    message = parse_worker_message(line)
                except (ValidationError, ValueError) as error:
                    raise WorkerProtocolFailure(
                        "worker stdout contains an invalid schema v1 message"
                    ) from error

                if isinstance(message, WorkerStarted):
                    if saw_started or message.run_id != command.run_id:
                        raise WorkerProtocolFailure(
                            "WorkerStarted is duplicate or has the wrong RunId"
                        )
                    saw_started = True
                    await self._observe(on_observation, message)
                elif isinstance(message, WorkerRunEvent):
                    if (
                        not saw_started
                        or message.run_id != command.run_id
                        or int(message.sequence) != expected_sequence
                    ):
                        raise WorkerProtocolFailure(
                            "WorkerRunEvent identity or sequence is invalid"
                        )
                    expected_sequence += 1
                    await self._observe(on_observation, message)
                elif isinstance(message, WorkerCompleted):
                    if (
                        not saw_started
                        or message.run.run_id != command.run_id
                        or message.result.run_id != command.run_id
                    ):
                        raise WorkerProtocolFailure(
                            "WorkerCompleted identity is invalid"
                        )
                    completed = message
                else:
                    if (
                        message.category != "Protocol" and not saw_started
                    ) or (
                        message.run_id is not None
                        and message.run_id != command.run_id
                    ):
                        raise WorkerProtocolFailure("WorkerFailed identity is invalid")
                    failed = message

            exit_code = await process.wait()
            diagnostics = await stderr_task
            stderr_task = None
            self.last_stderr_diagnostics = diagnostics
            if completed is None and failed is None:
                if exit_code != 0:
                    raise WorkerProcessFailure(
                        "platform_sim_worker exited abnormally before a terminal message"
                    )
                raise WorkerProtocolFailure(
                    "worker reached EOF before a terminal message"
                )
            if (completed is not None and exit_code != 0) or (
                failed is not None and exit_code == 0
            ):
                raise WorkerProcessFailure(
                    "worker terminal message and process exit status disagree"
                )
            return WorkerGatewayResult(
                exit_code=exit_code,
                completed=completed,
                failed=failed,
                stderr_diagnostics=diagnostics,
            )
        except GatewayFailure:
            if process is not None and process.returncode is None:
                process.kill()
                await process.wait()
            if stderr_task is not None:
                self.last_stderr_diagnostics = await stderr_task
            raise
        except asyncio.CancelledError:
            if process is not None:
                await self._terminate_and_reap(process)
            if stderr_task is not None:
                self.last_stderr_diagnostics = await stderr_task
            raise
        except Exception as error:
            if process is not None and process.returncode is None:
                process.kill()
                await process.wait()
            if stderr_task is not None:
                self.last_stderr_diagnostics = await stderr_task
            raise WorkerProcessFailure(
                "worker gateway failed while owning the child process"
            ) from error
        finally:
            if process is not None:
                self._active_processes.discard(process)

    @staticmethod
    async def _observe(
        callback: ObservationCallback,
        message: GatewayObservation,
    ) -> None:
        observed = callback(message)
        if inspect.isawaitable(observed):
            await observed

    @staticmethod
    async def _capture_stderr(reader: asyncio.StreamReader) -> str:
        maximum = 1 << 20
        captured = bytearray()
        while True:
            chunk = await reader.read(65536)
            if not chunk:
                break
            remaining = maximum - len(captured)
            if remaining > 0:
                captured.extend(chunk[:remaining])
        return captured.decode("utf-8", errors="replace")

    @staticmethod
    async def _terminate_and_reap(process: asyncio.subprocess.Process) -> None:
        if process.returncode is not None:
            await process.wait()
            return
        try:
            process.terminate()
        except ProcessLookupError:
            await process.wait()
            return
        try:
            await asyncio.wait_for(process.wait(), timeout=2.0)
        except TimeoutError:
            try:
                process.kill()
            except ProcessLookupError:
                pass
            await process.wait()
