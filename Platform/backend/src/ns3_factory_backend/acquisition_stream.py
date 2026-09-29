"""Per-request authoring progress; unrelated to the simulation event journal."""
from __future__ import annotations

import asyncio
import json
from collections.abc import Callable, AsyncIterator
from threading import Event

from starlette.concurrency import run_in_threadpool

from .ocean_download import Progress


async def acquisition_stream(lock: asyncio.Lock, operation: Callable[[Progress], dict]) -> AsyncIterator[str]:
    loop = asyncio.get_running_loop()
    queue: asyncio.Queue[dict] = asyncio.Queue()
    stopped = Event()

    def report(progress: dict) -> None:
        if stopped.is_set():
            raise RuntimeError("已取消获取，已完成的缓存仍保留")
        loop.call_soon_threadsafe(queue.put_nowait, {"type": "progress", **progress})

    async def run() -> None:
        try:
            async with lock:
                if stopped.is_set():
                    return
                result = await run_in_threadpool(operation, report)
            await queue.put({"type": "result", "result": result})
        except Exception as error:
            await queue.put({"type": "error", "message": str(error)})

    task = asyncio.create_task(run())
    try:
        yield json.dumps({"type": "progress", "stage": "queued",
                          "message": "正在等待水文读取任务；首次获取该月份需下载温度和盐度两个文件"}, ensure_ascii=False) + "\n"
        while True:
            try:
                event = await asyncio.wait_for(queue.get(), timeout=5)
            except TimeoutError:
                event = {"type": "heartbeat"}
            yield json.dumps(event, ensure_ascii=False, allow_nan=False) + "\n"
            if event["type"] in ("result", "error"):
                break
    finally:
        stopped.set()
        # Do not release the NetCDF lock while its thread is still active.
        # Network reads have a 20 s inactivity timeout and check cancellation.
        await asyncio.shield(task)
