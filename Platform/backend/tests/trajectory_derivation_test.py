from __future__ import annotations

import asyncio
import json
from pathlib import Path

import httpx
import pytest

from authoring_api_test import IdleGateway
from helpers import resource_catalog
from ns3_factory_backend.api import BackendSettings, create_app
from ns3_factory_backend.trajectory_authoring import Asset, Point, Store, Track, _finalize


def setup_source(tmp_path: Path):
    store = Store(tmp_path / "workspace" / "published" / "trajectories")
    source = store.save(_finalize(Asset(
        asset_id="source", version=3, name="Original test asset", source="explicit-test-fixture",
        source_notice="演示参数，不代表任意真实 UUV。",
        tracks=[Track(track_id="N1", points=[
            Point(time_s=5, x_m=10, y_m=20, z_m=-12, pitch_rad=.1, yaw_rad=.2,
                  u_mps=.3, v_mps=.4, w_mps=.5, q_radps=.6, r_radps=.7),
            Point(time_s=10, x_m=15, y_m=30, z_m=-18, pitch_rad=.2, yaw_rad=.3,
                  u_mps=.4, v_mps=.5, w_mps=.6, q_radps=.7, r_radps=.8),
        ]), Track(track_id="N2", points=[Point(time_s=5, x_m=0, y_m=0, z_m=-2),
                                           Point(time_s=10, x_m=0, y_m=0, z_m=-2)])],
    )))
    app = create_app(BackendSettings(worker_executable=tmp_path / "worker",
        environment_repository_root=tmp_path / "repo", authoring_workspace_root=tmp_path / "workspace"),
        gateway=IdleGateway(), resource_catalog=resource_catalog())
    request = {"source_asset_id": "source", "source_version": 3, "source_track_id": "N1",
               "asset_id": "placed", "name": "Placed test track", "x_m": 100, "y_m": -50, "z_m": -30}
    return store, source, app, request


def test_translate_publishes_new_asset_preserving_time_and_extended_state(tmp_path: Path):
    async def run():
        store, source, app, request = setup_source(tmp_path)
        original_path = store.root / "source.v3.json"
        original_bytes = original_path.read_bytes()
        async with httpx.AsyncClient(transport=httpx.ASGITransport(app=app), base_url="http://test") as client:
            response = await client.post("/authoring/trajectories/translate", json=request)
            assert response.status_code == 201, response.text
            actual = response.json()
            assert actual["version"] == 1
            assert actual["coordinate_system"] == "LocalCartesianMetersZUp"
            assert len(actual["tracks"]) == 1 and actual["tracks"][0]["track_id"] == "N1"
            points = actual["tracks"][0]["points"]
            assert [(p["x_m"], p["y_m"], p["z_m"]) for p in points] == [(100, -50, -30), (105, -40, -36)]
            for old, placed in zip(source.tracks[0].points, points, strict=True):
                for field, value in old.model_dump().items():
                    if field not in {"x_m", "y_m", "z_m"}:
                        assert placed[field] == value
            provenance = json.loads(actual["source"])
            assert provenance["asset_id"] == "source" and provenance["version"] == 3
            assert provenance["track_id"] == "N1"
            assert provenance["checksum_sha256"] == source.checksum_sha256
            assert provenance["translation_m"] == {"x": 90, "y": -70, "z": -18}
            assert "不代表任意真实 UUV" in actual["source_notice"]
            assert store.get("placed", 1).model_dump(mode="json") == actual
            assert original_path.read_bytes() == original_bytes
            conflict = await client.post("/authoring/trajectories/translate", json=request)
            assert conflict.status_code == 409
            assert conflict.json()["error"]["code"] == "ImmutableResourceConflict"
            assert original_path.read_bytes() == original_bytes
            assert store.get("placed", 1).model_dump(mode="json") == actual
    asyncio.run(run())


@pytest.mark.parametrize("change,code", [
    ({"source_asset_id": "missing"}, "TrajectoryNotFound"),
    ({"source_version": 99}, "TrajectoryNotFound"),
    ({"source_track_id": "missing"}, "TrajectoryTrackNotFound"),
])
def test_translate_missing_reference_never_selects_another_source(tmp_path: Path, change, code):
    async def run():
        store, _, app, request = setup_source(tmp_path)
        async with httpx.AsyncClient(transport=httpx.ASGITransport(app=app), base_url="http://test") as client:
            response = await client.post("/authoring/trajectories/translate", json={**request, **change})
            assert response.status_code == 404
            assert response.json()["error"]["code"] == code
            assert [asset.asset_id for asset in store.list()] == ["source"]
    asyncio.run(run())


@pytest.mark.parametrize("value", [float("nan"), float("inf"), float("-inf")])
def test_translate_rejects_nonfinite_input_before_any_write(tmp_path: Path, value):
    async def run():
        store, _, app, request = setup_source(tmp_path)
        async with httpx.AsyncClient(transport=httpx.ASGITransport(app=app), base_url="http://test") as client:
            response = await client.post("/authoring/trajectories/translate",
                content=json.dumps({**request, "x_m": value}), headers={"Content-Type": "application/json"})
            assert response.status_code == 400
            assert response.json()["error"]["code"] == "InvalidRequest"
            assert [asset.asset_id for asset in store.list()] == ["source"]
    asyncio.run(run())


def test_translate_rejects_damaged_source_before_publication(tmp_path: Path):
    async def run():
        store, _, app, request = setup_source(tmp_path)
        source_path = store.root / "source.v3.json"
        source = json.loads(source_path.read_text())
        source["tracks"][0]["points"][0]["x_m"] += 1
        source_path.write_text(json.dumps(source))
        damaged_bytes = source_path.read_bytes()
        async with httpx.AsyncClient(transport=httpx.ASGITransport(app=app), base_url="http://test") as client:
            response = await client.post("/authoring/trajectories/translate", json=request)
            assert response.status_code == 409
            assert response.json()["error"]["code"] == "TrajectoryResourceInvalid"
            assert source_path.read_bytes() == damaged_bytes
            assert not (store.root / "placed.v1.json").exists()
    asyncio.run(run())
