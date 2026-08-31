#!/usr/bin/env python3
from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
import shutil
import stat
import subprocess
import sys
from pathlib import Path


RELEASE_ID = "P0-S5-03"
TARGET = "linux-x86_64"
ARCHIVE_ROOT = "ns3-bellhop-platform-p0-s5-03-linux-x86_64"
ASSET_ID = "reference-shallow-water-v1"
ASSET_CHECKSUM = "fb64e543f9042c52"


def fail(message: str) -> None:
    print(f"RELEASE_BUILD_FAILED: {message}", file=sys.stderr)
    raise SystemExit(2)


def run(command: list[str], *, cwd: Path | None = None, env: dict[str, str] | None = None) -> str:
    result = subprocess.run(command, cwd=cwd, env=env, text=True, capture_output=True)
    if result.returncode:
        print(result.stdout, file=sys.stderr)
        print(result.stderr, file=sys.stderr)
        fail("command failed: " + " ".join(command))
    return result.stdout.strip()


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def copy_file(source: Path, target: Path, executable: bool = False) -> None:
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, target)
    target.chmod(0o755 if executable else 0o644)


def category(relative: str) -> str:
    prefix = relative.split("/", 1)[0]
    return {
        "bin": "binary",
        "backend": "dependency",
        "frontend": "frontend",
        "assets": "asset",
        "acceptance": "acceptance",
        "docs": "documentation",
        "licenses": "license",
        "scripts": "launcher",
    }.get(prefix, "metadata")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, required=True)
    parser.add_argument("--ns3-prefix", type=Path, required=True)
    args = parser.parse_args()

    repository = Path(__file__).resolve().parents[2]
    platform = repository / "Platform"
    if run(["git", "rev-parse", "--show-toplevel"], cwd=repository) != str(repository):
        fail("builder must run from its Git repository")
    if run(["git", "status", "--porcelain", "--untracked-files=all"], cwd=repository):
        fail("canonical release requires a clean committed source tree")
    source_revision = run(["git", "rev-parse", "HEAD"], cwd=repository)
    source_epoch = int(run(["git", "show", "-s", "--format=%ct", "HEAD"], cwd=repository))
    build_timestamp = dt.datetime.fromtimestamp(source_epoch, dt.UTC).isoformat().replace("+00:00", "Z")
    ns3_library = args.ns3_prefix / "lib/libns3.47-core-default.so"
    if not ns3_library.is_file():
        fail(f"ns-3.47 library unavailable: {ns3_library}")

    output = args.output_dir.resolve()
    work = args.work_dir.resolve()
    release_root = output / ARCHIVE_ROOT
    archive = output / f"{ARCHIVE_ROOT}.tar.gz"
    sidecar = output / f"{ARCHIVE_ROOT}.tar.gz.sha256"
    build = work / "cmake-build"
    for path in (release_root, archive, sidecar, build):
        if path.exists():
            fail(f"refusing to overwrite existing path: {path}")
    output.mkdir(parents=True, exist_ok=True)
    work.mkdir(parents=True, exist_ok=True)

    run([
        "cmake", "-S", str(platform), "-B", str(build), "-G", "Ninja",
        "-DCMAKE_BUILD_TYPE=Release", "-DBUILD_TESTING=ON", "-DPLATFORM_ENABLE_NS3=ON",
        "-DCMAKE_SKIP_RPATH=TRUE",
        f"-DCMAKE_PREFIX_PATH={args.ns3_prefix}",
        f"-DCMAKE_CXX_FLAGS=-ffile-prefix-map={repository}=.",
    ])
    run(["cmake", "--build", str(build)])
    test_env = os.environ.copy()
    test_env["LD_LIBRARY_PATH"] = str(args.ns3_prefix / "lib")
    run(["ctest", "--test-dir", str(build), "--output-on-failure"], env=test_env)

    npm = platform / "frontend/scripts/npm.sh"
    frontend_env = os.environ.copy()
    frontend_env["VITE_API_BASE_URL"] = "http://127.0.0.1:8000"
    run([str(npm), "ci", "--offline", "--cache", str(platform / "third_party/npm_cache")], cwd=platform / "frontend", env=frontend_env)
    run([str(npm), "run", "build"], cwd=platform / "frontend", env=frontend_env)

    copy_file(build / "worker/platform_sim_worker", release_root / "bin/platform_sim_worker", True)
    copy_file(build / "worker/platform_resource_catalog_adapter", release_root / "bin/platform_resource_catalog_adapter", True)
    copy_file(platform / "backend/requirements.lock", release_root / "backend/requirements.lock")
    shutil.copytree(platform / "backend/src", release_root / "backend/src", ignore=shutil.ignore_patterns("__pycache__", "*.pyc"))
    shutil.copytree(platform / "third_party/python_wheels/wheels", release_root / "backend/wheels")
    shutil.copytree(platform / "frontend/dist", release_root / "frontend")
    copy_file(platform / "acceptance/acceptance4_baseline_v1.json", release_root / "acceptance/acceptance4_baseline_v1.json")

    asset_repository = release_root / "assets/environment-repository"
    asset_repository.mkdir(parents=True)
    run([
        str(build / "environment/platform_reference_environment_builder"),
        str(platform / "environment/assets/reference_shallow_water_v1"),
        str(asset_repository),
    ])
    copy_file(platform / "environment/assets/reference_shallow_water_v1/golden_metadata.json", release_root / "assets/reference_environment/golden_metadata.json")
    copy_file(platform / "environment/assets/reference_shallow_water_v1/validation_report.json", release_root / "assets/reference_environment/validation_report.json")
    copy_file(platform / "environment/assets/reference_shallow_water_v1/source/source_manifest.json", release_root / "assets/reference_environment/source_manifest.json")
    copy_file(platform / "environment/assets/reference_shallow_water_v1/source/PROVENANCE.md", release_root / "assets/reference_environment/PROVENANCE.md")

    for name in ("release.sh", "release_preflight.py", "verify_checksums.py", "frontend_server.py"):
        copy_file(platform / "release/runtime" / name, release_root / ("release.sh" if name == "release.sh" else f"scripts/{name}"), True)
    for name in ("acceptance_runbook.md", "acceptance_handoff.md"):
        copy_file(platform / "docs/release" / name, release_root / "docs" / name)
    readme = (platform / "release/README.template.md").read_text(encoding="utf-8")
    readme = readme.replace("@SOURCE_REVISION@", source_revision)
    (release_root / "README.md").write_text(readme, encoding="utf-8")

    copy_file(platform / "third_party/THIRD_PARTY_NOTICES.md", release_root / "licenses/THIRD_PARTY_NOTICES.md")
    copy_file(platform / "third_party/nlohmann_json/LICENSE.MIT", release_root / "licenses/nlohmann-json-MIT.txt")
    copy_file(platform / "third_party/nodejs/LICENSE", release_root / "licenses/nodejs-build-toolchain-LICENSE.txt")
    copy_file(platform / "third_party/python_wheels/PROVENANCE.md", release_root / "licenses/python-wheelhouse-PROVENANCE.md")
    copy_file(platform / "third_party/npm_dependencies.json", release_root / "licenses/frontend-dependencies.json")
    copy_file(platform / "release/NS3_RUNTIME_NOTICE.md", release_root / "licenses/ns3-runtime-prerequisite.md")

    runtime_env = os.environ.copy()
    runtime_env["LD_LIBRARY_PATH"] = str(args.ns3_prefix / "lib")
    binaries: dict[str, dict[str, object]] = {}
    for name in ("platform_sim_worker", "platform_resource_catalog_adapter"):
        path = release_root / "bin" / name
        needed = []
        for line in run(["readelf", "-d", str(path)]).splitlines():
            if "(NEEDED)" in line:
                needed.append(line.split("[", 1)[1].split("]", 1)[0])
        run(["ldd", str(path)], env=runtime_env)
        binaries[name] = {"sha256": sha256(path), "elf_needed": sorted(needed)}
    (release_root / "binary_dependencies.json").write_text(json.dumps({
        "schema_version": 1,
        "ns3_decision": "external-prefix",
        "required_ns3_version": "3.47",
        "required_environment_variable": "PLATFORM_NS3_PREFIX",
        "binaries": binaries,
    }, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    frontend_files = sorted((release_root / "frontend").rglob("*"))
    frontend_identity = hashlib.sha256("".join(
        f"{path.relative_to(release_root).as_posix()}:{sha256(path)}\n"
        for path in frontend_files if path.is_file()
    ).encode()).hexdigest()
    manifest = {
        "schema_version": 1,
        "release_id": RELEASE_ID,
        "source_revision": source_revision,
        "build_timestamp_utc": build_timestamp,
        "build_target": TARGET,
        "cxx_standard": "23",
        "ns3": {"version": "3.47", "runtime_supply": "external-prefix", "environment_variable": "PLATFORM_NS3_PREFIX"},
        "python": {"version": "3.12", "supply": "hash-locked-offline-wheelhouse"},
        "node": {"version": "24.20.0", "npm_version": "11.19.0", "role": "offline-build-only-not-runtime"},
        "interfaces": {"worker_protocol_schema": "1", "backend_api_schema": "1", "acceptance_evidence_schema": "1"},
        "reference_environment": {"asset_id": ASSET_ID, "checksum": {"algorithm": "FNV1A64", "value": ASSET_CHECKSUM}},
        "acceptance_baseline": {"id": "Acceptance4Node", "version": "1"},
        "frontend_build_identity_sha256": frontend_identity,
        "third_party_notices_sha256": sha256(release_root / "licenses/THIRD_PARTY_NOTICES.md"),
        "archive": {"format": "tar.gz", "root_directory": ARCHIVE_ROOT, "deterministic_mtime_epoch": source_epoch},
    }
    (release_root / "MANIFEST.json").write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    inventory_exclusions = {"release_inventory.json", "SHA256SUMS"}
    inventory = []
    for path in sorted(release_root.rglob("*")):
        if path.is_file():
            relative = path.relative_to(release_root).as_posix()
            if relative not in inventory_exclusions:
                inventory.append({"path": relative, "size": path.stat().st_size, "sha256": sha256(path), "category": category(relative)})
    (release_root / "release_inventory.json").write_text(json.dumps({
        "schema_version": 1,
        "excluded_self_referential_files": sorted(inventory_exclusions),
        "files": inventory,
    }, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    checksums = []
    for path in sorted(release_root.rglob("*")):
        if path.is_file() and path.name != "SHA256SUMS":
            checksums.append(f"{sha256(path)}  {path.relative_to(release_root).as_posix()}")
    (release_root / "SHA256SUMS").write_text("\n".join(checksums) + "\n", encoding="utf-8")
    for path in release_root.rglob("*"):
        if path.is_file() and not path.stat().st_mode & stat.S_IXUSR:
            path.chmod(0o644)
    release_root.chmod(0o755)

    run([sys.executable, str(platform / "release/tests/release_bundle_test.py"), str(release_root), source_revision])

    tar_process = subprocess.Popen([
        "tar", "--sort=name", f"--mtime=@{source_epoch}", "--owner=0", "--group=0", "--numeric-owner",
        "--pax-option=delete=atime,delete=ctime", "-cf", "-", ARCHIVE_ROOT,
    ], cwd=output, stdout=subprocess.PIPE)
    with archive.open("wb") as stream:
        gzip_process = subprocess.run(["gzip", "-n", "-9"], stdin=tar_process.stdout, stdout=stream)
    assert tar_process.stdout is not None
    tar_process.stdout.close()
    if tar_process.wait() or gzip_process.returncode:
        fail("deterministic archive command failed")
    archive_digest = sha256(archive)
    sidecar.write_text(f"{archive_digest}  {archive.name}\n", encoding="utf-8")
    print(json.dumps({"release_root": str(release_root), "archive": str(archive), "archive_sha256": archive_digest}, sort_keys=True))


if __name__ == "__main__":
    main()
