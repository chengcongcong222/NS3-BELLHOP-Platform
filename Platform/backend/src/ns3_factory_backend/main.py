from __future__ import annotations

import os
import atexit
from contextlib import asynccontextmanager
from pathlib import Path

from .api import BackendSettings, create_app
from .algorithm_installation import InstallationLease


_catalog_adapter = os.environ.get("PLATFORM_RESOURCE_CATALOG_ADAPTER")
_installation_root = os.environ.get("PLATFORM_ALGORITHM_INSTALL_ROOT")
_installation = InstallationLease(Path(_installation_root)) if _installation_root else None
if _installation is not None:
    atexit.register(_installation.close)


app = create_app(
    BackendSettings(
        worker_executable=(_installation.worker if _installation is not None
                           else Path(os.environ.get("PLATFORM_SIM_WORKER", ""))),
        environment_repository_root=Path(
            os.environ.get("PLATFORM_ENVIRONMENT_REPOSITORY", "")
        ),
        resource_catalog_adapter=(Path(_catalog_adapter) if _catalog_adapter else None),
        acceptance_environment_asset_id=os.environ.get(
            "PLATFORM_ACCEPTANCE_ENVIRONMENT_ASSET_ID",
            "reference-shallow-water-v1",
        ),
        acceptance_baseline_path=Path(
            os.environ.get(
                "PLATFORM_ACCEPTANCE_BASELINE",
                Path(__file__).resolve().parents[3]
                / "acceptance"
                / "acceptance4_baseline_v1.json",
            )
        ),
        source_revision=os.environ.get(
            "PLATFORM_SOURCE_REVISION", "unavailable"
        ),
        build_configuration=os.environ.get(
            "PLATFORM_BUILD_CONFIGURATION", "ns3-on"
        ),
        platform_version=os.environ.get("PLATFORM_VERSION", "0.1.0"),
        frontend_release=os.environ.get(
            "PLATFORM_FRONTEND_RELEASE", "p0-s5-05"
        ),
        release_id=os.environ.get("PLATFORM_RELEASE_ID", "P0-S5-05"),
        build_target=os.environ.get("PLATFORM_BUILD_TARGET", "linux-x86_64"),
        reference_environment_asset_id=os.environ.get(
            "PLATFORM_REFERENCE_ENVIRONMENT_ASSET_ID",
            "reference-shallow-water-v1",
        ),
        reference_environment_checksum=os.environ.get(
            "PLATFORM_REFERENCE_ENVIRONMENT_CHECKSUM",
            "fb64e543f9042c52",
        ),
        frontend_origin=os.environ.get(
            "PLATFORM_FRONTEND_ORIGIN", "http://127.0.0.1:4173"
        ),
        authoring_workspace_root=(
            Path(os.environ["PLATFORM_AUTHORING_WORKSPACE"])
            if os.environ.get("PLATFORM_AUTHORING_WORKSPACE")
            else Path(__file__).resolve().parents[3]
            / ".runtime"
            / "authoring-workspace"
        ),
        authoring_reference_asset_root=(
            Path(os.environ["PLATFORM_AUTHORING_REFERENCE_ASSET_ROOT"])
            if os.environ.get("PLATFORM_AUTHORING_REFERENCE_ASSET_ROOT")
            else None
        ),
        trajectory_generator_executable=(
            Path(os.environ["PLATFORM_TRAJECTORY_GENERATOR"])
            if os.environ.get("PLATFORM_TRAJECTORY_GENERATOR")
            else None
        ),
        environment_asset_publisher_executable=(
            Path(os.environ["PLATFORM_ENVIRONMENT_ASSET_PUBLISHER"])
            if os.environ.get("PLATFORM_ENVIRONMENT_ASSET_PUBLISHER") else None
        ),
    )
)

if _installation is not None:
    _original_lifespan = app.router.lifespan_context

    @asynccontextmanager
    async def _managed_lifespan(application):
        try:
            async with _original_lifespan(application):
                yield
        finally:
            # The original lifespan first reaps all owned workers.
            _installation.close()

    app.router.lifespan_context = _managed_lifespan
