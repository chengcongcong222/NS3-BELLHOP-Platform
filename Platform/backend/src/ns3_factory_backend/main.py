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
    )
)
