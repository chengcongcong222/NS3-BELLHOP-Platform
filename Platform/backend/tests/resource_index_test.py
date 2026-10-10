from __future__ import annotations

import asyncio
import json
import os
import subprocess
import threading
from pathlib import Path
from types import SimpleNamespace

import httpx
import pytest

from ns3_factory_backend.api import BackendSettings, create_app
from ns3_factory_backend.resource_index import VerifiedResourceIndex, repository_stamp
from ns3_factory_backend.resources import ResourceCatalog, ResourceCatalogConfigurationError, inspect_environment
from ns3_factory_backend.authoring import AuthoringStore
from helpers import resource_catalog


def test_warm_index_single_flight_invalidates_add_change_delete_and_no_stale_fallback(tmp_path):
    calls = []
    catalog = resource_catalog()
    def load():
        calls.append(1)
        if (tmp_path / "bad").exists():
            raise ResourceCatalogConfigurationError("bad asset")
        return catalog
    index = VerifiedResourceIndex(tmp_path, catalog, repository_stamp(tmp_path), load)

    async def check():
        assert await index.current() is catalog
        assert not calls
        package = tmp_path / "new"
        package.mkdir()
        field = package / "field.bin"
        field.write_bytes(b"first")
        assert all(item is catalog for item in await asyncio.gather(*(index.current() for _ in range(12))))
        assert len(calls) == 1
        field.write_bytes(b"other")  # same length, new mtime/ctime
        await index.current()
        assert len(calls) == 2
        field.unlink()
        await index.current()
        assert len(calls) == 3
        (tmp_path / "bad").mkdir()
        for _ in range(3):
            with pytest.raises(ResourceCatalogConfigurationError):
                await index.current()
        assert len(calls) == 4  # bad stamp is not retried by every browser request
        (tmp_path / "bad").rmdir()
        assert await index.current() is catalog
        assert len(calls) == 5
    asyncio.run(check())


def test_refresh_rejects_repository_changed_during_validation(tmp_path):
    catalog = resource_catalog()
    def load():
        (tmp_path / "changed").write_text("modified during validation")
        return catalog
    index = VerifiedResourceIndex(tmp_path, catalog, repository_stamp(tmp_path), load)
    (tmp_path / "new").mkdir()
    with pytest.raises(ResourceCatalogConfigurationError, match="during validation"):
        asyncio.run(index.current())


def test_slow_catalog_refresh_does_not_block_other_http_routes(tmp_path, monkeypatch):
    entered, release = threading.Event(), threading.Event()
    calls = []
    catalog = resource_catalog()
    def load(*_args):
        calls.append(1)
        entered.set()
        assert release.wait(5)
        return catalog
    monkeypatch.setattr(ResourceCatalog, "from_adapter", load)
    app = create_app(BackendSettings(Path("/missing/worker"), tmp_path,
        resource_catalog_adapter=Path("/test/adapter")), resource_catalog=catalog)
    (tmp_path / "new").mkdir()
    async def check():
        async with httpx.AsyncClient(transport=httpx.ASGITransport(app=app), base_url="http://test") as client:
            pending = [asyncio.create_task(client.get("/environments")) for _ in range(6)]
            try:
                assert await asyncio.to_thread(entered.wait, 2)
                for route in ("/system/info", "/runs"):
                    assert (await asyncio.wait_for(client.get(route), .5)).status_code == 200
                assert len(calls) == 1
            finally:
                release.set()
            assert all(response.status_code == 200 for response in await asyncio.gather(*pending))
            assert len(calls) == 1
    asyncio.run(check())


def test_invalid_catalog_http_response_is_explicit_and_never_serves_old_data(tmp_path, monkeypatch):
    def rejected(*_args):
        raise ResourceCatalogConfigurationError("invalid package")
    monkeypatch.setattr(ResourceCatalog, "from_adapter", rejected)
    app = create_app(BackendSettings(Path("/missing/worker"), tmp_path,
        resource_catalog_adapter=Path("/test/adapter")), resource_catalog=resource_catalog())
    (tmp_path / "corrupt").mkdir()
    async def check():
        async with httpx.AsyncClient(transport=httpx.ASGITransport(app=app), base_url="http://test") as client:
            response = await client.get("/environments")
            assert response.status_code == 503
            assert response.json()["error"]["code"] == "EnvironmentUnavailable"
            assert (await client.get("/system/info")).status_code == 200
    asyncio.run(check())


