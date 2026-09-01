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


RELEASE_ID = "P0-S5-05"
TARGET = "linux-x86_64"
ARCHIVE_ROOT = "ns3-bellhop-platform-p0-s5-05-linux-x86_64"
HANDOFF_ROOT = "acceptance-handoff-p0-s5-05"
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


def materialize_text(source: Path, target: Path, replacements: dict[str, str]) -> None:
    text = source.read_text(encoding="utf-8")
    for marker, value in replacements.items():
        text = text.replace(marker, value)
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(text, encoding="utf-8")
    target.chmod(0o644)


def deterministic_archive(root: Path, archive: Path, epoch: int) -> str:
    tar_process = subprocess.Popen([
        "tar", "--sort=name", f"--mtime=@{epoch}", "--owner=0", "--group=0", "--numeric-owner",
        "--pax-option=delete=atime,delete=ctime", "-cf", "-", root.name,
    ], cwd=root.parent, stdout=subprocess.PIPE)
    assert tar_process.stdout is not None
    with archive.open("wb") as stream:
        gzip_process = subprocess.run(["gzip", "-n", "-9"], stdin=tar_process.stdout, stdout=stream)
    tar_process.stdout.close()
    if tar_process.wait() or gzip_process.returncode:
        fail(f"deterministic archive command failed: {archive.name}")
    return sha256(archive)


def file_metrics(root: Path) -> tuple[int, int]:
    files = [path for path in root.rglob("*") if path.is_file()]
    return len(files), sum(path.stat().st_size for path in files)


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
    handoff_root = output / HANDOFF_ROOT
    handoff_archive = output / f"{HANDOFF_ROOT}.tar.gz"
    handoff_sidecar = output / f"{HANDOFF_ROOT}.tar.gz.sha256"
    build = work / "cmake-build"
    for path in (
        release_root, archive, sidecar, handoff_root, handoff_archive,
        handoff_sidecar, build,
    ):
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
    runtime_replacements = {
        "@SOURCE_REVISION@": source_revision,
        "@ARCHIVE_SHA256@": "provided-by-adjacent-archive-sidecar",
    }
    acceptance_documents = (
        "acceptance_baseline.md",
        "acceptance_architecture.md",
        "acceptance_demo_script.md",
        "acceptance_evidence_index.md",
        "acceptance_evidence_matrix.md",
        "acceptance_evidence_matrix.json",
        "acceptance_evidence_matrix_schema_v1.json",
        "acceptance_presentation_baseline.md",
        "acceptance_q_and_a.md",
        "acceptance_scenario_baseline.md",
        "final_acceptance_presentation_source.md",
        "final_acceptance_report_source.md",
        "manual_screenshot_capture.md",
        "screenshot_manifest.md",
    )
    for name in acceptance_documents:
        materialize_text(
            platform / "docs/acceptance" / name,
            release_root / "docs/acceptance" / name,
            runtime_replacements,
        )
    materialize_text(
        platform / "release/README.template.md",
        release_root / "README.md",
        runtime_replacements,
    )

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
    runtime_file_count, runtime_uncompressed_bytes = file_metrics(release_root)
    archive_digest = deterministic_archive(release_root, archive, source_epoch)
    sidecar.write_text(f"{archive_digest}  {archive.name}\n", encoding="utf-8")

    handoff_replacements = {
        "@SOURCE_REVISION@": source_revision,
        "@ARCHIVE_SHA256@": archive_digest,
    }
    materialize_text(
        platform / "release/HANDOFF_README.template.md",
        handoff_root / "README.md",
        handoff_replacements,
    )
    for name in acceptance_documents:
        materialize_text(
            platform / "docs/acceptance" / name,
            handoff_root / name,
            handoff_replacements,
        )
    copy_file(
        platform / "acceptance/acceptance4_baseline_v1.json",
        handoff_root / "acceptance4_baseline_v1.json",
    )
    copy_file(platform / "release/FINAL_TBD.md", handoff_root / "TBD.md")
    handoff_manifest = {
        "schema_version": 1,
        "package_id": HANDOFF_ROOT,
        "material_type": "acceptance-material-not-runtime-release",
        "runtime_release_id": RELEASE_ID,
        "source_revision": source_revision,
        "runtime_archive": {"filename": archive.name, "sha256": archive_digest},
        "reference_environment": {"asset_id": ASSET_ID, "checksum": ASSET_CHECKSUM},
        "acceptance_baseline": {"id": "Acceptance4Node", "version": "1"},
        "screenshots": {
            "status": "manual-capture-required-no-images-in-package",
            "authority": "manual_screenshot_capture.md",
        },
    }
    (handoff_root / "HANDOFF_MANIFEST.json").write_text(
        json.dumps(handoff_manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    matrix = json.loads((handoff_root / "acceptance_evidence_matrix.json").read_text(encoding="utf-8"))
    (handoff_root / "golden_run_summary.json").write_text(
        json.dumps(matrix["golden_run"], indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    final_record = f"""# P0-S5-05 Final Release Record

- Runtime release: `{RELEASE_ID}`
- Source revision: `{source_revision}`
- Build target: `{TARGET}`
- Runtime archive: `{archive.name}`
- Runtime archive bytes: `{archive.stat().st_size}`
- Runtime archive SHA-256: `{archive_digest}`
- Runtime uncompressed bytes: `{runtime_uncompressed_bytes}`
- Runtime file count: `{runtime_file_count}`
- Handoff package identity: `{HANDOFF_ROOT}`
- Reference asset: `{ASSET_ID}`
- Reference checksum: FNV1A64 `{ASSET_CHECKSUM}`
- Acceptance baseline: `Acceptance4Node` version 1

## P0-S5-05 Golden normalized hashes

- Result: `{matrix['golden_run']['normalized_hashes']['result_sha256']}`
- Fusion: `{matrix['golden_run']['normalized_hashes']['fusion_sha256']}`
- Trace ordering: `{matrix['golden_run']['normalized_hashes']['trace_sha256']}`
- Acceptance report: `{matrix['golden_run']['normalized_hashes']['acceptance_report_sha256']}`

P0 SOFTWARE BASELINE = FROZEN FOR ACCEPTANCE
"""
    (handoff_root / "final_release_record.md").write_text(final_record, encoding="utf-8")

    handoff_checksums = []
    for path in sorted(handoff_root.rglob("*")):
        if path.is_file() and path.name != "SHA256SUMS":
            handoff_checksums.append(f"{sha256(path)}  {path.relative_to(handoff_root).as_posix()}")
            path.chmod(0o644)
    (handoff_root / "SHA256SUMS").write_text("\n".join(handoff_checksums) + "\n", encoding="utf-8")
    handoff_root.chmod(0o755)
    run([
        sys.executable,
        str(platform / "release/tests/handoff_bundle_test.py"),
        str(handoff_root),
        source_revision,
        archive_digest,
    ])
    handoff_digest = deterministic_archive(handoff_root, handoff_archive, source_epoch)
    handoff_sidecar.write_text(f"{handoff_digest}  {handoff_archive.name}\n", encoding="utf-8")
    print(json.dumps({
        "release_root": str(release_root),
        "archive": str(archive),
        "archive_sha256": archive_digest,
        "runtime_file_count": runtime_file_count,
        "runtime_uncompressed_bytes": runtime_uncompressed_bytes,
        "handoff_root": str(handoff_root),
        "handoff_archive": str(handoff_archive),
        "handoff_archive_sha256": handoff_digest,
    }, sort_keys=True))


if __name__ == "__main__":
    main()
