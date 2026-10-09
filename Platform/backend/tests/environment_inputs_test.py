from __future__ import annotations

import asyncio
from copy import deepcopy
import io
import json
import os
from pathlib import Path
from unittest.mock import patch

import httpx
import pytest

from ns3_factory_backend.authoring import (
    AuthoringStore, BuildRequest, DraftWrite, OceanAcquisitionRequest,
    TerrainAcquisitionRequest, acquire_terrain, acquire_woa, new_build, parse_two_column_text,
)
from ns3_factory_backend.environment_inputs import validate_environment_samples, write_bellhop_inputs


def environment_payload() -> dict:
    return {
        "center_frequency_hz": 25000,
        "sound_speed_profile": [{"depth_meters": 0, "speed_meters_per_second": 1500},
                                {"depth_meters": 200, "speed_meters_per_second": 1490}],
        "bathymetry": [{"range_meters": 0, "depth_meters": 100},
                       {"range_meters": 1000, "depth_meters": 110}],
        "bottom": {"sound_speed_meters_per_second": 1700,
                   "density_grams_per_cubic_centimeter": 1.8,
                   "attenuation_db_per_wavelength": 0.5},
        "bellhop": {"beam_count": 501, "minimum_launch_angle_degrees": -60,
                    "maximum_launch_angle_degrees": 60, "output": "Arrivals",
                    "source_depths_meters": [8, 60], "receiver_depths_meters": [8, 60],
                    "range_sample_count": 11},
        "acquisition": {"latitudeDeg": 18, "longitudeDeg": 150},
        "build_source": "authored-bellhop-inputs",
    }


@pytest.mark.parametrize("content", ["0,1500\n10,nan\n20,1490", "0,1500\noops\n20,1490",
                                    "0,1500\n0,1490", '{"depth_m":[0,10,20],"sound_speed_mps":[1500,1490]}'])
def test_import_cannot_silently_drop_invalid_or_duplicate_samples(content: str) -> None:
    with pytest.raises(ValueError):
        parse_two_column_text(content, "ssp")


def test_build_refuses_a_different_draft_revision(tmp_path: Path) -> None:
    store = AuthoringStore(tmp_path)
    draft = store.create(DraftWrite(kind="environment", name="Revision gate", payload=environment_payload()))
    with pytest.raises(RuntimeError, match="已更新"):
        new_build(store, BuildRequest(draft_id=draft.draft_id, expected_revision=2))


def test_current_input_files_are_deterministic_and_preserve_source(tmp_path: Path) -> None:
    payload = environment_payload()
    before = deepcopy(payload)
    one = write_bellhop_inputs(payload, tmp_path / "one")
    two = write_bellhop_inputs(payload, tmp_path / "two")
    assert one == two
    assert payload == before
    env = (tmp_path / "one/environment.env").read_text()
    # Terminal SSP is interpolated within the original 0..200 m domain, not flattened.
    assert "110 1494.5 /" in env
    # The numerical tracing box must not terminate rays at the physical bed.
    assert one["tracing_box_depth_meters"] > 110
    assert float(env.strip().splitlines()[-1].split()[1]) > 110
    assert "1700 0 1.8" in env  # halfspace shear speed is separate from density
    assert "1 110" in (tmp_path / "one/environment.bty").read_text()  # ranges: km
    assert json.loads((tmp_path / "one/input.json").read_text()) == payload


@pytest.mark.parametrize("mutation", [
    lambda p: p["sound_speed_profile"][1].update(depth_meters=50),
    lambda p: p["sound_speed_profile"][1].update(depth_meters=0),
    lambda p: p["sound_speed_profile"][0].update(depth_meters=10),
    lambda p: p["sound_speed_profile"][1].update(speed_meters_per_second=float("nan")),
    lambda p: p["bathymetry"][1].update(range_meters=0),
    lambda p: p.update(center_frequency_hz=float("inf")),
    lambda p: p["bathymetry"].append({"range_meters": 2000, "depth_meters": None}),
])
def test_invalid_source_is_rejected_without_repair(mutation) -> None:
    payload = environment_payload()
    mutation(payload)
    with pytest.raises(ValueError):
        validate_environment_samples(payload)


