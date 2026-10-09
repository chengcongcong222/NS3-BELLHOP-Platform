from __future__ import annotations

import asyncio
import json
from pathlib import Path
from unittest.mock import patch

import httpx
import pytest

from ns3_factory_backend.api import BackendSettings, create_app
from ns3_factory_backend.authoring import AuthoringStore, DraftWrite
from ns3_factory_backend.ocean_cache import store_hydrology_profile, woa_selection
from authoring_api_test import IdleGateway
from helpers import resource_catalog


POINTS = [{"depth_meters": 0.0, "speed_meters_per_second": 1500.0},
          {"depth_meters": 100.0, "speed_meters_per_second": 1490.0}]
QUERY = {"latitude_deg": 23.2, "longitude_deg": 119.7, "month": 9,
         "maximum_depth_meters": 200.0, "use_reference_cache": False}
LIBRARY = "/authoring/environment/hydrology-cache"


def client_for(root: Path) -> httpx.AsyncClient:
    app = create_app(BackendSettings(worker_executable=root / "worker",
                     environment_repository_root=root / "repository",
                     authoring_workspace_root=root),
                     gateway=IdleGateway(), resource_catalog=resource_catalog())
    return httpx.AsyncClient(transport=httpx.ASGITransport(app=app), base_url="http://test")


def test_import_is_deduplicated_listed_and_retrieved_without_invented_selection(tmp_path: Path):
    async def run():
        async with client_for(tmp_path) as client:
            body = {"filename": "测站-17.csv", "content": "depth_m,speed\n0,1500\n100,1490\n"}
            first = await client.post("/authoring/import/ssp", json=body)
            again = await client.post("/authoring/import/ssp", json=body)
            assert first.status_code == again.status_code == 200
            assert first.json()["cache_id"] == again.json()["cache_id"]
            summaries = (await client.get(LIBRARY)).json()
            assert len(summaries) == 1 and "result" not in summaries[0]
            assert summaries[0]["source"] == "file"
            assert summaries[0]["filename"] == "测站-17.csv"
            assert summaries[0]["selection"] is None
            assert summaries[0]["provider"] is None
            detail = await client.get(f"{LIBRARY}/{first.json()['cache_id']}")
            assert detail.status_code == 200
            assert detail.json()["result"]["sound_speed_profile"] == POINTS
            assert "month" not in detail.json()["result"]
            assert "requested_coordinate" not in detail.json()["result"]
            assert (await client.get("/runs")).json() == []
    asyncio.run(run())


def test_existing_saved_drafts_are_reusable_with_exact_original_provenance_and_unchanged_files(tmp_path: Path):
    store = AuthoringStore(tmp_path)
    imported = {"sound_speed_profile": POINTS,
                "acquisition": {"latitudeDeg": 50, "longitudeDeg": 70, "month": 1},
                "sources": {"ssp": {"source": "file", "filename": "old-cast.csv"}}}
    acquired = {"sound_speed_profile": POINTS,
                "sources": {"ssp": {"source": "online-real-data", "provider": "NOAA WOA23",
                    "month": 9, "grid_coordinate": {"latitude_deg": 23.5, "longitude_deg": 119.5},
                    "request": QUERY}}}
    for name, payload in (("以前导入的剖面", imported), ("以前获取的九月剖面", acquired)):
        store.create(DraftWrite(kind="environment", name=name, payload=payload))
    before = {path: path.read_bytes() for path in (tmp_path / "drafts").rglob("*.json")}

    async def run():
        with patch("urllib.request.urlopen", side_effect=AssertionError("reuse must stay offline")):
            async with client_for(tmp_path) as client:
                listed = await client.get(LIBRARY)
                assert listed.status_code == 200
                entries = listed.json()
                assert len(entries) == 2
                imported_entry = next(item for item in entries if item["source"] == "file")
                assert imported_entry["filename"] == "old-cast.csv"
                assert imported_entry["selection"] is None  # Unrelated editor coordinates are not provenance.
                woa_entry = next(item for item in entries if item["source"] == "online-real-data")
                assert woa_entry["selection"] == woa_selection(QUERY)
                detail = (await client.get(f"{LIBRARY}/{woa_entry['cache_id']}")).json()
                assert detail["result"]["grid_coordinate"] == acquired["sources"]["ssp"]["grid_coordinate"]
                assert detail["result"]["month"] == 9
                repeated = await client.get(LIBRARY)
                assert repeated.json() == entries
                reuse = await client.post("/authoring/acquisition/woa", json=QUERY)
                assert reuse.status_code == 200
                assert reuse.json()["result_cache_reused"] is True
                assert reuse.json()["cache_id"] == woa_entry["cache_id"]
    asyncio.run(run())
    assert before == {path: path.read_bytes() for path in before}


