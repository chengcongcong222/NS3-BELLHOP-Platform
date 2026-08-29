from __future__ import annotations

import hashlib
import importlib.metadata
import zipfile
from pathlib import Path


BACKEND_DIR = Path(__file__).resolve().parents[1]
WHEELHOUSE = BACKEND_DIR.parent / "third_party" / "python_wheels" / "wheels"


def _locked() -> dict[str, tuple[str, str]]:
    result: dict[str, tuple[str, str]] = {}
    for raw_line in (BACKEND_DIR / "requirements.lock").read_text().splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        requirement, hash_part = line.split(" --hash=sha256:", maxsplit=1)
        name, version = requirement.split("==", maxsplit=1)
        result[name.replace("_", "-").lower()] = (version, hash_part)
    return result


def test_lock_matches_wheelhouse_and_environment() -> None:
    locked = _locked()
    wheels = sorted(WHEELHOUSE.glob("*.whl"))
    assert len(locked) == 21
    assert len(wheels) == len(locked)

    wheel_hashes = {
        hashlib.sha256(path.read_bytes()).hexdigest(): path.name for path in wheels
    }
    assert {digest for _, digest in locked.values()} == set(wheel_hashes)

    for name, (version, _) in locked.items():
        assert importlib.metadata.version(name) == version


def test_dependency_baseline_versions() -> None:
    assert importlib.metadata.version("fastapi") == "0.141.1"
    assert importlib.metadata.version("pydantic") == "2.13.5"
    assert importlib.metadata.version("uvicorn") == "0.52.4"
    assert importlib.metadata.version("httpx") == "0.28.1"
    assert importlib.metadata.version("pytest") == "9.1.1"


def test_every_vendored_wheel_retains_license_notice() -> None:
    for wheel in WHEELHOUSE.glob("*.whl"):
        with zipfile.ZipFile(wheel) as archive:
            assert any(".dist-info/licenses/" in name for name in archive.namelist())