def test_changed_parameters_change_inputs(tmp_path: Path) -> None:
    payload = environment_payload()
    before = write_bellhop_inputs(payload, tmp_path / "before")
    payload["sound_speed_profile"][0]["speed_meters_per_second"] = 1520
    payload["bottom"]["sound_speed_meters_per_second"] = 1800
    payload["bellhop"]["beam_count"] = 701
    after = write_bellhop_inputs(payload, tmp_path / "after")
    assert before["input_files_sha256"]["environment.env"] != after["input_files_sha256"]["environment.env"]
    assert before["input_files_sha256"]["environment.bty"] == after["input_files_sha256"]["environment.bty"]


def test_missing_bellhop_does_not_import_reference(tmp_path: Path, monkeypatch) -> None:
    monkeypatch.delenv("PLATFORM_BELLHOP_EXECUTABLE", raising=False)
    store = AuthoringStore(tmp_path)
    draft = store.create(DraftWrite(kind="environment", name="User input", payload=environment_payload()))
    result = new_build(store, BuildRequest(draft_id=draft.draft_id))
    assert result.state == "Failed"
    assert result.artifact is None
    assert "不会改用内置参考声场" in result.failure["message"]


def test_reference_marker_cannot_hide_changed_input(tmp_path: Path) -> None:
    payload = environment_payload()
    payload["build_source"] = "reference-bellhop-arrivals"
    store = AuthoringStore(tmp_path)
    draft = store.create(DraftWrite(kind="environment", name="Changed reference", payload=payload))
    result = new_build(store, BuildRequest(draft_id=draft.draft_id))
    assert result.state == "Failed" and result.artifact is None
    assert "必须重新计算" in result.failure["message"]


def test_reference_cache_cannot_claim_a_different_coordinate(tmp_path: Path) -> None:
    reference = Path(__file__).resolve().parents[2] / "environment/assets/reference_shallow_water_v1"
    with pytest.raises(ValueError, match="固定"):
        acquire_woa(OceanAcquisitionRequest(latitude_deg=30, longitude_deg=-40, month=8,
                    use_reference_cache=True), tmp_path, reference)
    with pytest.raises(ValueError, match="固定"):
        acquire_terrain(TerrainAcquisitionRequest(latitude_deg=18, longitude_deg=150,
                        bearing_deg=180, use_reference_cache=True), reference)
    data = acquire_woa(OceanAcquisitionRequest(latitude_deg=18, longitude_deg=110,
                        month=4, use_reference_cache=True), tmp_path, reference)
    assert data["grid_coordinate"] == {"latitude_deg": 17.5, "longitude_deg": 109.5}


@pytest.mark.parametrize("elevation", [None, 0, 10, True])
def test_land_or_missing_terrain_does_not_repeat_previous_depth(tmp_path: Path, elevation) -> None:
    response = {"results": [{"elevation": -100}, {"elevation": elevation}]}
    with patch("urllib.request.urlopen", return_value=io.BytesIO(json.dumps(response).encode())):
        with pytest.raises(ValueError, match="不能用相邻水深填补"):
            acquire_terrain(TerrainAcquisitionRequest(latitude_deg=18, longitude_deg=150, sample_count=2), tmp_path)


