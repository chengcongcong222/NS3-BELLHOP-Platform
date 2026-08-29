from __future__ import annotations

import os
from pathlib import Path

from .api import BackendSettings, create_app


app = create_app(
    BackendSettings(
        worker_executable=Path(os.environ.get("PLATFORM_SIM_WORKER", "")),
        environment_repository_root=Path(
            os.environ.get("PLATFORM_ENVIRONMENT_REPOSITORY", "")
        ),
    )
)
