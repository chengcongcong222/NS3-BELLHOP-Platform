#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import sys
from pathlib import Path


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    checksum_file = root / "SHA256SUMS"
    failures: list[str] = []
    for line in checksum_file.read_text(encoding="utf-8").splitlines():
        digest, relative = line.split("  ", 1)
        path = root / relative
        if not path.is_file():
            failures.append(f"missing: {relative}")
            continue
        actual = hashlib.sha256(path.read_bytes()).hexdigest()
        if actual != digest:
            failures.append(f"checksum mismatch: {relative}")
    if failures:
        print("RELEASE_INTEGRITY_FAILED", file=sys.stderr)
        for failure in failures:
            print(failure, file=sys.stderr)
        raise SystemExit(2)
    print("RELEASE_INTEGRITY_OK")


if __name__ == "__main__":
    main()