def test_batch_summaries_only_expose_saved_display_inputs(tmp_path):
    root = tmp_path / "workspace"
    (root / "published").mkdir(parents=True)
    (root / "published/backend-field-v1.json").write_text(json.dumps({"source_snapshot": {
        "name": "真实海域", "revision": 7, "payload": {"maximum_depth_meters": 175,
            "acquisition": {"latitudeDeg": 18, "longitudeDeg": 110, "month": 4},
            "sound_speed_profile": [{"depth_meters": 0, "speed_meters_per_second": 1500}]}}}))
    app = create_app(BackendSettings(Path("/missing/worker"), tmp_path,
        authoring_workspace_root=root), resource_catalog=resource_catalog())
    async def check():
        async with httpx.AsyncClient(transport=httpx.ASGITransport(app=app), base_url="http://test") as client:
            response = await client.get("/environments/authoring-summaries")
            assert response.status_code == 200
            assert response.json() == {"backend-field-v1": {"available": True,
                "name": "真实海域", "latitude": 18, "longitude": 110, "month": 4, "water_depth_m": 175}}
    asyncio.run(check())


@pytest.mark.skipif("PLATFORM_RESOURCE_CATALOG_ADAPTER_PATH" not in os.environ or
                   "PLATFORM_WORKER_TEST_ASSET_BUILDER_PATH" not in os.environ,
                   reason="requires real C++ authority tools")
def test_exact_cpp_inspection_validates_selected_package_not_entire_catalog(tmp_path):
    adapter = Path(os.environ["PLATFORM_RESOURCE_CATALOG_ADAPTER_PATH"])
    subprocess.run([os.environ["PLATFORM_WORKER_TEST_ASSET_BUILDER_PATH"], str(tmp_path), "selected"], check=True)
    selected = inspect_environment(adapter, tmp_path, "selected")
    assert selected.validation_state == "Valid"
    (tmp_path / "unrelated-invalid").mkdir()
    assert inspect_environment(adapter, tmp_path, "selected") == selected
    with pytest.raises(ResourceCatalogConfigurationError):
        inspect_environment(adapter, tmp_path, "missing")
    field = tmp_path / "selected/field.bin"
    field.write_bytes(field.read_bytes()[:-1])
    with pytest.raises(ResourceCatalogConfigurationError):
        inspect_environment(adapter, tmp_path, "selected")


def test_run_start_revalidates_exact_package_even_when_index_is_warm(tmp_path, monkeypatch):
    calls = []
    def rejected(_executable, _root, asset_id):
        calls.append(asset_id)
        raise ResourceCatalogConfigurationError("selected package invalid")
    monkeypatch.setattr("ns3_factory_backend.api.inspect_environment", rejected)
    app = create_app(BackendSettings(Path("/missing/worker"), tmp_path,
        resource_catalog_adapter=Path("/test/adapter")), resource_catalog=resource_catalog())
    async def check():
        async with httpx.AsyncClient(transport=httpx.ASGITransport(app=app), base_url="http://test") as client:
            assert (await client.get("/environments")).status_code == 200
            response = await client.post("/runs", json={"experiment_id": "acceptance4-experiment", "experiment_version": "1"})
            assert response.status_code == 503
            assert calls == ["backend-field-v1"]
            assert (await client.get("/runs")).json() == []
    asyncio.run(check())


def test_large_publication_is_off_loop_and_repeated_click_is_busy(tmp_path, monkeypatch):
    entered, release = threading.Event(), threading.Event()
    (tmp_path / "built").mkdir()
    artifact = {"environment_asset_id": "built", "arrivals_path": "large.arr", "arrivals_sha256": "checked"}
    def checksum(_path):
        entered.set()
        assert release.wait(5)
        return "checked"
    monkeypatch.setattr("ns3_factory_backend.api.file_sha256", checksum)
    monkeypatch.setattr(AuthoringStore, "get_build", lambda *_: SimpleNamespace(state="Completed", artifact=artifact))
    monkeypatch.setattr(AuthoringStore, "publish_build", lambda *_: artifact)
    app = create_app(BackendSettings(Path("/missing/worker"), tmp_path), resource_catalog=resource_catalog())
    async def check():
        async with httpx.AsyncClient(transport=httpx.ASGITransport(app=app), base_url="http://test") as client:
            publishing = asyncio.create_task(client.post("/authoring/environment-builds/large/publish"))
            try:
                assert await asyncio.to_thread(entered.wait, 2)
                assert (await asyncio.wait_for(client.get("/system/info"), .5)).status_code == 200
                repeated = await asyncio.wait_for(client.post("/authoring/environment-builds/large/publish"), .5)
                assert repeated.json()["error"]["code"] == "BackendBusy"
            finally:
                release.set()
            assert (await publishing).status_code == 200
    asyncio.run(check())
