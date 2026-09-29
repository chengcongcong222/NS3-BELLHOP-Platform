from __future__ import annotations

import asyncio
import os
from pathlib import Path

import httpx

from ns3_factory_backend.api import BackendSettings, create_app
from ns3_factory_backend.authoring import _reference_data
from helpers import failure_result, resource_catalog


class IdleGateway:
    async def shutdown(self) -> None:
        pass


class CapturingGateway:
    def __init__(self) -> None:
        self.command = None
        self.called = asyncio.Event()

    async def run(self, command, _callback):
        self.command = command
        self.called.set()
        return failure_result(command.run_id)

    async def shutdown(self) -> None:
        pass


def test_server_workspace_import_build_and_immutable_publish(tmp_path: Path) -> None:
    async def run() -> None:
        app = create_app(
            BackendSettings(
                worker_executable=tmp_path / "worker",
                environment_repository_root=tmp_path / "repository",
                authoring_workspace_root=tmp_path / "workspace",
                environment_asset_publisher_executable=Path(
                    os.environ["PLATFORM_ENVIRONMENT_ASSET_PUBLISHER_PATH"]),
            ),
            gateway=IdleGateway(),
            resource_catalog=resource_catalog(),
        )
        async with httpx.AsyncClient(
            transport=httpx.ASGITransport(app=app), base_url="http://test"
        ) as client:
            hydrology = await client.post(
                "/authoring/import/ssp",
                json={"filename": "profile.csv", "content": "depth_m,speed\n0,1500\n100,1490\n"},
            )
            terrain = await client.post(
                "/authoring/import/terrain",
                json={"filename": "terrain.json", "content": '{"range_m":[0,1000],"depth_m":[100,110]}'},
            )
            assert hydrology.status_code == 200
            assert terrain.status_code == 200
            reference_ssp, reference_terrain = _reference_data(Path(__file__).resolve().parents[2] / "environment/assets/reference_shallow_water_v1")
            payload = {
                "center_frequency_hz": 25000,
                "sound_speed_profile": reference_ssp,
                "bathymetry": reference_terrain,
                "build_source": "reference-bellhop-arrivals",
            }
            created = await client.post(
                "/authoring/drafts",
                json={"kind": "environment", "name": "Reference rebase", "payload": payload},
            )
            assert created.status_code == 201
            assert created.json()["validation_issues"] == []
            draft_id = created.json()["draft_id"]

            conflict = await client.put(
                f"/authoring/drafts/environment/{draft_id}",
                headers={"If-Match": "99"},
                json={"kind": "environment", "name": "Reference rebase", "payload": payload},
            )
            assert conflict.status_code == 409

            build = await client.post(
                "/authoring/environment-builds", json={"draft_id": draft_id}
            )
            assert build.status_code == 202
            assert build.json()["state"] == "Completed"
            assert all(step["state"] == "Completed" for step in build.json()["steps"])
            assert build.json()["artifact"]["build_kind"] == "BellhopOffline"

            build_id = build.json()["build_id"]
            published = await client.post(
                f"/authoring/environment-builds/{build_id}/publish"
            )
            assert published.status_code == 200
            assert published.json()["environment_asset_id"].endswith("-r1")
            assert published.json()["runtime_catalog_available"] is True
            repeated = await client.post(
                f"/authoring/environment-builds/{build_id}/publish"
            )
            assert repeated.status_code == 200

    asyncio.run(run())


def test_reference_woa_and_terrain_acquisition_are_real_cached_sources(tmp_path: Path) -> None:
    async def run() -> None:
        app = create_app(
            BackendSettings(
                worker_executable=tmp_path / "worker",
                environment_repository_root=tmp_path / "repository",
                authoring_workspace_root=tmp_path / "workspace",
            ),
            gateway=IdleGateway(),
            resource_catalog=resource_catalog(),
        )
        async with httpx.AsyncClient(
            transport=httpx.ASGITransport(app=app), base_url="http://test"
        ) as client:
            woa = await client.post(
                "/authoring/acquisition/woa",
                json={"latitude_deg": 18, "longitude_deg": 110, "month": 4, "use_reference_cache": True},
            )
            gebco = await client.post(
                "/authoring/acquisition/terrain",
                json={"latitude_deg": 18, "longitude_deg": 110, "use_reference_cache": True},
            )
            assert woa.status_code == 200
            assert woa.json()["source"] == "cached-real-data"
            assert len(woa.json()["sound_speed_profile"]) >= 2
            assert gebco.status_code == 200
            assert len(gebco.json()["bathymetry"]) >= 2

    asyncio.run(run())


def test_channel2_authoring_selection_is_projected_to_worker_command(tmp_path: Path) -> None:
    async def run() -> None:
        gateway = CapturingGateway()
        app = create_app(
            BackendSettings(
                worker_executable=tmp_path / "worker",
                environment_repository_root=tmp_path / "repository",
                authoring_workspace_root=tmp_path / "workspace",
            ),
            gateway=gateway,
            id_factory=lambda: "authoring-channel2-run",
            resource_catalog=resource_catalog(),
        )
        async with httpx.AsyncClient(
            transport=httpx.ASGITransport(app=app), base_url="http://test"
        ) as client:
            created = await client.post("/authoring/drafts", json={
                "kind": "experiment",
                "name": "Waveform experiment",
                "payload": {
                    "scenario_id": "acceptance4-scenario",
                    "bit_rate_bits_per_second": "60",
                    "simulation_cycle_count": "2",
                    "deterministic_seed": "17",
                    "physical_layer": "Channel2Waveform",
                    "waveform_modulation": "Bfsk",
                    "waveform_noise": "Awgn",
                    "waveform_noise_psd_db_re_1upa2_per_hz": 75.0,
                },
            })
            assert created.status_code == 201
            draft_id = created.json()["draft_id"]
            launched = await client.post(
                f"/authoring/experiments/{draft_id}/runs")
            assert launched.status_code == 201
            await asyncio.wait_for(gateway.called.wait(), 1)
            assert gateway.command.execution.physical_layer == "Channel2Waveform"
            assert gateway.command.execution.rx_quality_mode == "None"
            assert gateway.command.execution.waveform_modulation == "Bfsk"
            assert gateway.command.execution.waveform_noise == "Awgn"
            assert gateway.command.execution.waveform_noise_psd_db_re_1upa2_per_hz == 75.0
            assert gateway.command.execution.deterministic_seed == "17"

    asyncio.run(run())
