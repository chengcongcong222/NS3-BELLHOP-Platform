from __future__ import annotations

import builtins
from concurrent.futures import ThreadPoolExecutor
from copy import deepcopy
import json
from pathlib import Path
from threading import Event
from unittest.mock import patch

import pytest

from ns3_factory_backend.authoring import OceanAcquisitionRequest, acquire_woa
from ns3_factory_backend.ocean_cache import (
    get_hydrology_profile, list_hydrology_profiles, store_hydrology_profile,
    woa_selection,
)


PROFILE = {"provider": "NOAA WOA23", "source": "online-real-data", "month": 4,
           "requested_coordinate": {"latitude_deg": 18.0, "longitude_deg": 110.0},
           "grid_coordinate": {"latitude_deg": 17.5, "longitude_deg": 109.5},
           "maximum_depth_meters": 200.0, "resolution_degrees": 1,
           "sound_speed_profile": [
               {"depth_meters": 0.0, "speed_meters_per_second": 1521.4},
               {"depth_meters": 100.0, "speed_meters_per_second": 1514.8},
           ]}


def request(**changes) -> OceanAcquisitionRequest:
    return OceanAcquisitionRequest(**{
        "latitude_deg": 18, "longitude_deg": 110, "month": 4,
        "maximum_depth_meters": 200, **changes,
    })


def test_profile_store_deduplicates_exact_content_and_preserves_provenance(tmp_path: Path):
    selection = woa_selection(request().model_dump())
    original = deepcopy(selection)
    first = store_hydrology_profile(tmp_path, {**PROFILE, "cache_files_reused": 2}, selection=selection)
    second = store_hydrology_profile(tmp_path, {**PROFILE, "result_cache_reused": True}, selection=selection)
    assert first == second and len(list(tmp_path.glob("*.json"))) == 1
    selection["month"] = 8
    assert first["selection"] == original
    assert first["result"] == PROFILE
    assert get_hydrology_profile(tmp_path, first["cache_id"]) == first
    summary = list_hydrology_profiles(tmp_path)[0]
    assert "result" not in summary
    assert summary["source"] == "online-real-data" and summary["provider"] == "NOAA WOA23"
    assert summary["point_count"] == 2 and summary["maximum_depth_meters"] == 100
    assert summary["selection"]["maximum_depth_meters"] == 200


def test_imported_profile_has_filename_but_no_invented_coordinate_or_month(tmp_path: Path):
    imported = {"source": "file", "filename": "海试声速.csv", "point_count": 2,
                "sound_speed_profile": PROFILE["sound_speed_profile"]}
    record = store_hydrology_profile(tmp_path, imported)
    loaded = get_hydrology_profile(tmp_path, record["cache_id"])
    assert loaded["selection"] is None and loaded["provider"] is None
    assert loaded["filename"] == "海试声速.csv"
    assert loaded["result"] == imported
    assert "month" not in loaded["result"] and "requested_coordinate" not in loaded["result"]


def test_exact_query_cache_is_read_before_scientific_imports_or_network(tmp_path: Path):
    selection = woa_selection(request().model_dump())
    saved = store_hydrology_profile(tmp_path / "profiles", PROFILE, selection=selection)
    original_import = builtins.__import__

    def without_science(name, *args, **kwargs):
        if name in {"numpy", "netCDF4"}:
            raise ImportError("test deliberately has no scientific reader")
        return original_import(name, *args, **kwargs)

    progress = []
    with patch("builtins.__import__", side_effect=without_science), patch(
            "urllib.request.urlopen", side_effect=AssertionError("must stay offline")):
        result = acquire_woa(request(), tmp_path, tmp_path / "unused-reference", progress.append)
    assert result["cache_id"] == saved["cache_id"] and result["result_cache_reused"] is True
    assert result["sound_speed_profile"] == PROFILE["sound_speed_profile"]
    assert result["source"] == "online-real-data" and result["cache_files_reused"] == 0
    assert progress[-1]["stage"] == "cache"


