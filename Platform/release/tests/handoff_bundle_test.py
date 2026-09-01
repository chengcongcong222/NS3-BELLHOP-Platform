from __future__ import annotations

import hashlib
import json
import sys
from pathlib import Path


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main(root: Path, expected_revision: str, expected_runtime_sha256: str) -> None:
    manifest = json.loads((root / "HANDOFF_MANIFEST.json").read_text(encoding="utf-8"))
    assert manifest == {
        "schema_version": 1,
        "package_id": "acceptance-handoff-p0-s5-05",
        "material_type": "acceptance-material-not-runtime-release",
        "runtime_release_id": "P0-S5-05",
        "source_revision": expected_revision,
        "runtime_archive": {
            "filename": "ns3-bellhop-platform-p0-s5-05-linux-x86_64.tar.gz",
            "sha256": expected_runtime_sha256,
        },
        "reference_environment": {
            "asset_id": "reference-shallow-water-v1",
            "checksum": "fb64e543f9042c52",
        },
        "acceptance_baseline": {"id": "Acceptance4Node", "version": "1"},
        "screenshots": {
            "status": "manual-capture-required-no-images-in-package",
            "authority": "manual_screenshot_capture.md",
        },
    }
    required = {
        "README.md", "HANDOFF_MANIFEST.json", "SHA256SUMS", "TBD.md",
        "acceptance4_baseline_v1.json", "acceptance_baseline.md", "acceptance_architecture.md",
        "acceptance_demo_script.md", "acceptance_evidence_index.md",
        "acceptance_evidence_matrix.md", "acceptance_evidence_matrix.json",
        "acceptance_evidence_matrix_schema_v1.json", "acceptance_presentation_baseline.md",
        "acceptance_q_and_a.md", "acceptance_scenario_baseline.md",
        "final_acceptance_presentation_source.md", "final_acceptance_report_source.md",
        "final_release_record.md", "golden_run_summary.json",
        "manual_screenshot_capture.md", "screenshot_manifest.md",
    }
    assert required <= {path.name for path in root.iterdir() if path.is_file()}
    for forbidden in ("bin", "backend", "frontend", "release.sh", "scripts"):
        assert not (root / forbidden).exists(), forbidden
    for path in root.rglob("*"):
        if path.is_file() and path.name != "SHA256SUMS":
            text = path.read_text(encoding="utf-8", errors="ignore")
            assert "/home/ccc" not in text, path
            assert "@SOURCE_REVISION@" not in text, path
            assert "@ARCHIVE_SHA256@" not in text, path
    lines = (root / "SHA256SUMS").read_text(encoding="utf-8").splitlines()
    for line in lines:
        digest, relative = line.split("  ", 1)
        assert sha256(root / relative) == digest


if __name__ == "__main__":
    if len(sys.argv) != 4:
        raise SystemExit("usage: handoff_bundle_test.py HANDOFF_ROOT SOURCE_REVISION RUNTIME_SHA256")
    main(Path(sys.argv[1]), sys.argv[2], sys.argv[3])
