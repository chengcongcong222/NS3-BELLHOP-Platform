from __future__ import annotations

import hashlib
import importlib.util
import json
import subprocess
import sys
from pathlib import Path


def main(root: Path, temporary: Path) -> None:
    schema = json.loads((root / "release_manifest_schema_v1.json").read_text())
    assert schema["properties"]["release_id"]["const"] == "P0-S5-05"
    assert schema["properties"]["build_target"]["const"] == "linux-x86_64"
    builder = (root / "build_release.py").read_text()
    launcher = (root / "runtime/release.sh").read_text()
    preflight = (root / "runtime/release_preflight.py").read_text()
    combined = builder + launcher + preflight
    assert "/home/ccc" not in combined
    assert '"status", "--porcelain"' in builder
    assert "clean committed source tree" in builder
    assert '"--sort=name"' in builder and '"gzip", "-n", "-9"' in builder
    assert 'RELEASE_ID = "P0-S5-05"' in builder
    assert 'HANDOFF_ROOT = "acceptance-handoff-p0-s5-05"' in builder
    assert "handoff_bundle_test.py" in builder
    handoff_template = (root / "HANDOFF_README.template.md").read_text()
    assert "not a second runtime distribution" in handoff_template
    assert "@SOURCE_REVISION@" in handoff_template
    assert "@ARCHIVE_SHA256@" in handoff_template
    assert "PLATFORM_NS3_PREFIX" in launcher and "PLATFORM_NS3_PREFIX" in preflight
    assert "Linux" in preflight and "x86_64" in preflight
    for command in ("prepare", "preflight", "start", "status", "restart", "stop"):
        assert command in launcher
    assert "RELEASE_STOP_FAILED" in launcher
    assert 'probe.bind(("127.0.0.1",port))' in launcher
    assert "SO_REUSEADDR" in launcher and "SO_REUSEADDR" in preflight

    module_spec = importlib.util.spec_from_file_location(
        "release_preflight_under_test", root / "runtime/release_preflight.py"
    )
    assert module_spec is not None and module_spec.loader is not None
    preflight_module = importlib.util.module_from_spec(module_spec)
    module_spec.loader.exec_module(preflight_module)
    for system_name, machine_name in (("Darwin", "x86_64"), ("Linux", "aarch64")):
        try:
            preflight_module.validate_platform(system_name, machine_name)
        except SystemExit as error:
            assert error.code == 2
        else:
            raise AssertionError(f"unsupported platform accepted: {system_name} {machine_name}")

    fixture = temporary / "fixture"
    scripts = fixture / "scripts"
    scripts.mkdir(parents=True)
    verifier = root / "runtime/verify_checksums.py"
    target = fixture / "payload.bin"
    target.write_bytes(b"canonical")
    digest = hashlib.sha256(target.read_bytes()).hexdigest()
    (fixture / "SHA256SUMS").write_text(f"{digest}  payload.bin\n")
    copied = scripts / "verify_checksums.py"
    copied.write_bytes(verifier.read_bytes())
    passed = subprocess.run([sys.executable, str(copied)], capture_output=True, text=True)
    assert passed.returncode == 0 and "RELEASE_INTEGRITY_OK" in passed.stdout
    target.write_bytes(b"corrupted")
    failed = subprocess.run([sys.executable, str(copied)], capture_output=True, text=True)
    assert failed.returncode == 2 and "RELEASE_INTEGRITY_FAILED" in failed.stderr


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: release_contract_test.py RELEASE_SOURCE_ROOT")
    import tempfile

    with tempfile.TemporaryDirectory() as directory:
        main(Path(sys.argv[1]), Path(directory))
