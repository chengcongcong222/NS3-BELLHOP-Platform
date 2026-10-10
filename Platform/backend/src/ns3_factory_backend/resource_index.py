"""App-owned index of C++-validated immutable resources, not acoustic data.

Filesystem stamps only invalidate an index; they never establish validity.
Only the C++ adapter can replace the complete validated snapshot. Run start
uses a separate exact-package validation, even when the browsing index is warm.
"""
from __future__ import annotations

import asyncio
from collections.abc import Callable
from pathlib import Path

from starlette.concurrency import run_in_threadpool

from .resources import ResourceCatalog, ResourceCatalogConfigurationError


def repository_stamp(root: Path) -> tuple:
    def stamp(path: Path) -> tuple:
        info = path.stat()
        return (str(path.relative_to(root)), info.st_dev, info.st_ino,
                info.st_mode, info.st_size, info.st_mtime_ns, info.st_ctime_ns)

    rows = [stamp(root)]
    for package in sorted(root.iterdir()):
        rows.append(stamp(package))
        if package.is_dir():
            rows.extend(stamp(child) for child in sorted(package.iterdir()))
    return tuple(rows)


class VerifiedResourceIndex:
    def __init__(self, root: Path, initial: ResourceCatalog, initial_stamp: tuple,
                 loader: Callable[[], ResourceCatalog]) -> None:
        self.root = root
        self._snapshot = initial
        self._stamp = initial_stamp
        self._failed_stamp: tuple | None = None
        self._loader = loader
        # Publication and full package checks share this gate: no memory
        # stampede and no catalog scans of a publisher's temporary package.
        self.lock = asyncio.Lock()

    def refresh_locked(self) -> ResourceCatalog:
        """Blocking IO; caller holds lock and runs this in a worker thread."""
        try:
            before = repository_stamp(self.root)
            if before == self._failed_stamp:
                raise ResourceCatalogConfigurationError("environment repository validation failed")
            if before == self._stamp:
                return self._snapshot
            try:
                updated = self._loader()
            except ResourceCatalogConfigurationError:
                self._failed_stamp = before
                raise
            if before != repository_stamp(self.root):
                raise ResourceCatalogConfigurationError("environment repository changed during validation")
            self._snapshot, self._stamp = updated, before
            self._failed_stamp = None
            return updated
        except OSError as error:
            raise ResourceCatalogConfigurationError("environment repository is unavailable") from error

    async def current(self) -> ResourceCatalog:
        async with self.lock:
            return await run_in_threadpool(self.refresh_locked)
