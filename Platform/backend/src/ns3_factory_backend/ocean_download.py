"""Bounded, observable downloads for offline authoring (never Runtime)."""
from __future__ import annotations

import time
import urllib.error
import urllib.request
import uuid
from pathlib import Path
from typing import Callable, Any

Progress = Callable[[dict[str, Any]], None]


def download_dataset(url: str, destination: Path, report: Progress,
                     *, total_timeout: float = 600) -> None:
    # A failed/interrupted transfer must never look like a complete .nc cache.
    # Keep incomplete files for diagnosis; a retry writes its own temporary file.
    partial = destination.with_name(destination.name + f".{uuid.uuid4().hex}.partial")
    started = time.monotonic()
    report({"stage": "connecting", "message": "正在连接 NOAA 海洋数据服务"})
    request = urllib.request.Request(url, headers={"User-Agent": "NS3-BELLHOP-Platform"})
    try:
        with urllib.request.urlopen(request, timeout=20) as response, partial.open("wb") as output:
            length = response.headers.get("Content-Length")
            total = int(length) if length and length.isdigit() else None
            received = 0
            last_report = 0.0
            while True:
                now = time.monotonic()
                if now - started > total_timeout:
                    raise TimeoutError("单个数据文件下载超过 10 分钟")
                # read1 returns available bytes instead of waiting for a whole chunk.
                chunk = response.read1(256 * 1024)
                if not chunk:
                    break
                output.write(chunk)
                received += len(chunk)
                if now - last_report >= 0.2:
                    report({"stage": "downloading", "message": "正在下载 NOAA 月度数据",
                            "received_bytes": received, "total_bytes": total})
                    last_report = now
            if not received or (total is not None and received != total):
                raise ValueError("下载不完整，请重试；未将不完整文件作为缓存")
            report({"stage": "downloading", "message": "下载完成，正在检查数据",
                    "received_bytes": received, "total_bytes": total})
        # The caller validates NetCDF before marking the cache usable on later queries.
        partial.replace(destination)
    except (urllib.error.URLError, TimeoutError, OSError) as error:
        raise RuntimeError("WOA 数据服务连接或下载超时，请重试；已完成的数据文件可继续复用。"
                           f"（{error}）") from error
