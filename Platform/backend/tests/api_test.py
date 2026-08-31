from __future__ import annotations

import asyncio
import inspect
import json
import os
from pathlib import Path

import httpx
import pytest

from ns3_factory_backend.api import BackendSettings, create_app
from ns3_factory_backend.gateway import (
    WorkerGateway,
    WorkerGatewayResult,
    WorkerProtocolFailure,
)

from helpers import (
    completed,
    event,
    failure_result,
    resource_catalog,
    started,
    success_result,
)


REQUEST = {
    "experiment_id": "acceptance4-experiment",
    "experiment_version": "1",
}


class ControlledGateway:
    def __init__(self, *, failed: bool = False, overall: str = "Pass") -> None:
        self.release = asyncio.Event()
        self.first_event_emitted = asyncio.Event()
        self.failed = failed
        self.overall = overall
        self.command = None

    async def run(self, command, callback):
        self.command = command
        for observation in [started(command.run_id), event(command.run_id, 1)]:
            result = callback(observation)
            if inspect.isawaitable(result):
                await result
        self.first_event_emitted.set()
        await self.release.wait()
        result = callback(event(command.run_id, 2))
        if inspect.isawaitable(result):
            await result
        if self.failed:
            return failure_result(command.run_id)
        return success_result(command.run_id, self.overall)


class BrokenGateway:
    async def run(self, _command, _callback):
        raise WorkerProtocolFailure("malformed worker stdout")


class MismatchedCompletedGateway:
    async def run(self, command, callback):
        terminal = completed(command.run_id, "Pass")
        terminal = terminal.model_copy(
            update={
                "run": terminal.run.model_copy(
                    update={"experiment_id": "other-experiment"}
                )
            }
        )
        return WorkerGatewayResult(
            exit_code=0,
            completed=terminal,
            failed=None,
            stderr_diagnostics="fixture diagnostic",
        )


class SequencedTerminalGateway:
    def __init__(self, outcomes: list[tuple[str, str]]) -> None:
        self.outcomes = iter(outcomes)

    async def run(self, command, _callback):
        kind, overall = next(self.outcomes)
        if kind == "failed":
            return failure_result(command.run_id)
        return success_result(command.run_id, overall)


async def _client(gateway, run_id: str = "run-api-1"):
    app = create_app(
        BackendSettings(Path("/missing/worker"), Path("/missing/assets")),
        gateway=gateway,
        id_factory=lambda: run_id,
        resource_catalog=resource_catalog(),
    )
    return app, httpx.AsyncClient(
        transport=httpx.ASGITransport(app=app), base_url="http://test"
    )


async def _wait_terminal(client: httpx.AsyncClient, run_id: str) -> dict:
    for _ in range(200):
        response = await client.get(f"/runs/{run_id}")
        body = response.json()
        if body["lifecycle"] in {"Completed", "Failed"}:
            return body
        await asyncio.sleep(0.005)
    raise AssertionError("Run did not become terminal")


def _scripted_worker(
    tmp_path: Path,
    lines_before_release: list[str],
    lines_after_release: list[str],
) -> tuple[Path, Path, Path]:
    executable = tmp_path / "fixture-worker-gated"
    ready = tmp_path / "worker.ready"
    release = tmp_path / "worker.release"
    source = [
        "#!/usr/bin/env python3",
        "import pathlib, sys, time",
        "sys.stdin.readline()",
    ]
    source.extend(f"print({line!r}, flush=True)" for line in lines_before_release)
    source.extend(
        [
            "root = pathlib.Path(sys.argv[1])",
            "(root / 'worker.ready').write_text('ready')",
            "while not (root / 'worker.release').exists(): time.sleep(0.005)",
        ]
    )
    source.extend(f"print({line!r}, flush=True)" for line in lines_after_release)
    executable.write_text("\n".join(source) + "\n")
    executable.chmod(0o755)
    return executable, ready, release


async def _wait_path(path: Path) -> None:
    for _ in range(400):
        if path.exists():
            return
        await asyncio.sleep(0.005)
    raise AssertionError(f"Timed out waiting for {path.name}")