@pytest.mark.skipif(not os.environ.get("PLATFORM_BELLHOP_EXECUTABLE"), reason="requires explicit real offline Bellhop executable")
def test_real_bellhop_uses_current_input_and_publisher_accepts_it(tmp_path: Path) -> None:
    """Opt-in real engine gate; no fake solver or canned arrivals."""
    import subprocess
    store = AuthoringStore(tmp_path / "workspace")
    payload = environment_payload()
    (tmp_path / "repository").mkdir()
    outputs = []
    for speed in (1500, 1540):
        payload["sound_speed_profile"][0]["speed_meters_per_second"] = speed
        draft = store.create(DraftWrite(kind="environment", name=f"water-{speed}", payload=deepcopy(payload)))
        build = new_build(store, BuildRequest(draft_id=draft.draft_id))
        assert build.state == "Completed", build.failure
        assert build.artifact["bellhop_execution"] == "Executed"
        assert build.artifact["source_snapshot"]["payload"] == payload
        outputs.append(build.artifact["arrivals_sha256"])
        publisher = os.environ["PLATFORM_ENVIRONMENT_ASSET_PUBLISHER_PATH"]
        result = subprocess.run([publisher, build.artifact["arrivals_path"], str(tmp_path / "repository"),
                                 build.artifact["environment_asset_id"]], capture_output=True, text=True)
        assert result.returncode == 0, result.stderr
    assert outputs[0] != outputs[1]


def test_run_expected_revision_rejects_changed_draft(tmp_path: Path) -> None:
    from helpers import resource_catalog
    from authoring_api_test import CapturingGateway
    from ns3_factory_backend.api import BackendSettings, create_app

    async def run():
        gateway = CapturingGateway()
        app = create_app(BackendSettings(worker_executable=tmp_path / "worker",
                        environment_repository_root=tmp_path / "repository",
                        authoring_workspace_root=tmp_path / "workspace"),
                         gateway=gateway, resource_catalog=resource_catalog())
        async with httpx.AsyncClient(transport=httpx.ASGITransport(app=app), base_url="http://test") as client:
            draft = (await client.post("/authoring/drafts", json={"kind": "experiment", "name": "Version gate", "payload": {
                "scenario_id": "acceptance4-scenario", "bit_rate_bits_per_second": "60",
                "simulation_cycle_count": "2", "deterministic_seed": "17"}})).json()
            result = await client.post(f"/authoring/experiments/{draft['draft_id']}/runs", headers={"If-Match": "99"})
            assert result.status_code == 409
            assert not gateway.called.is_set()
    asyncio.run(run())


def test_environment_projection_reads_publication_not_mutable_draft(tmp_path: Path) -> None:
    from helpers import resource_catalog
    from authoring_api_test import IdleGateway
    from ns3_factory_backend.authoring import EnvironmentBuildResource
    from ns3_factory_backend.api import BackendSettings, create_app
    store = AuthoringStore(tmp_path / "workspace")
    draft = store.create(DraftWrite(kind="environment", name="原环境", payload=environment_payload()))
    build = EnvironmentBuildResource(build_id="saved-build", draft_id=draft.draft_id, state="Completed",
        steps=[], created_at="", updated_at="", artifact={"environment_asset_id": "backend-field-v1",
        "format": "NS3_FACTORY_ACOUSTIC_FIELD", "build_kind": "BellhopOffline", "arrivals_sha256": "test-checksum",
        "source_snapshot": draft.model_dump()})
    store.save_build(build)
    store.publish_build(build.build_id)
    changed = environment_payload()
    changed["sound_speed_profile"][0]["speed_meters_per_second"] = 1540
    store.update("environment", draft.draft_id, DraftWrite(kind="environment", name="后来修改", payload=changed), 1)

    async def run():
        app = create_app(BackendSettings(worker_executable=tmp_path / "worker",
            environment_repository_root=tmp_path / "repository", authoring_workspace_root=tmp_path / "workspace"),
            gateway=IdleGateway(), resource_catalog=resource_catalog())
        async with httpx.AsyncClient(transport=httpx.ASGITransport(app=app), base_url="http://test") as client:
            result = await client.get("/environments/backend-field-v1/authoring-inputs")
            assert result.status_code == 200
            assert result.json()["name"] == "原环境"
            assert result.json()["source_revision"] == 1
            assert result.json()["payload"] == environment_payload()
            missing = await client.get("/environments/missing/authoring-inputs")
            assert missing.status_code == 404
    asyncio.run(run())
