#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import platform
import socket
import subprocess
import sys
from pathlib import Path


def fail(code: str, detail: str) -> None:
    print(f"RELEASE_PREFLIGHT_{code}: {detail}", file=sys.stderr)
    raise SystemExit(2)


def run(command: list[str], code: str, env: dict[str, str] | None = None) -> bytes:
    try:
        result = subprocess.run(command, capture_output=True, timeout=30, env=env)
    except (OSError, subprocess.SubprocessError) as error:
        fail(code, str(error))
    if result.returncode:
        fail(code, result.stderr.decode(errors="replace").strip() or "non-zero exit")
    return result.stdout


def port_available(port: int) -> None:
    probe = socket.socket()
    try:
        probe.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        probe.bind(("127.0.0.1", port))
    except OSError as error:
        fail("PORT_IN_USE", f"127.0.0.1:{port}: {error}")
    finally:
        probe.close()


def validate_platform(system_name: str, machine_name: str) -> None:
    if system_name != "Linux" or machine_name != "x86_64":
        fail("UNSUPPORTED_PLATFORM", f"requires Linux x86_64, got {system_name} {machine_name}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--backend-port", type=int, default=8000)
    parser.add_argument("--frontend-port", type=int, default=4173)
    parser.add_argument("--skip-ports", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    manifest = json.loads((root / "MANIFEST.json").read_text(encoding="utf-8"))
    validate_platform(platform.system(), platform.machine())
    if manifest["release_id"] != "P0-S5-05" or manifest["build_target"] != "linux-x86_64":
        fail("MANIFEST_IDENTITY", "unexpected release identity")

    ns3_prefix_text = os.environ.get("PLATFORM_NS3_PREFIX", "")
    if not ns3_prefix_text:
        fail("NS3_PREFIX_REQUIRED", "set PLATFORM_NS3_PREFIX to an ns-3.47 install prefix")
    ns3_prefix = Path(ns3_prefix_text)
    ns3_library = ns3_prefix / "lib/libns3.47-core-default.so"
    if not ns3_library.is_file():
        fail("NS3_347_UNAVAILABLE", str(ns3_library))

    python = root / ".runtime/venv/bin/python"
    if not python.is_file():
        fail("PYTHON_ENV_UNAVAILABLE", "run ./release.sh prepare")
    if not run([str(python), "--version"], "PYTHON_VERSION").decode().startswith("Python 3.12."):
        fail("PYTHON_VERSION", "CPython 3.12 is required")
    run([str(python), "-m", "pip", "check"], "PYTHON_LOCK")

    worker = root / "bin/platform_sim_worker"
    adapter = root / "bin/platform_resource_catalog_adapter"
    for executable in (worker, adapter, root / "release.sh"):
        if not executable.is_file() or not executable.stat().st_mode & 0o111:
            fail("EXECUTABLE", str(executable))
    environment = os.environ.copy()
    environment["LD_LIBRARY_PATH"] = str(ns3_prefix / "lib")
    dependencies = run(["ldd", str(worker)], "WORKER_DEPENDENCIES", environment).decode()
    if "not found" in dependencies or "libns3.47-core-default.so" not in dependencies:
        fail("WORKER_DEPENDENCIES", dependencies.strip())
    catalog = run(
        [str(adapter), str(root / "assets/environment-repository"), "reference-shallow-water-v1"],
        "REFERENCE_ASSET",
        environment,
    )
    resource = json.loads(catalog)["environments"][0]
    if resource["environment_asset_id"] != manifest["reference_environment"]["asset_id"]:
        fail("REFERENCE_ASSET_ID", resource["environment_asset_id"])
    if resource["checksum"]["value"] != manifest["reference_environment"]["checksum"]["value"]:
        fail("REFERENCE_ASSET_CHECKSUM", resource["checksum"]["value"])
    if not (root / "frontend/index.html").is_file():
        fail("FRONTEND", "production frontend is missing")
    if not args.skip_ports:
        port_available(args.backend_port)
        port_available(args.frontend_port)
    print("RELEASE_PREFLIGHT_OK")


if __name__ == "__main__":
    main()
