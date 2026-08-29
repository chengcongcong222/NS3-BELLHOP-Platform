from __future__ import annotations

import asyncio
import os
import subprocess
from pathlib import Path

import httpx
import pytest

from ns3_factory_backend.api import BackendSettings, create_app


pytestmark = pytest.mark.skipif(
    "PLATFORM_SIM_WORKER_PATH" not in os.environ,
    reason="the real worker is available only in the ns-3 ON build",
)


def test_acceptance4_node_runs_through_http_and_cpp_worker(tmp_path: Path) -> None:
    async def run() -> None:
        worker = Path(os.environ["PLATFORM_SIM_WORKER_PATH"])
        builder = Path(os.environ["PLATFORM_WORKER_TEST_ASSET_BUILDER_PATH"])
        repository = tmp_path / "environment-repository"
        subprocess.run([builder, repository], check=True)

        app = create_app(
            BackendSettings(worker, repository),
            id_factory=lambda: "http-acceptance4-run",
        )
        async with httpx.AsyncClient(
            transport=httpx.ASGITransport(app=app), base_url="http://test"
        ) as client:
            health = await client.get("/health")
            assert health.json()["worker_executable_ready"] is True
            created = await client.post(
                "/runs",
                json={
                    "environment_asset_id": "backend-field-v1",
                    "environment_format_version": "1",
                    "simulation_cycle_count": "2",
                    "rx_quality_mode": "ModeledBpskAwgn",
                    "equivalent_noise_power_db_re_1upa2": 45.0,
                    "deterministic_seed": "19",
                },
            )
            assert created.status_code == 201
            assert created.json()["lifecycle"] == "Created"

            terminal = None
            for _ in range(400):
                response = await client.get("/runs/http-acceptance4-run")
                terminal = response.json()
                if terminal["lifecycle"] in {"Completed", "Failed"}:
                    break
                await asyncio.sleep(0.01)
            assert terminal is not None
            assert terminal["lifecycle"] == "Completed", terminal

            result = await client.get("/runs/http-acceptance4-run/results")
            assert result.status_code == 200
            body = result.json()
            assert body["run_id"] == "http-acceptance4-run"
            assert body["projection"]["node_count"] == "4"
            assert body["projection"]["cycle_count"] == "2"

            events = await client.get("/runs/http-acceptance4-run/events")
            assert events.status_code == 200
            ids = [
                int(line.removeprefix("id: "))
                for line in events.text.splitlines()
                if line.startswith("id: ")
            ]
            assert ids
            assert ids == list(range(1, len(ids) + 1))

    asyncio.run(run())
