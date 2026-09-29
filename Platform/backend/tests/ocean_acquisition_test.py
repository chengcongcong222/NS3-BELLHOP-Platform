from __future__ import annotations

import asyncio
import io
import json
from pathlib import Path
from threading import Event
from unittest.mock import patch

import httpx
import pytest

from ns3_factory_backend.acquisition_stream import acquisition_stream
from ns3_factory_backend.ocean_download import download_dataset
from ns3_factory_backend.api import BackendSettings, create_app
from helpers import resource_catalog
from authoring_api_test import IdleGateway


class Download(io.BytesIO):
    def __init__(self, data: bytes, length: int):
        super().__init__(data)
        self.headers = {"Content-Length": str(length)}


def test_complete_download_has_real_byte_progress_and_atomic_cache(tmp_path: Path):
    destination = tmp_path / "month.nc"
    progress = []
    with patch("urllib.request.urlopen", return_value=Download(b"test-only-data", 14)):
        download_dataset("https://test.invalid", destination, progress.append)
    assert destination.read_bytes() == b"test-only-data"
    assert progress[-1]["received_bytes"] == progress[-1]["total_bytes"] == 14
    assert not list(tmp_path.glob("*.partial"))


def test_incomplete_download_never_becomes_cache_and_retry_is_safe(tmp_path: Path):
    destination = tmp_path / "month.nc"
    with patch("urllib.request.urlopen", return_value=Download(b"partial", 100)):
        with pytest.raises(ValueError, match="下载不完整"):
            download_dataset("https://test.invalid", destination, lambda _p: None)
    assert not destination.exists()
    partials = list(tmp_path.glob("*.partial"))
    assert len(partials) == 1 and partials[0].read_bytes() == b"partial"
    with patch("urllib.request.urlopen", return_value=Download(b"complete", 8)):
        download_dataset("https://test.invalid", destination, lambda _p: None)
    assert destination.read_bytes() == b"complete"


def test_timeout_is_explicit_and_no_fallback_file_created(tmp_path: Path):
    with patch("urllib.request.urlopen", side_effect=TimeoutError("test timeout")):
        with pytest.raises(RuntimeError, match="连接或下载超时"):
            download_dataset("https://test.invalid", tmp_path / "month.nc", lambda _p: None)
    assert not (tmp_path / "month.nc").exists()


def test_total_download_deadline_is_bounded(tmp_path: Path):
    with patch("urllib.request.urlopen", return_value=Download(b"data", 4)), patch(
            "ns3_factory_backend.ocean_download.time.monotonic", side_effect=[0, 601]):
        with pytest.raises(RuntimeError, match="超过 10 分钟"):
            download_dataset("https://test.invalid", tmp_path / "month.nc", lambda _p: None)
    assert not (tmp_path / "month.nc").exists()


def test_stream_delivers_progress_then_failure_not_a_reference_result():
    def failing(report):
        report({"stage": "connecting", "message": "connecting"})
        raise RuntimeError("NOAA unavailable")
    async def run():
        events = [json.loads(line) async for line in acquisition_stream(asyncio.Lock(), failing)]
        assert [e["type"] for e in events] == ["progress", "progress", "error"]
        assert events[-1]["message"] == "NOAA unavailable"
    asyncio.run(run())


def test_cancelled_stream_stops_its_reader_before_releasing_the_acquisition_lock():
    stopped = Event()
    def operation(report):
        try:
            for _ in range(100):
                report({"stage": "downloading", "message": "test progress"})
                stopped.wait(0.01)
        finally:
            stopped.set()
        return {"sound_speed_profile": []}
    async def run():
        lock = asyncio.Lock()
        stream = acquisition_stream(lock, operation)
        assert json.loads(await anext(stream))["stage"] == "queued"
        assert json.loads(await anext(stream))["stage"] == "downloading"
        assert lock.locked()
        await asyncio.wait_for(stream.aclose(), timeout=2)
        assert stopped.is_set() and not lock.locked()
    asyncio.run(run())


