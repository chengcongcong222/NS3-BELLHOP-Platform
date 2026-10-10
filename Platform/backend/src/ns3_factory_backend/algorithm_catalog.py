"""Read the compiled Worker catalog; never execute algorithms in Python."""
from __future__ import annotations

import asyncio
import hashlib
import json
import math
from pathlib import Path
import re
import subprocess

from starlette.concurrency import run_in_threadpool
from .catalog import CatalogError

INTEGER = re.compile(r"(?:0|-?[1-9][0-9]*)\Z")
NUMBER = re.compile(r"-?(?:0|[1-9][0-9]*)(?:\.[0-9]+)?(?:[eE][+-]?[0-9]+)?\Z")


def validate_selection(selection: object, catalog: dict, *, frozen: bool = False) -> dict:
    required = {"id", "version", "package_sha256", "interface", "parameters"}
    if frozen:
        required.add("worker_sha256")
    if not isinstance(selection, dict) or set(selection) != required:
        raise CatalogError("AlgorithmConfigurationInvalid", "算法配置不完整或包含未知字段，请重新选择算法。")
    candidates = [a for a in catalog["algorithms"] if all(selection.get(k) == a.get(k)
                  for k in ("id", "version", "package_sha256", "interface"))]
    if len(candidates) != 1:
        raise CatalogError("AlgorithmUnavailable", "此实验使用的算法版本未安装或源码已变化，请显式重新选择；未替换算法。")
    if frozen and selection["worker_sha256"] != catalog["worker_sha256"]:
        raise CatalogError("AlgorithmUnavailable", "原运行的算法执行程序已变化，不能按原配置复现；可编辑实验后创建新运行。")
    algorithm = candidates[0]
    parameters = selection["parameters"]
    if not isinstance(parameters, dict) or set(parameters) != {p["id"] for p in algorithm["parameters"]}:
        raise CatalogError("AlgorithmConfigurationInvalid", "算法参数缺失或包含其他算法的参数。")
    for field in algorithm["parameters"]:
        value = parameters[field["id"]]
        valid = isinstance(value, str) and len(value) <= 128
        kind = field["type"]
        if valid and kind in {"integer", "duration_ns"}:
            valid = bool(INTEGER.fullmatch(value)) and -(2**63) <= int(value) < 2**63 and int(field["min"]) <= int(value) <= int(field["max"])
        elif valid and kind == "number":
            valid = bool(NUMBER.fullmatch(value)) and math.isfinite(float(value)) and float(field["min"]) <= float(value) <= float(field["max"])
        elif valid and kind == "boolean":
            valid = value in {"true", "false"}
        elif valid and kind == "enum":
            valid = value in field["choices"]
        elif valid:
            valid = False
        if not valid:
            raise CatalogError("AlgorithmConfigurationInvalid", f"{field['label']}：请填写允许范围内的{field['unit'] or '参数值'}。")
    return {**selection, "parameters": dict(parameters), "worker_sha256": catalog["worker_sha256"]}


class AlgorithmCatalog:
    def __init__(self, worker: Path):
        self.worker = worker
        self.lock = asyncio.Lock()
        self._stamp = None
        self._catalog = None

    def _load(self) -> dict:
        try:
            info = self.worker.stat()
            stamp = (info.st_dev, info.st_ino, info.st_size, info.st_mtime_ns, info.st_ctime_ns)
            if self._stamp == stamp:
                return self._catalog
            result = subprocess.run([str(self.worker), "--algorithm-catalog"],
                                    capture_output=True, timeout=15, check=True)
            if len(result.stdout) > 1_048_576:
                raise ValueError("algorithm catalog exceeds size limit")
            data = json.loads(result.stdout)
            if data.get("schema") != "platform.algorithm-catalog.v1" or not isinstance(data.get("algorithms"), list):
                raise ValueError("unsupported algorithm catalog")
            identities = [(a["id"], a["version"]) for a in data["algorithms"]]
            if len(set(identities)) != len(identities):
                raise ValueError("duplicate installed algorithm")
            with self.worker.open("rb") as stream:
                data["worker_sha256"] = hashlib.file_digest(stream, "sha256").hexdigest()
            after = self.worker.stat()
            if stamp != (after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns, after.st_ctime_ns):
                raise ValueError("worker changed during catalog read")
            self._stamp, self._catalog = stamp, data
            return data
        except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError) as error:
            raise CatalogError("AlgorithmUnavailable", "算法目录无法读取，请检查服务端算法安装与 Worker 版本。") from error

    async def current(self) -> dict:
        async with self.lock:
            return await run_in_threadpool(self._load)

    async def freeze(self, payload: dict, *, frozen: bool = False) -> dict | None:
        if payload.get("mac_mode") != "RegisteredMac":
            return None
        return validate_selection(payload.get("mac_algorithm"), await self.current(), frozen=frozen)
