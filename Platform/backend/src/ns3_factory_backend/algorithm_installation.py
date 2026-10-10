"""Local immutable Worker releases. Shared service leases exclude activation.

This is deployment coordination, not a simulation clock or algorithm runtime.
Only trusted local administrators can publish installations.
"""
from __future__ import annotations

import fcntl
import hashlib
import json
from pathlib import Path
import re


def digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def read_release(root: Path, identity: str) -> tuple[Path, dict]:
    if not re.fullmatch(r"[0-9a-f]{64}", identity):
        raise ValueError("安装版本标识无效。")
    directory = root / "releases" / identity
    if directory.is_symlink():
        raise ValueError("安装目录不能是符号链接。")
    manifest = json.loads((directory / "manifest.json").read_text())
    worker = directory / "platform_sim_worker"
    if (manifest.get("schema") != "platform.algorithm-installation.v1"
            or manifest.get("worker_sha256") != identity or worker.is_symlink()
            or digest(worker) != identity):
        raise ValueError("安装的执行程序已变化，请重新检查安装；未替换历史版本。")
    for dependency in manifest["dependencies"]:
        path = Path(dependency["path"])
        if not path.is_file() or digest(path) != dependency["sha256"]:
            raise ValueError("安装依赖缺失或已变化：" + path.name)
    return worker, manifest


class InstallationLease:
    """Pin one release for a backend lifetime, never hot-replace an active run."""

    def __init__(self, root: Path):
        self.root = root.resolve(strict=True)
        self._lock = (self.root / "activation.lock").open("a+")
        try:
            fcntl.flock(self._lock, fcntl.LOCK_SH)
            identity = json.loads((self.root / "active.json").read_text())["release"]
            self.worker, self.manifest = read_release(self.root, identity)
        except BaseException:
            self.close()
            raise

    def close(self):
        if not self._lock.closed:
            fcntl.flock(self._lock, fcntl.LOCK_UN)
            self._lock.close()

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()

    def __del__(self):
        if hasattr(self, "_lock"):
            self.close()
