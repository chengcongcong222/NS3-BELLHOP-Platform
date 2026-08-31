#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import socket
import subprocess
import sys
from pathlib import Path


def fail(code: str, message: str) -> None:
    print(f"PREFLIGHT_{code}: {message}", file=sys.stderr)
    raise SystemExit(2)


def run(command: list[str], code: str) -> subprocess.CompletedProcess[bytes]:
    try:
        result = subprocess.run(
            command, check=False, capture_output=True, timeout=30
        )
    except (OSError, subprocess.SubprocessError) as error:
        fail(code, str(error))
    if result.returncode != 0:
        detail = result.stderr.decode(errors="replace").strip()
        fail(code, detail or "command returned non-zero")
    return result


def executable(path: Path, code: str) -> None:
    if not path.is_file() or not path.stat().st_mode & 0o111:
        fail(code, f"required executable is unavailable: {path}")


def port_available(host: str, port: int) -> None:
    probe = socket.socket()
    try:
        probe.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        probe.bind((host, port))
    except OSError as error:
        fail("PORT_IN_USE", f"{host}:{port}: {error}")
    finally:
        probe.close()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--platform-root", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--environment-repository", type=Path, required=True)
    parser.add_argument(
        "--acceptance-environment-asset-id",
        default="reference-shallow-water-v1",
    )
    parser.add_argument("--backend-port", type=int, default=8000)
    parser.add_argument("--frontend-port", type=int, default=4173)
    args = parser.parse_args()

    root = args.platform_root.resolve()
    build = args.build_dir.resolve()
    repository = args.environment_repository.resolve()
    worker = build / "worker" / "platform_sim_worker"
    adapter = build / "worker" / "platform_resource_catalog_adapter"
    backend_python = build / "backend" / "venv" / "bin" / "python"
    npm = root / "frontend" / "scripts" / "npm.sh"
    frontend_dist = root / "frontend" / "dist"
    baseline = root / "acceptance" / "acceptance4_baseline_v1.json"
    lock = root / "backend" / "requirements.lock"
    node_archive = root / "third_party" / "nodejs" / "node-v24.20.0-linux-x64.tar.xz"
    node_runtime = root / "frontend" / ".tools" / "node-v24.20.0-linux-x64" / "bin" / "node"
    npm_cache = root / "third_party" / "npm_cache"

    for path, code in [
        (worker, "WORKER_UNAVAILABLE"),
        (adapter, "RESOURCE_ADAPTER_UNAVAILABLE"),
        (backend_python, "PYTHON_ENV_UNAVAILABLE"),
        (npm, "NPM_WRAPPER_UNAVAILABLE"),
    ]:
        executable(path, code)
    for path, code in [
        (frontend_dist / "index.html", "FRONTEND_BUILD_UNAVAILABLE"),
        (baseline, "ACCEPTANCE_BASELINE_UNAVAILABLE"),
        (lock, "PYTHON_LOCK_UNAVAILABLE"),
        (node_archive, "NODE_OFFLINE_SUPPLY_UNAVAILABLE"),
    ]:
        if not path.is_file():
            fail(code, f"required file is unavailable: {path}")
    if not npm_cache.is_dir():
        fail("NPM_OFFLINE_SUPPLY_UNAVAILABLE", str(npm_cache))
    if not repository.is_dir():
        fail("ENVIRONMENT_REPOSITORY_UNAVAILABLE", str(repository))

    version = run([str(backend_python), "--version"], "PYTHON_VERSION").stdout
    if not version.decode().startswith("Python 3.12."):
        fail("PYTHON_VERSION", version.decode().strip())
    run([str(backend_python), "-m", "pip", "check"], "PYTHON_LOCK_MISMATCH")
    expected = {}
    for line in lock.read_text().splitlines():
        if not line or line.startswith("#"):
            continue
        name, remainder = line.split("==", 1)
        expected[name.lower().replace("_", "-")] = remainder.split()[0]
    frozen = run(
        [str(backend_python), "-m", "pip", "freeze", "--all"],
        "PYTHON_LOCK_MISMATCH",
    ).stdout.decode()
    installed = {}
    for line in frozen.splitlines():
        if "==" in line:
            name, version_text = line.split("==", 1)
            installed[name.lower().replace("_", "-")] = version_text
    mismatch = {
        name: (version_text, installed.get(name))
        for name, version_text in expected.items()
        if installed.get(name) != version_text
    }
    if mismatch:
        fail("PYTHON_LOCK_MISMATCH", repr(mismatch))
    npm_version = run([str(npm), "--version"], "NPM_VERSION").stdout.decode().strip()
    if npm_version != "11.19.0":
        fail("NPM_VERSION", f"expected 11.19.0, got {npm_version}")
    executable(node_runtime, "NODE_RUNTIME_UNAVAILABLE")
    node_version = run([str(node_runtime), "--version"], "NODE_VERSION").stdout.decode().strip()
    if node_version != "v24.20.0":
        fail("NODE_VERSION", f"expected v24.20.0, got {node_version}")

    catalog = run(
        [str(adapter), str(repository), args.acceptance_environment_asset_id],
        "ENVIRONMENT_VALIDATION",
    )
    try:
        document = json.loads(catalog.stdout)
        experiment_ids = {item["experiment_id"] for item in document["experiments"]}
    except (KeyError, TypeError, json.JSONDecodeError) as error:
        fail("RESOURCE_CATALOG_SCHEMA", str(error))
    if "acceptance4-experiment" not in experiment_ids:
        fail("ACCEPTANCE_RESOURCE_MISSING", "acceptance4-experiment")

    port_available("127.0.0.1", args.backend_port)
    port_available("127.0.0.1", args.frontend_port)
    print("PREFLIGHT_OK")


if __name__ == "__main__":
    main()
