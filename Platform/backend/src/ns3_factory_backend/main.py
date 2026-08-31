from __future__ import annotations

import os
from pathlib import Path

from .api import BackendSettings, create_app


_catalog_adapter = os.environ.get("PLATFORM_RESOURCE_CATALOG_ADAPTER")


app = create_app(
    BackendSettings(
        worker_executable=Path(os.environ.get("PLATFORM_SIM_WORKER", "")),
        environment_repository_root=Path(
            os.environ.get("PLATFORM_ENVIRONMENT_REPOSITORY", "")
        ),
        resource_catalog_adapter=(Path(_catalog_adapter) if _catalog_adapter else None),
        acceptance_environment_asset_id=os.environ.get(
            "PLATFORM_ACCEPTANCE_ENVIRONMENT_ASSET_ID", "backend-field-v1"
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
            "PLATFORM_FRONTEND_RELEASE", "p0-s5-01"
        ),
        frontend_origin=os.environ.get(
            "PLATFORM_FRONTEND_ORIGIN", "http://127.0.0.1:4173"
        ),
    )
)