async def _disconnect_after_first_sse_event(app, path: str) -> bytes:
    disconnect = asyncio.Event()
    request_sent = False
    bodies: list[bytes] = []

    async def receive():
        nonlocal request_sent
        if not request_sent:
            request_sent = True
            return {"type": "http.request", "body": b"", "more_body": False}
        await disconnect.wait()
        return {"type": "http.disconnect"}

    async def send(message):
        if message["type"] == "http.response.body":
            body = message.get("body", b"")
            bodies.append(body)
            if b"id: 1\n" in body:
                disconnect.set()

    await asyncio.wait_for(
        app(
            {
                "type": "http",
                "asgi": {"version": "3.0", "spec_version": "2.3"},
                "http_version": "1.1",
                "method": "GET",
                "scheme": "http",
                "path": path,
                "raw_path": path.encode(),
                "query_string": b"",
                "root_path": "",
                "headers": [],
                "client": ("test", 1),
                "server": ("test", 80),
            },
            receive,
            send,
        ),
        timeout=5,
    )
    return b"".join(bodies)


def test_post_is_immediate_single_active_and_result_not_ready() -> None:
    async def run() -> None:
        gateway = ControlledGateway()
        _app, client = await _client(gateway)
        async with client:
            created = await client.post("/runs", json=REQUEST)
            assert created.status_code == 201
            assert created.json()["lifecycle"] == "Created"
            await gateway.first_event_emitted.wait()

            running = await client.get("/runs/run-api-1")
            assert running.json()["lifecycle"] == "Running"

            not_ready = await client.get("/runs/run-api-1/results")
            assert not_ready.status_code == 409
            assert not_ready.json()["error"]["code"] == "RunNotReady"

            busy = await client.post("/runs", json=REQUEST)
            assert busy.status_code == 409
            assert busy.json()["error"]["code"] == "BackendBusy"

            gateway.release.set()
            terminal = await _wait_terminal(client, "run-api-1")
            assert terminal["lifecycle"] == "Completed"
            result = await client.get("/runs/run-api-1/results")
            assert result.status_code == 200
            assert result.json()["run_id"] == "run-api-1"
            assert result.json()["projection"]["cycle_count"] == "2"

    asyncio.run(run())


def test_sse_replay_live_boundary_cursor_and_terminal_close() -> None:
    async def run() -> None:
        gateway = ControlledGateway()
        _app, client = await _client(gateway, "run-sse")
        async with client:
            await client.post("/runs", json=REQUEST)
            await gateway.first_event_emitted.wait()

            stream_task = asyncio.create_task(client.get("/runs/run-sse/events"))
            await asyncio.sleep(0)
            gateway.release.set()
            response = await asyncio.wait_for(stream_task, timeout=5)
            assert response.status_code == 200
            assert response.text.count("id: 1\n") == 1
            assert response.text.count("id: 2\n") == 1
            data_lines = [
                json.loads(line.removeprefix("data: "))
                for line in response.text.splitlines()
                if line.startswith("data: ")
            ]
            assert [item["sequence"] for item in data_lines] == ["1", "2"]

            replay = await client.get(
                "/runs/run-sse/events", headers={"Last-Event-ID": "1"}
            )
            assert replay.status_code == 200
            assert "id: 1\n" not in replay.text
            assert replay.text.count("id: 2\n") == 1

            future = await client.get(
                "/runs/run-sse/events", headers={"Last-Event-ID": "3"}
            )
            assert future.status_code == 400
            assert future.json()["error"]["code"] == "InvalidRequest"

            noncanonical = await client.get(
                "/runs/run-sse/events", headers={"Last-Event-ID": "01"}
            )
            assert noncanonical.status_code == 400

    asyncio.run(run())


def test_worker_failed_preserves_events_and_has_no_result() -> None:
    async def run() -> None:
        gateway = ControlledGateway(failed=True)
        _app, client = await _client(gateway, "run-failed")
        async with client:
            await client.post("/runs", json=REQUEST)
            await gateway.first_event_emitted.wait()
            gateway.release.set()
            terminal = await _wait_terminal(client, "run-failed")
            assert terminal["lifecycle"] == "Failed"
            assert terminal["failure"]["code"] == "RunFailed"
            result = await client.get("/runs/run-failed/results")
            assert result.status_code == 409
            assert result.json()["error"]["code"] == "RunFailed"
            events = await client.get("/runs/run-failed/events")
            assert events.text.count("event: run-event") == 2

    asyncio.run(run())


