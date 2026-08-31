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


def test_acceptance4_uses_reference_bellhop_asset_end_to_end(
    tmp_path: Path,
) -> None:
    async def run() -> None:
        worker = Path(os.environ["PLATFORM_SIM_WORKER_PATH"])
        builder = Path(os.environ["PLATFORM_REFERENCE_ENVIRONMENT_BUILDER_PATH"])
        assets = Path(os.environ["PLATFORM_REFERENCE_ENVIRONMENT_ASSET_ROOT"])
        repository = tmp_path / "environment-repository"
        repository.mkdir()
        subprocess.run([builder, assets, repository], check=True)

        app = create_app(
            BackendSettings(
                worker,
                repository,
                resource_catalog_adapter=Path(
                    os.environ["PLATFORM_RESOURCE_CATALOG_ADAPTER_PATH"]
                ),
                acceptance_environment_asset_id="reference-shallow-water-v1",
            ),
            id_factory=lambda: "reference-environment-run",
        )
        async with httpx.AsyncClient(
            transport=httpx.ASGITransport(app=app), base_url="http://test"
        ) as client:
            environment = await client.get(
                "/environments/reference-shallow-water-v1"
            )
            assert environment.status_code == 200
            resource = environment.json()
            assert resource["provenance"]["producer"] == "BellhopRawImport"
            assert resource["checksum"] == {
                "algorithm": "FNV1A64",
                "value": "fb64e543f9042c52",
            }
            assert resource["cell_count"] == "650"
            assert resource["signal_cell_count"] == "625"
            assert resource["no_arrival_cell_count"] == "25"

            created = await client.post(
                "/runs",
                json={
                    "experiment_id": "acceptance4-experiment",
                    "experiment_version": "1",
                },
            )
            assert created.status_code == 201
            terminal = None
            for _ in range(400):
                response = await client.get("/runs/reference-environment-run")
                terminal = response.json()
                if terminal["lifecycle"] in {"Completed", "Failed"}:
                    break
                await asyncio.sleep(0.01)
            assert terminal is not None
            assert terminal["lifecycle"] == "Completed", terminal
            assert terminal["environment_asset_id"] == (
                "reference-shallow-water-v1"
            )

            result = (
                await client.get("/runs/reference-environment-run/results")
            ).json()
            assert result["projection"]["node_count"] == "4"
            assert result["projection"]["cycle_count"] == "2"
            assert result["environment_asset_id"] == (
                "reference-shallow-water-v1"
            )
            assert result["acceptance_report"] is not None

            evidence = (
                await client.get(
                    "/runs/reference-environment-run/acceptance-evidence"
                )
            ).json()
            assert evidence["manifest"]["environment"]["environment_asset_id"] == (
                "reference-shallow-water-v1"
            )
            assert evidence["manifest"]["environment"]["checksum"]["value"] == (
                "fb64e543f9042c52"
            )
            assert evidence["semantics"]["environment_evidence"] == (
                "Reference / modeled"
            )
            assert evidence["semantics"]["propagation_evidence"] == (
                "Bellhop-derived"
            )
            assert evidence["semantics"]["ber_evidence_source"] == "Modeled"

    asyncio.run(run())
