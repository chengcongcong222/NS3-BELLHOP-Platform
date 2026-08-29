from __future__ import annotations

import asyncio
import json
import os
from pathlib import Path

import pytest

from ns3_factory_backend.gateway import (
    WorkerGateway,
    WorkerProcessFailure,
    WorkerProtocolFailure,
)
from ns3_factory_backend.wire import (
    Environment,
    Execution,
    Preset,
    StartRunCommand,
)

from helpers import completed, event, failed, started


def _command(run_id: str) -> StartRunCommand:
    return StartRunCommand(
        run_id=run_id,
        preset=Preset(
            scenario_id="acceptance4-scenario",
            experiment_id="acceptance4-experiment",
            definition_version="1",
            acceptance_profile="Acceptance4Node",
        ),
        environment=Environment(asset_id="asset-v1", asset_format_version="1"),
        execution=Execution(
            simulation_cycle_count="2",
            rx_quality_mode="None",
            equivalent_noise_power_db_re_1upa2=45.0,
            deterministic_seed="0",
        ),
    )


def _worker(tmp_path: Path, lines: list[str], exit_code: int = 0) -> Path:
    path = tmp_path / "fixture-worker"
    source = [
        "#!/usr/bin/env python3",
        "import sys",
        "sys.stdin.readline()",
        "sys.stderr.write('human diagnostic only\\n')",
    ]
    source.extend(f"print({line!r}, flush=True)" for line in lines)
    source.append(f"raise SystemExit({exit_code})")
    path.write_text("\n".join(source) + "\n")
    path.chmod(0o755)
    return path


def _json(model) -> str:
    return model.model_dump_json()


def test_gateway_completed_sequence_and_stderr_capture(tmp_path: Path) -> None:
    async def run() -> None:
        run_id = "gateway-success"
        executable = _worker(
            tmp_path,
            [
                _json(started(run_id)),
                _json(event(run_id, 1)),
                _json(completed(run_id)),
            ],
        )
        observations = []
        result = await WorkerGateway(executable, tmp_path).run(
            _command(run_id), observations.append
        )
        assert result.exit_code == 0
        assert result.completed is not None
        assert result.failed is None
        assert [item.type for item in observations] == [
            "WorkerStarted",
            "WorkerRunEvent",
        ]
        assert "human diagnostic only" in result.stderr_diagnostics

    asyncio.run(run())


def test_gateway_accepts_failed_with_nonzero_exit(tmp_path: Path) -> None:
    async def run() -> None:
        run_id = "gateway-failed"
        executable = _worker(
            tmp_path,
            [_json(started(run_id)), _json(failed(run_id))],
            exit_code=3,
        )
        result = await WorkerGateway(executable, tmp_path).run(
            _command(run_id), lambda _message: None
        )
        assert result.completed is None
        assert result.failed is not None
        assert result.failed.category == "Simulation"

    asyncio.run(run())


@pytest.mark.parametrize("mode", ["eof", "malformed", "failed-zero"])
def test_gateway_rejects_protocol_and_exit_inconsistency(
    tmp_path: Path, mode: str
) -> None:
    async def run() -> None:
        run_id = f"gateway-{mode}"
        lines = [_json(started(run_id))]
        exit_code = 0
        expected = WorkerProtocolFailure
        if mode == "malformed":
            lines.append("{not-json")
        elif mode == "failed-zero":
            lines.append(_json(failed(run_id)))
            expected = WorkerProcessFailure
        executable = _worker(tmp_path, lines, exit_code)
        with pytest.raises(expected):
            await WorkerGateway(executable, tmp_path).run(
                _command(run_id), lambda _message: None
            )

    asyncio.run(run())


def test_gateway_reports_child_signal_crash(tmp_path: Path) -> None:
    async def run() -> None:
        run_id = "gateway-crash"
        path = tmp_path / "fixture-worker-crash"
        path.write_text(
            "#!/usr/bin/env python3\n"
            "import json, os, signal, sys\n"
            "command = json.loads(sys.stdin.readline())\n"
            "print(json.dumps({'schema_version': 1, 'type': 'WorkerStarted', "
            "'run_id': command['run_id']}), flush=True)\n"
            "os.kill(os.getpid(), signal.SIGKILL)\n"
        )
        path.chmod(0o755)
        with pytest.raises(WorkerProcessFailure):
            await WorkerGateway(path, tmp_path).run(
                _command(run_id), lambda _message: None
            )

    asyncio.run(run())


def test_gateway_shutdown_terminates_and_reaps_active_child(tmp_path: Path) -> None:
    async def run() -> None:
        pid_file = tmp_path / "worker.pid"
        path = tmp_path / "fixture-worker-waits"
        path.write_text(
            "#!/usr/bin/env python3\n"
            "import json, os, pathlib, sys, time\n"
            "command = json.loads(sys.stdin.readline())\n"
            "pathlib.Path(sys.argv[1], 'worker.pid').write_text(str(os.getpid()))\n"
            "print(json.dumps({'schema_version': 1, 'type': 'WorkerStarted', "
            "'run_id': command['run_id']}), flush=True)\n"
            "while True: time.sleep(0.1)\n"
        )
        path.chmod(0o755)
        gateway = WorkerGateway(path, tmp_path)
        task = asyncio.create_task(
            gateway.run(_command("shutdown-run"), lambda _message: None)
        )
        for _ in range(200):
            if pid_file.exists():
                break
            await asyncio.sleep(0.005)
        assert pid_file.exists()
        pid = int(pid_file.read_text())
        assert gateway.active_process_count == 1

        await gateway.shutdown()
        with pytest.raises(WorkerProcessFailure):
            await task
        assert gateway.active_process_count == 0
        with pytest.raises(ProcessLookupError):
            os.kill(pid, 0)

    asyncio.run(run())