def test_acceptance_verdict_fail_is_completed_not_process_failure() -> None:
    async def run() -> None:
        gateway = ControlledGateway(overall="Fail")
        _app, client = await _client(gateway, "run-verdict-fail")
        async with client:
            await client.post("/runs", json=REQUEST)
            await gateway.first_event_emitted.wait()
            gateway.release.set()
            terminal = await _wait_terminal(client, "run-verdict-fail")
            assert terminal["lifecycle"] == "Completed"
            result = await client.get("/runs/run-verdict-fail/results")
            assert result.json()["acceptance_report"]["overall"] == "Fail"

    asyncio.run(run())


def test_protocol_failure_maps_to_failed_run_and_owned_code() -> None:
    async def run() -> None:
        _app, client = await _client(BrokenGateway(), "run-protocol-fail")
        async with client:
            await client.post("/runs", json=REQUEST)
            terminal = await _wait_terminal(client, "run-protocol-fail")
            assert terminal["lifecycle"] == "Failed"
            assert terminal["failure"] == {
                "code": "WorkerProtocolFailure",
                "message": "malformed worker stdout",
            }

    asyncio.run(run())


def test_identity_mismatch_fails_run_and_withholds_formal_result() -> None:
    async def run() -> None:
        _app, client = await _client(
            MismatchedCompletedGateway(), "run-identity-mismatch"
        )
        async with client:
            created = await client.post("/runs", json=REQUEST)
            assert created.status_code == 201
            terminal = await _wait_terminal(client, "run-identity-mismatch")
            assert terminal["lifecycle"] == "Failed"
            assert terminal["failure"]["code"] == "WorkerProtocolFailure"
            result = await client.get("/runs/run-identity-mismatch/results")
            assert result.status_code == 409
            assert result.json()["error"]["code"] == "RunFailed"

    asyncio.run(run())


def test_health_and_restart_semantics_are_explicit() -> None:
    async def run() -> None:
        first_gateway = ControlledGateway()
        _first_app, first = await _client(first_gateway, "volatile-run")
        async with first:
            health = await first.get("/health")
            assert health.json() == {
                "status": "degraded",
                "backend_alive": True,
                "worker_executable_ready": False,
            }
            await first.post("/runs", json=REQUEST)
            await first_gateway.first_event_emitted.wait()
            first_gateway.release.set()
            await _wait_terminal(first, "volatile-run")

        second_gateway = ControlledGateway()
        _second_app, second = await _client(second_gateway, "new-run")
        async with second:
            missing = await second.get("/runs/volatile-run")
            assert missing.status_code == 404
            assert missing.json()["error"]["code"] == "NotFound"

    asyncio.run(run())


def test_run_and_result_catalogs_are_authoritative_and_creation_ordered() -> None:
    async def run() -> None:
        ids = iter(["z-run", "m-run", "a-run"])
        gateway = SequencedTerminalGateway(
            [("completed", "Pass"), ("failed", "Pass"), ("completed", "Fail")]
        )
        app = create_app(
            BackendSettings(Path("/missing/worker"), Path("/missing/assets")),
            gateway=gateway,
            id_factory=lambda: next(ids),
            resource_catalog=resource_catalog(),
        )
        async with httpx.AsyncClient(
            transport=httpx.ASGITransport(app=app), base_url="http://test"
        ) as client:
            for run_id in ["z-run", "m-run", "a-run"]:
                created = await client.post("/runs", json=REQUEST)
                assert created.status_code == 201
                await _wait_terminal(client, run_id)

            runs = await client.get("/runs")
            assert runs.status_code == 200
            assert [item["run_id"] for item in runs.json()] == [
                "z-run",
                "m-run",
                "a-run",
            ]
            assert [item["catalog_sequence"] for item in runs.json()] == [
                "1",
                "2",
                "3",
            ]
            assert [item["result_available"] for item in runs.json()] == [
                True,
                False,
                True,
            ]
            assert runs.json()[1]["failure"]["code"] == "RunFailed"

            results = await client.get("/results")
            assert results.status_code == 200
            assert [item["run_id"] for item in results.json()] == [
                "z-run",
                "a-run",
            ]
            assert [item["catalog_sequence"] for item in results.json()] == [
                "1",
                "3",
            ]
            assert [item["acceptance_overall"] for item in results.json()] == [
                "Pass",
                "Fail",
            ]
            assert all(item["simulation_duration_ns"] == "1" for item in results.json())
            assert all(item["fusion_result_count"] == "0" for item in results.json())

    asyncio.run(run())


