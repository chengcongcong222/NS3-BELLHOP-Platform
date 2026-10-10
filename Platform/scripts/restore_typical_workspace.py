#!/usr/bin/env python3
"""Restore the checked-in small teaching dataset into a NEW, empty directory."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import tempfile


def restore(seed: Path, target: Path) -> None:
    seed = seed.resolve()
    target = target.resolve()
    if target.exists():
        raise ValueError("目标已存在；为避免覆盖用户数据，请选择新的目录")
    manifest = json.loads((seed / "manifest.json").read_text())
    for name, digest in manifest["sha256"].items():
        if Path(name).is_absolute() or ".." in Path(name).parts:
            raise ValueError(f"非法相对路径: {name}")
        path = (seed / "data" / name).resolve()
        if not path.is_relative_to(seed / "data") or not path.is_file():
            raise ValueError(f"非法或缺失文件: {name}")
        if hashlib.sha256(path.read_bytes()).hexdigest() != digest:
            raise ValueError(f"文件校验失败: {name}")
    target.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="typical-restore-", dir=target.parent) as scratch:
        staged = Path(scratch) / "data"
        staged.mkdir()
        for name in manifest["sha256"]:
            dest = staged / name
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(seed / "data" / name, dest)
            if name.startswith("workspace/") and dest.suffix == ".json":
                text = dest.read_text()
                if "${WORKSPACE}" in text:
                    # JSON-escape the path so directories with quotes are safe.
                    escaped = json.dumps(str(target / "workspace"))[1:-1]
                    dest.write_text(text.replace("${WORKSPACE}", escaped))
        staged.rename(target)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("target", type=Path)
    parser.add_argument("--seed", type=Path,
                        default=Path(__file__).resolve().parents[1] / "release/typical-system")
    args = parser.parse_args()
    try:
        restore(args.seed, args.target)
    except (ValueError, OSError) as error:
        parser.error(str(error))
    print(f"典型数据已恢复：{args.target.resolve()}；未覆盖任何现有数据。")