@pytest.mark.parametrize("changes", [
    {"latitude_deg": 18.1}, {"longitude_deg": 110.1}, {"month": 5},
    {"month": None}, {"maximum_depth_meters": 100}, {"use_reference_cache": True},
])
def test_changed_query_never_borrows_another_location_month_depth_or_source(tmp_path: Path, changes):
    store_hydrology_profile(tmp_path / "profiles", PROFILE,
                            selection=woa_selection(request().model_dump()))
    with patch("ns3_factory_backend.authoring._acquire_woa_uncached",
               side_effect=RuntimeError("explicit extraction required")) as extract:
        with pytest.raises(RuntimeError, match="explicit extraction required"):
            acquire_woa(request(**changes), tmp_path, tmp_path)
    extract.assert_called_once()


def test_repeated_concurrent_queries_extract_once_then_read_same_saved_result(tmp_path: Path):
    started, release = Event(), Event()

    def extract(*_args):
        started.set()
        assert release.wait(2)
        return deepcopy(PROFILE)

    with patch("ns3_factory_backend.authoring._acquire_woa_uncached", side_effect=extract) as operation:
        with ThreadPoolExecutor(max_workers=6) as pool:
            futures = [pool.submit(acquire_woa, request(), tmp_path, tmp_path) for _ in range(6)]
            assert started.wait(2)
            release.set()
            results = [future.result(timeout=3) for future in futures]
    operation.assert_called_once()
    assert len({result["cache_id"] for result in results}) == 1
    assert sum(result["result_cache_reused"] for result in results) == 5
    assert len(list_hydrology_profiles(tmp_path / "profiles")) == 1


def test_content_change_produces_new_identity_and_corruption_is_not_offered(tmp_path: Path):
    one = store_hydrology_profile(tmp_path, PROFILE)
    changed = deepcopy(PROFILE)
    changed["sound_speed_profile"][0]["speed_meters_per_second"] = 1499
    two = store_hydrology_profile(tmp_path, changed)
    assert one["cache_id"] != two["cache_id"]
    damaged = tmp_path / f"{one['cache_id']}.json"
    raw = json.loads(damaged.read_text())
    raw["result"]["month"] = 7
    damaged.write_text(json.dumps(raw))
    with pytest.raises(ValueError, match="校验失败"):
        get_hydrology_profile(tmp_path, one["cache_id"])
    assert [record["cache_id"] for record in list_hydrology_profiles(tmp_path)] == [two["cache_id"]]
    assert damaged.exists()  # Never delete a damaged user file.


def test_corrupt_timestamp_cannot_break_listing_other_profiles(tmp_path: Path):
    saved = store_hydrology_profile(tmp_path, PROFILE)
    path = tmp_path / f"{saved['cache_id']}.json"
    raw = json.loads(path.read_text())
    raw["created_at"] = 123
    path.write_text(json.dumps(raw))
    with pytest.raises(ValueError, match="保存时间无效"):
        get_hydrology_profile(tmp_path, saved["cache_id"])
    assert list_hydrology_profiles(tmp_path) == []
    assert path.is_file()


@pytest.mark.parametrize("cache_id", ["../outside", "/tmp/profile", "a" * 63, "g" * 64])
def test_cache_identity_is_never_a_filesystem_path(tmp_path: Path, cache_id: str):
    with pytest.raises(ValueError, match="编号无效"):
        get_hydrology_profile(tmp_path, cache_id)


@pytest.mark.parametrize("point", [
    {"depth_meters": float("nan"), "speed_meters_per_second": 1500},
    {"depth_meters": 100, "speed_meters_per_second": -1},
    {"depth_meters": 0, "speed_meters_per_second": 1500},
    {"depth_meters": True, "speed_meters_per_second": 1500},
])
def test_invalid_profile_is_not_persisted(tmp_path: Path, point):
    invalid = deepcopy(PROFILE)
    invalid["sound_speed_profile"][1] = point
    with pytest.raises(ValueError):
        store_hydrology_profile(tmp_path, invalid)
    assert list(tmp_path.iterdir()) == []


def test_absent_cache_keeps_dependency_failure_explicit_without_reference_fallback(tmp_path: Path):
    original_import = builtins.__import__

    def without_science(name, *args, **kwargs):
        if name in {"numpy", "netCDF4"}:
            raise ImportError("deliberately unavailable")
        return original_import(name, *args, **kwargs)

    with patch("builtins.__import__", side_effect=without_science), patch(
            "urllib.request.urlopen", side_effect=AssertionError("no network")):
        with pytest.raises(RuntimeError, match="可选依赖"):
            acquire_woa(request(), tmp_path, tmp_path)
    assert not list(tmp_path.glob("profiles/*.json"))
