from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path


def main(root: Path, expected_revision: str) -> None:
    manifest = json.loads((root / "MANIFEST.json").read_text())
    assert manifest["schema_version"] == 1
    assert manifest["release_id"] == "P0-S5-03"
    assert manifest["source_revision"] == expected_revision
    assert manifest["build_target"] == "linux-x86_64"
    assert manifest["reference_environment"] == {
        "asset_id": "reference-shallow-water-v1",
        "checksum": {"algorithm": "FNV1A64", "value": "fb64e543f9042c52"},
    }
    assert manifest["acceptance_baseline"] == {
        "id": "Acceptance4Node",
        "version": "1",
    }

    inventory = json.loads((root / "release_inventory.json").read_text())
    entries = {item["path"]: item for item in inventory["files"]}
    actual = {
        path.relative_to(root).as_posix()
        for path in root.rglob("*")
        if path.is_file()
    }
    assert set(entries) == actual - {"release_inventory.json", "SHA256SUMS"}
    for relative, entry in entries.items():
        path = root / relative
        assert entry["size"] == path.stat().st_size
        assert len(entry["sha256"]) == 64
        assert entry["category"] in {
            "binary", "dependency", "frontend", "asset", "acceptance",
            "documentation", "license", "launcher", "metadata",
        }

    required = [
        "release.sh", "README.md", "MANIFEST.json", "SHA256SUMS",
        "bin/platform_sim_worker", "bin/platform_resource_catalog_adapter",
        "frontend/index.html", "acceptance/acceptance4_baseline_v1.json",
        "assets/environment-repository/reference-shallow-water-v1/field.bin",
        "assets/environment-repository/reference-shallow-water-v1/manifest.txt",
        "docs/acceptance_runbook.md", "licenses/THIRD_PARTY_NOTICES.md",
    ]
    for relative in required:
        assert (root / relative).is_file(), relative
    for relative in ("release.sh", "bin/platform_sim_worker", "bin/platform_resource_catalog_adapter"):
        assert (root / relative).stat().st_mode & 0o111

    text_suffixes = {".json", ".md", ".txt", ".py", ".sh", ".lock", ".html", ".css", ".js"}
    for path in root.rglob("*"):
        if path.is_file() and path.suffix in text_suffixes:
            text = path.read_text(errors="ignore")
            assert "/home/ccc" not in text, path
            assert "working-tree@" not in text, path
    verify = subprocess.run(
        [sys.executable, str(root / "scripts/verify_checksums.py")],
        capture_output=True,
        text=True,
    )
    assert verify.returncode == 0, verify.stderr
    dependencies = json.loads((root / "binary_dependencies.json").read_text())
    assert dependencies["ns3_decision"] == "external-prefix"
    assert "libns3.47-core-default.so" in dependencies["binaries"]["platform_sim_worker"]["elf_needed"]


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("usage: release_bundle_test.py RELEASE_ROOT SOURCE_REVISION")
    main(Path(sys.argv[1]), sys.argv[2])
