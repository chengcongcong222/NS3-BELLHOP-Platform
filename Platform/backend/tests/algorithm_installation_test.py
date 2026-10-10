import fcntl
import json
from pathlib import Path

import pytest

from ns3_factory_backend.algorithm_installation import InstallationLease, digest, read_release


def release(tmp_path):
    binary = tmp_path / "fixture-worker"
    binary.write_bytes(b"trusted test fixture, not executable")
    identity = digest(binary)
    path = tmp_path / "releases" / identity
    path.mkdir(parents=True)
    binary.rename(path / "platform_sim_worker")
    (path / "manifest.json").write_text(json.dumps({"schema": "platform.algorithm-installation.v1",
        "worker_sha256": identity, "dependencies": []}))
    (tmp_path / "active.json").write_text(json.dumps({"release": identity}))
    return identity, path


def test_service_lease_blocks_activation_until_reaped(tmp_path):
    identity, path = release(tmp_path)
    with InstallationLease(tmp_path) as lease:
        assert lease.worker == path / "platform_sim_worker"
        with (tmp_path / "activation.lock").open("a+") as lock:
            with pytest.raises(BlockingIOError):
                fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    with (tmp_path / "activation.lock").open("a+") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    assert read_release(tmp_path, identity)[0] == path / "platform_sim_worker"


def test_corrupt_release_and_changed_dependency_fail_closed(tmp_path):
    identity, path = release(tmp_path)
    manifest = json.loads((path / "manifest.json").read_text())
    dependency = tmp_path / "dependency.so"
    dependency.write_bytes(b"dependency fixture")
    manifest["dependencies"] = [{"path": str(dependency), "sha256": digest(dependency)}]
    (path / "manifest.json").write_text(json.dumps(manifest))
    assert read_release(tmp_path, identity)
    dependency.write_bytes(b"changed fixture")
    with pytest.raises(ValueError, match="依赖"):
        read_release(tmp_path, identity)
    (path / "platform_sim_worker").write_bytes(b"corrupt fixture")
    with pytest.raises(ValueError, match="执行程序"):
        read_release(tmp_path, identity)
    with pytest.raises(ValueError, match="标识"):
        read_release(tmp_path, "../../elsewhere")