def test_real_reference_stream_completes_and_invalid_coordinate_reports_error(tmp_path: Path):
    async def run():
        app = create_app(BackendSettings(worker_executable=tmp_path / "worker",
                         environment_repository_root=tmp_path / "repo", authoring_workspace_root=tmp_path),
                         gateway=IdleGateway(), resource_catalog=resource_catalog())
        async with httpx.AsyncClient(transport=httpx.ASGITransport(app=app), base_url="http://test") as client:
            request = {"latitude_deg": 18, "longitude_deg": 110, "month": 4, "use_reference_cache": True}
            response = await client.post("/authoring/acquisition/woa/stream", json=request)
            events = [json.loads(line) for line in response.text.splitlines()]
            assert events[0]["stage"] == "queued"
            assert events[-1]["type"] == "result"
            assert len(events[-1]["result"]["sound_speed_profile"]) >= 2
            request["longitude_deg"] = 150
            failed = await client.post("/authoring/acquisition/woa/stream", json=request)
            assert json.loads(failed.text.splitlines()[-1])["type"] == "error"
    asyncio.run(run())


def test_existing_truncated_netcdf_is_preserved_and_replaced_by_a_complete_download(tmp_path: Path):
    netcdf = pytest.importorskip("netCDF4")
    from ns3_factory_backend.authoring import OceanAcquisitionRequest, acquire_woa
    bad = tmp_path / "woa23_decav_t04_01.nc"
    bad.write_bytes(b"interrupted cache")
    for variable in ("t", "s"):
        path = tmp_path / f"source-{variable}.nc"
        with netcdf.Dataset(str(path), "w") as dataset:
            for name, size in (("time", 1), ("depth", 2), ("lat", 1), ("lon", 1)):
                dataset.createDimension(name, size)
            dataset.createVariable("lat", "f8", ("lat",))[:] = [17.5]
            dataset.createVariable("lon", "f8", ("lon",))[:] = [109.5]
            dataset.createVariable("depth", "f8", ("depth",))[:] = [0, 100]
            dataset.createVariable(f"{variable}_an", "f8", ("time", "depth", "lat", "lon"))[:] = 20 if variable == "t" else 35
    responses = [Download((tmp_path / f"source-{v}.nc").read_bytes(), (tmp_path / f"source-{v}.nc").stat().st_size) for v in ("t", "s")]
    progress = []
    request = OceanAcquisitionRequest(latitude_deg=18, longitude_deg=110, month=4)
    with patch("urllib.request.urlopen", side_effect=responses) as download:
        one = acquire_woa(request, tmp_path, tmp_path, progress.append)
        two = acquire_woa(request, tmp_path, tmp_path, progress.append)
    assert download.call_count == 2
    assert one["sound_speed_profile"] == two["sound_speed_profile"]
    assert two["cache_files_reused"] == 2
    assert any(p["stage"] == "repair" for p in progress)
    assert next(tmp_path.glob("*.invalid")).read_bytes() == b"interrupted cache"


def test_changed_scene_revision_is_rejected_before_starting_worker(tmp_path: Path):
    async def run():
        app = create_app(BackendSettings(worker_executable=tmp_path / "worker",
                         environment_repository_root=tmp_path / "repo", authoring_workspace_root=tmp_path),
                         gateway=IdleGateway(), resource_catalog=resource_catalog())
        async with httpx.AsyncClient(transport=httpx.ASGITransport(app=app), base_url="http://test") as client:
            scene = (await client.post("/authoring/drafts", json={"kind": "scenario", "name": "Scene", "payload": {}})).json()
            experiment = (await client.post("/authoring/drafts", json={"kind": "experiment", "name": "Experiment", "payload": {
                "scenario_id": scene["draft_id"], "scenario_revision": 1, "bit_rate_bits_per_second": "60",
                "simulation_cycle_count": "1", "deterministic_seed": "1"}})).json()
            await client.put(f"/authoring/drafts/scenario/{scene['draft_id']}", headers={"If-Match": "1"},
                             json={"kind": "scenario", "name": "Changed scene", "payload": {}})
            response = await client.post(f"/authoring/experiments/{experiment['draft_id']}/runs", headers={"If-Match": "1"})
            assert response.status_code == 409 and response.json()["error"]["code"] == "DraftConflict"
            assert (await client.get("/runs")).json() == []
    asyncio.run(run())
