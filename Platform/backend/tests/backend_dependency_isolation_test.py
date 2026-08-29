from __future__ import annotations

import re
from pathlib import Path


PLATFORM_ROOT = Path(__file__).resolve().parents[2]
BACKEND_ROOT = PLATFORM_ROOT / "backend"
BACKEND_SOURCE = BACKEND_ROOT / "src"


def test_fastapi_stack_is_local_to_backend() -> None:
    forbidden = re.compile(r"^\s*(from|import)\s+(fastapi|pydantic|uvicorn)\b", re.M)
    violations = []
    for source in PLATFORM_ROOT.rglob("*.py"):
        if BACKEND_ROOT in source.parents or "third_party" in source.parts:
            continue
        if forbidden.search(source.read_text()):
            violations.append(source.relative_to(PLATFORM_ROOT).as_posix())
    assert violations == []


def test_backend_process_boundary_has_no_shell_or_ns3_import() -> None:
    forbidden = [
        re.compile(r"shell\s*=\s*True"),
        re.compile(r"os\.system\s*\("),
        re.compile(r"^\s*(from|import)\s+ns3\b", re.M),
    ]
    violations = []
    for source in BACKEND_SOURCE.rglob("*.py"):
        text = source.read_text()
        if any(pattern.search(text) for pattern in forbidden):
            violations.append(source.relative_to(PLATFORM_ROOT).as_posix())
    assert violations == []