def test_sse_disconnect_is_non_causal_and_reconnect_replays_exact_result(
    tmp_path: Path,
) -> None:
    async def run() -> None:
        run_id = "run-disconnect"
        executable, ready, release = _scripted_worker(
            tmp_path,
            [started(run_id).model_dump_json(), event(run_id, 1).model_dump_json()],
            [event(run_id, 2).model_dump_json(), completed(run_id).model_dump_json()],
        )
        gateway = WorkerGateway(executable, tmp_path)
        app = create_app(
            BackendSettings(executable, tmp_path),
            gateway=gateway,
            id_factory=lambda: run_id,
            resource_catalog=resource_catalog(),
        )
        async with httpx.AsyncClient(
            transport=httpx.ASGITransport(app=app), base_url="http://test"
        ) as client:
            created = await client.post("/runs", json=REQUEST)
            assert created.status_code == 201
            await _wait_path(ready)

            disconnected_prefix = await _disconnect_after_first_sse_event(
                app, f"/runs/{run_id}/events"
            )
            assert disconnected_prefix.count(b"id: 1\n") == 1
            assert gateway.active_process_count == 1

            release.write_text("continue")
            terminal = await _wait_terminal(client, run_id)
            assert terminal["lifecycle"] == "Completed"
            replay = await client.get(f"/runs/{run_id}/events")
            assert replay.text.count("id: 1\n") == 1
            assert replay.text.count("id: 2\n") == 1
            result = await client.get(f"/runs/{run_id}/results")
            assert result.status_code == 200
            assert result.json()["projection"] == completed(
                run_id
            ).result.projection.model_dump(mode="json")
            assert result.json()["experiment_id"] == "acceptance4-experiment"

    asyncio.run(run())


def test_completed_message_is_not_published_until_exit_zero(tmp_path: Path) -> None:
    async def run() -> None:
        run_id = "run-terminal-atomic"
        executable, ready, release = _scripted_worker(
            tmp_path,
            [started(run_id).model_dump_json(), completed(run_id).model_dump_json()],
            [],
        )
        gateway = WorkerGateway(executable, tmp_path)
        app = create_app(
            BackendSettings(executable, tmp_path),
            gateway=gateway,
            id_factory=lambda: run_id,
            resource_catalog=resource_catalog(),
        )
        async with httpx.AsyncClient(
            transport=httpx.ASGITransport(app=app), base_url="http://test"
        ) as client:
            await client.post("/runs", json=REQUEST)
            await _wait_path(ready)
            running = await client.get(f"/runs/{run_id}")
            assert running.json()["lifecycle"] == "Running"
            unavailable = await client.get(f"/runs/{run_id}/results")
            assert unavailable.status_code == 409
            assert unavailable.json()["error"]["code"] == "RunNotReady"

            release.write_text("exit-zero")
            terminal = await _wait_terminal(client, run_id)
            assert terminal["lifecycle"] == "Completed"
            available = await client.get(f"/runs/{run_id}/results")
            assert available.status_code == 200

    asyncio.run(run())


def test_application_shutdown_terminates_and_reaps_worker(tmp_path: Path) -> None:
    async def run() -> None:
        run_id = "run-app-shutdown"
        pid_file = tmp_path / "app-worker.pid"
        executable = tmp_path / "fixture-worker-app-shutdown"
        executable.write_text(
            "#!/usr/bin/env python3\n"
            "import json, os, pathlib, sys, time\n"
            "command = json.loads(sys.stdin.readline())\n"
            "pathlib.Path(sys.argv[1], 'app-worker.pid').write_text(str(os.getpid()))\n"
            "print(json.dumps({'schema_version': 1, 'type': 'WorkerStarted', "
            "'run_id': command['run_id']}), flush=True)\n"
            "while True: time.sleep(0.1)\n"
        )
        executable.chmod(0o755)
        gateway = WorkerGateway(executable, tmp_path)
        app = create_app(
            BackendSettings(executable, tmp_path),
            gateway=gateway,
            id_factory=lambda: run_id,
            resource_catalog=resource_catalog(),
        )

        async with app.router.lifespan_context(app):
            async with httpx.AsyncClient(
                transport=httpx.ASGITransport(app=app), base_url="http://test"
            ) as client:
                created = await client.post("/runs", json=REQUEST)
                assert created.status_code == 201
                await _wait_path(pid_file)
                assert gateway.active_process_count == 1
                pid = int(pid_file.read_text())

        assert gateway.active_process_count == 0
        with pytest.raises(ProcessLookupError):
            os.kill(pid, 0)

    asyncio.run(run())