def test_missing_invalid_and_corrupt_cache_ids_do_not_fallback_or_delete_files(tmp_path: Path):
    root = tmp_path / "cache" / "woa23" / "profiles"
    saved = store_hydrology_profile(root, {"source": "file", "filename": "damaged.csv", "sound_speed_profile": POINTS})
    path = root / f"{saved['cache_id']}.json"
    damaged = json.loads(path.read_text())
    damaged["result"]["sound_speed_profile"][0]["speed_meters_per_second"] = 1501
    path.write_text(json.dumps(damaged))
    before = path.read_bytes()

    async def run():
        async with client_for(tmp_path) as client:
            missing = await client.get(f"{LIBRARY}/{'0' * 64}")
            assert missing.status_code == 404
            assert missing.json()["error"]["code"] == "HydrologyCacheNotFound"
            invalid = await client.get(f"{LIBRARY}/not-a-cache-id")
            assert invalid.status_code == 422
            assert invalid.json()["error"]["code"] == "InvalidHydrologyCache"
            corrupt = await client.get(f"{LIBRARY}/{saved['cache_id']}")
            assert corrupt.status_code == 422
            assert "校验失败" in corrupt.json()["error"]["message"]
            assert (await client.get(LIBRARY)).json() == []
    asyncio.run(run())
    assert path.read_bytes() == before


def test_frozen_terrain_endpoints_leave_existing_drafts_and_acquired_files_intact(tmp_path: Path):
    store = AuthoringStore(tmp_path)
    draft = store.create(DraftWrite(kind="environment", name="旧环境地形", payload={
        "sound_speed_profile": POINTS,
        "bathymetry": [{"range_meters": 0, "depth_meters": 120}, {"range_meters": 1000, "depth_meters": 130}],
        "sources": {"terrain": {"source": "file", "filename": "original-terrain.json"}},
    }))
    old_asset = tmp_path / "published-terrain.json"
    old_asset.write_text('{"original": true}')
    before = {path: path.read_bytes() for path in tmp_path.rglob("*") if path.is_file()}

    async def run():
        with patch("urllib.request.urlopen", side_effect=AssertionError("frozen endpoint must not fetch")):
            async with client_for(tmp_path) as client:
                for endpoint, payload in (
                    ("/authoring/import/terrain", {"filename": "new.csv", "content": "0,100\n1000,110\n"}),
                    ("/authoring/acquisition/terrain", {"latitude_deg": 18, "longitude_deg": 110}),
                ):
                    response = await client.post(endpoint, json=payload)
                    assert response.status_code == 410
                    assert response.json()["error"]["code"] == "TerrainFeatureFrozen"
                fetched = await client.get(f"/authoring/drafts/environment/{draft.draft_id}")
                assert fetched.status_code == 200 and fetched.json()["payload"] == draft.payload
                capabilities = (await client.get("/authoring/environment-capabilities")).json()
                assert capabilities["terrain_authoring_available"] is False
                assert capabilities["hydrology_cache_available"] is True
    asyncio.run(run())
    assert before == {path: path.read_bytes() for path in tmp_path.rglob("*") if path.is_file()}


@pytest.mark.parametrize("sources", [None, [], {"ssp": None}])
def test_malformed_legacy_source_metadata_cannot_break_profile_listing(tmp_path: Path, sources):
    AuthoringStore(tmp_path).create(DraftWrite(kind="environment", name="不完整旧草稿", payload={
        "sound_speed_profile": POINTS, "sources": sources,
    }))

    async def run():
        async with client_for(tmp_path) as client:
            response = await client.get(LIBRARY)
            assert response.status_code == 200
            records = response.json()
            assert len(records) == 1
            assert records[0]["source"] == "saved-draft"
            assert records[0]["selection"] is None
            assert records[0]["provider"] is None
            assert records[0]["filename"] == "不完整旧草稿"
    asyncio.run(run())
