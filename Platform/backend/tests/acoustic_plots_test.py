import asyncio
import hashlib
import json
import os
from pathlib import Path

import httpx
import pytest
from ns3_factory_backend.acoustic_plots import arrivals_plot, produce_ray_plot, ray_plot, ray_plot_for_source, file_sha256, _display_points
from ns3_factory_backend.api import BackendSettings, create_app
from ns3_factory_backend.wire import ReceptionPayload
from helpers import resource_catalog

ROOT = Path(__file__).resolve().parents[2]
REFERENCE = ROOT / "environment/assets/reference_shallow_water_v1"


@pytest.mark.skipif(not os.environ.get("PLATFORM_BELLHOP_EXECUTABLE"), reason="requires real Bellhop")
def test_real_flat_bottom_rays_continue_after_bottom_reflection(tmp_path):
    from environment_inputs_test import environment_payload
    payload = environment_payload()
    payload["bathymetry"] = [{"range_meters": 0, "depth_meters": 100}, {"range_meters": 1000, "depth_meters": 100}]
    artifact = produce_ray_plot(payload, tmp_path, Path(os.environ["PLATFORM_BELLHOP_EXECUTABLE"]))
    assert artifact["available"], artifact
    plot = ray_plot(Path(artifact["path"]), artifact["sha256"], 60)
    # A real trace must reach the physical bottom then turn upwards, not stop
    # at the numerical tracing box. No synthetic segment is added by the adapter.
    from itertools import groupby
    traces = [[point for point, _ in groupby(ray["points"])] for ray in plot["rays"]]
    reflections = [(a, b, c) for points in traces for a, b, c in zip(points, points[1:], points[2:])
                   if abs(b[1] - 100) < 1e-4 and a[1] < b[1] and c[1] < b[1] and c[0] > b[0]]
    assert reflections, "ray tracing terminates at the bottom instead of reflecting"


def test_reference_arrivals_use_real_delay_gain_and_checked_hash():
    path = REFERENCE / "bellhop/reference_shallow_water_v1.arr"
    sha = json.loads((REFERENCE / "bellhop/config.json").read_text())["files"]["arrival_sha256"]
    plot = arrivals_plot(path, sha, 2, 0, 10)
    assert len(plot["impulse"]) == 9
    assert plot["impulse"][0]["delay_s"] == pytest.approx(.653415382)
    selected = next(c for c in plot["field"] if c["receiver_index"] == 0 and c["range_index"] == 10)
    assert selected["tl_db"] == pytest.approx(52.5742, abs=.0001)
    assert arrivals_plot(path, sha, 0, 0, 0)["impulse"] == []
    with pytest.raises(ValueError, match="校验"):
        arrivals_plot(path, "0" * 64, 0, 0, 0)
    with pytest.raises(ValueError, match="采样"):
        arrivals_plot(path, sha, 99, 0, 0)


@pytest.mark.parametrize("content", ["'2D'\nNaN\n", "'2D'\n25000\n2 5 5\n", "'3D'\n", "'2D'\n25000\n1 8\n1 8\n1 100\n1\n1\n1e300 0 .1 0 0 0 0 0\n"])
def test_rejects_malformed_plot_input(tmp_path, content):
    path = tmp_path / "bad.arr"; path.write_text(content)
    with pytest.raises(ValueError):
        arrivals_plot(path, None, 0, 0, 0)


def test_single_depth_plot_preserves_samples_and_no_arrival_gaps(tmp_path):
    path = tmp_path / "single-depth.arr"
    path.write_text("'2D'\n25000\n1 10\n1 30\n3 100 200 300\n1\n"
                    "1\n0.01 0 .1 0 0 0 0 0\n0\n1\n0.001 0 .3 0 0 0 0 0\n")
    sha = hashlib.sha256(path.read_bytes()).hexdigest()
    data = arrivals_plot(path, sha, 0, 0, 2)
    assert data["receiver_depths_m"] == [30]
    assert data["ranges_m"] == [100, 200, 300]
    assert [cell["tl_db"] for cell in data["field"]] == [40, None, 60]
    assert data["selection"] == {"source_index": 0, "receiver_index": 0, "range_index": 2}
    assert data["impulse"][0]["delay_s"] == .3
    for selection in ((-1, 0, 0), (0, 1, 0), (0, 0, 3)):
        with pytest.raises(ValueError, match="不能外推"):
            arrivals_plot(path, sha, *selection)


@pytest.mark.parametrize("amplitude", ["0", "1e-300"])
def test_arrival_paths_with_zero_or_underflow_energy_are_not_reported_as_no_arrival(tmp_path, amplitude):
    path = tmp_path / "zero-energy.arr"
    path.write_text(f"'2D'\n25000\n1 10\n1 30\n1 100\n1\n1\n{amplitude} 0 .1 0 0 0 0 0\n")
    with pytest.raises(ValueError, match="不能标记为无到达"):
        arrivals_plot(path, None, 0, 0, 0)


@pytest.mark.parametrize("frequency,beams,origins", [
    (0, "2 1", [10, 10]), (25000, "0 1", [10, 10]),
    (25000, "2 1", [10, 20]),
])
def test_ray_plot_rejects_false_frequency_beam_count_or_mixed_source(tmp_path, frequency, beams, origins):
    path = tmp_path / "bad.ray"
    path.write_text(f"'test source'\n{frequency}\n1 1 1\n{beams}\n0\n100\n'rz'\n"
                    f"-10\n2 0 0\n0 {origins[0]}\n100 30\n"
                    f"10\n2 0 0\n0 {origins[1]}\n100 50\n")
    sha = hashlib.sha256(path.read_bytes()).hexdigest()
    with pytest.raises(ValueError, match="不受支持"):
        ray_plot(path, sha)


def test_ray_plot_preserves_actual_frequency_source_and_hash(tmp_path):
    path = tmp_path / "saved.ray"
    path.write_text("'test source'\n12000\n1 1 1\n1 1\n0\n100\n'rz'\n"
                    "-10\n2 0 0\n0 20\n100 30\n")
    sha = hashlib.sha256(path.read_bytes()).hexdigest()
    data = ray_plot(path, sha)
    assert data["frequency_hz"] == 12000
    assert data["source_depth_m"] == 20
    assert data["source_depths_m"] == [20]
    assert data["sha256"] == sha
    assert data["rays"][0]["points"] == [[0, 20], [100, 30]]
    with pytest.raises(ValueError, match="校验"):
        ray_plot(path, "0" * 64)


def test_ray_selection_uses_saved_origins_not_ray_order(tmp_path):
    path = tmp_path / "two-sources.ray"
    # Deliberately interleave sources; no source is inferred from beam order.
    path.write_text("'two actual sources'\n25000\n1 1 2\n2 1\n0\n100\n'rz'\n"
                    "-10\n2 0 0\n0 60\n100 70\n"
                    "-10\n2 0 0\n0 20\n100 30\n"
                    "10\n2 0 1\n0 60\n100 40\n")
    artifact = {"available": True, "path": str(path), "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
    shallow = ray_plot_for_source(artifact, 20, 25000)
    deep = ray_plot_for_source(artifact, 60, 25000)
    assert shallow["source_depths_m"] == deep["source_depths_m"] == [20, 60]
    assert shallow["source_depth_m"] == 20
    assert deep["source_depth_m"] == 60
    assert len(shallow["rays"]) == 1 and len(deep["rays"]) == 2
    assert all(ray["points"][0] == [0, 60] for ray in deep["rays"])
    assert shallow["sha256"] == deep["sha256"] == artifact["sha256"]
    assert str(tmp_path) not in json.dumps(deep)
    assert not ray_plot_for_source(artifact, 60, 12000)["available"]


def test_old_single_source_is_not_relabelled_for_another_depth(tmp_path):
    path = tmp_path / "legacy.ray"
    path.write_text("'legacy source'\n25000\n1 1 1\n1 1\n0\n100\n'rz'\n"
                    "-10\n2 0 0\n0 20\n100 30\n")
    original = path.read_bytes()
    artifact = {"available": True, "path": str(path), "sha256": hashlib.sha256(original).hexdigest()}
    assert ray_plot_for_source(artifact, 20, 25000)["available"]
    missing = ray_plot_for_source(artifact, 60, 25000)
    assert not missing["available"] and missing["source_depths_m"] == [20]
    assert "60 m" in missing["reason"] and "另存并重新计算" in missing["reason"]
    assert "rays" not in missing
    assert path.read_bytes() == original
    assert not ray_plot_for_source({**artifact, "sha256": "0" * 64}, 20, 25000)["available"]
    assert not ray_plot_for_source({}, 20, 25000)["available"]


def test_ray_header_must_account_for_every_actual_source(tmp_path):
    path = tmp_path / "missing-source.ray"
    path.write_text("'incomplete sources'\n25000\n1 1 2\n1 1\n0\n100\n'rz'\n"
                    "-10\n2 0 0\n0 20\n100 30\n")
    with pytest.raises(ValueError, match="不受支持"):
        ray_plot(path, hashlib.sha256(path.read_bytes()).hexdigest())


def test_display_thinning_keeps_actual_endpoints_and_bottom_turns():
    points = [[i, i if i < 2001 else 4002 - i] for i in range(4003)]
    displayed = _display_points(points)
    assert displayed[0] == points[0] and displayed[-1] == points[-1]
    assert points[2001] in displayed
    assert len(displayed) < len(points)
    assert all(point == points[point[0]] for point in displayed)


def test_large_rays_stream_without_read_bytes_or_old_global_point_limit(tmp_path, monkeypatch):
    ray = tmp_path / "large.ray"
    # >32 MiB, >500,000 points; bounded single-trace memory. Blank lines are
    # legal and exercise the byte limit without an expensive solver job.
    with ray.open("w") as stream:
        stream.write("'large'\n25000\n1 1 2\n2 1\n0\n100\n'rz'\n")
        for depth in (20, 60):
            for angle in (-10, 10):
                stream.write(f"{angle}\n130001 0 0\n0 {depth}\n")
                for i in range(1, 130001):
                    stream.write(f"{i / 1000} {depth}\n")
        for _ in range(4096):
            stream.write(" " * 8191 + "\n")
    sha = file_sha256(ray)
    def forbid_bulk_read(*args):
        raise AssertionError("Must stream the plot file")
    monkeypatch.setattr(Path, "read_bytes", forbid_bulk_read)
    result = ray_plot(ray, sha, 60)
    assert result["source_depths_m"] == [20, 60]
    assert len(result["rays"]) == 2
    assert all(r["points"][0] == [0, 60] for r in result["rays"])
    assert result["display_sampling"]["original_points"] == 260002
    assert result["display_sampling"]["displayed_points"] < 2500
    with pytest.raises(ValueError, match="校验"):
        ray_plot(ray, "0" * 64, 60)


def test_real_offline_ray_job_does_not_modify_main_input(tmp_path):
    from ns3_factory_backend.environment_inputs import write_bellhop_inputs
    exe = Path(os.environ.get("PLATFORM_BELLHOP_EXECUTABLE", ROOT / ".runtime/tools/bellhopcuda/bin/bellhopcxx2d"))
    if not exe.is_file():
        pytest.skip("optional offline Bellhop executable is not installed")
    payload = {"center_frequency_hz": 25000, "sound_speed_profile": [
        {"depth_meters": 0, "speed_meters_per_second": 1500}, {"depth_meters": 100, "speed_meters_per_second": 1495}],
        "bathymetry": [{"range_meters": 0, "depth_meters": 100}, {"range_meters": 1000, "depth_meters": 100}],
        "bottom": {"sound_speed_meters_per_second": 1700, "density_grams_per_cubic_centimeter": 1.8, "attenuation_db_per_wavelength": .5},
        "bellhop": {"beam_count": 51, "range_sample_count": 11, "source_depths_meters": [20, 60],
                    "receiver_depths_meters": [10, 50], "minimum_launch_angle_degrees": -60,
                    "maximum_launch_angle_degrees": 60, "output": "Arrivals"}}
    before = json.dumps(payload, sort_keys=True)
    job_root = tmp_path / "main-arrivals-job"
    write_bellhop_inputs(payload, job_root)
    main_inputs = {path.name: path.read_bytes() for path in job_root.iterdir()}
    result = produce_ray_plot(payload, job_root, exe)
    assert result["available"], result
    parsed = ray_plot(Path(result["path"]), result["sha256"])
    assert parsed["source_depth_m"] == 20
    assert parsed["source_depths_m"] == [20, 60]
    assert parsed["rays"] and len(parsed["rays"]) <= 31
    deep = ray_plot_for_source(result, 60, 25000)
    assert deep["available"] and deep["source_depth_m"] == 60
    assert deep["rays"] and len(deep["rays"]) <= 31
    assert all(ray["points"][0][1] == 60 for ray in deep["rays"])
    assert deep["rays"] != parsed["rays"]
    assert json.dumps(payload, sort_keys=True) == before
    assert all((job_root / name).read_bytes() == content for name, content in main_inputs.items())


def test_plot_api_is_readonly_and_does_not_expose_local_paths(tmp_path):
    async def run():
        app = create_app(BackendSettings(worker_executable=tmp_path / "unused", environment_repository_root=tmp_path / "repository", authoring_workspace_root=tmp_path / "workspace"), resource_catalog=resource_catalog())
        async with httpx.AsyncClient(transport=httpx.ASGITransport(app=app), base_url="http://test") as client:
            missing = await client.get("/environments/backend-field-v1/acoustic-plots")
            assert missing.status_code == 200 and not missing.json()["available"]
            assert str(tmp_path) not in missing.text
            unknown = await client.get("/environments/nonexistent/acoustic-plots")
            assert unknown.status_code == 404
    asyncio.run(run())


@pytest.mark.parametrize("legacy_single_source", [False, True])
def test_plot_api_selects_exact_saved_source_for_build_and_publication(tmp_path, monkeypatch, legacy_single_source):
    from ns3_factory_backend.authoring import AuthoringStore, EnvironmentBuildResource
    arrivals = tmp_path / "two-depths.arr"
    arrivals.write_text("'2D'\n25000\n2 20 60\n1 30\n1 100\n"
                        "1\n1\n0.01 0 .1 0 0 0 0 0\n"
                        "1\n1\n0.001 0 .2 0 0 0 0 0\n")
    ray = tmp_path / "two-depths.ray"
    ray.write_text(f"'actual source traces'\n25000\n1 1 {1 if legacy_single_source else 2}\n1 1\n0\n100\n'rz'\n"
                  "-10\n2 0 0\n0 20\n100 30\n"
                  + ("" if legacy_single_source else "-10\n2 0 0\n0 60\n100 70\n"))
    original_arrivals, original_ray = arrivals.read_bytes(), ray.read_bytes()
    artifact = {"environment_asset_id": "backend-field-v1", "format": "NS3_FACTORY_ACOUSTIC_FIELD",
                "build_kind": "BellhopOffline", "arrivals_path": str(arrivals),
                "arrivals_sha256": hashlib.sha256(original_arrivals).hexdigest(),
                "ray_plot": {"available": True, "path": str(ray), "sha256": hashlib.sha256(original_ray).hexdigest()}}
    store = AuthoringStore(tmp_path / "workspace")
    store.save_build(EnvironmentBuildResource(build_id="saved-plot-build", draft_id="source-draft", state="Completed",
        steps=[], created_at="", updated_at="", artifact=artifact))
    store.publish_build("saved-plot-build")
    def no_calculation(*args, **kwargs):
        raise AssertionError("A read-only plot request must not run Bellhop")
    monkeypatch.setattr("subprocess.run", no_calculation)

    async def run():
        app = create_app(BackendSettings(worker_executable=tmp_path / "unused",
            environment_repository_root=tmp_path / "repository", authoring_workspace_root=tmp_path / "workspace"),
            resource_catalog=resource_catalog())
        async with httpx.AsyncClient(transport=httpx.ASGITransport(app=app), base_url="http://test") as client:
            for base in ("/environments/backend-field-v1", "/authoring/environment-builds/saved-plot-build"):
                shallow = await client.get(base + "/acoustic-plots?source_index=0")
                deep = await client.get(base + "/acoustic-plots?source_index=1")
                assert shallow.status_code == deep.status_code == 200
                assert shallow.json()["ray_plot"]["source_depth_m"] == 20
                data = deep.json()
                assert data["available"] and data["field"][0]["tl_db"] == 60
                if legacy_single_source:
                    assert not data["ray_plot"]["available"]
                    assert "另存并重新计算" in data["ray_plot"]["reason"]
                else:
                    assert data["ray_plot"]["source_depth_m"] == 60
                    assert data["ray_plot"]["rays"][0]["points"][0] == [0, 60]
                # Ray-only display must not evaluate any loss/impulse grid.
                def no_arrivals(*args):
                    raise AssertionError("Ray view must not parse arrivals")
                with monkeypatch.context() as patch:
                    patch.setattr("ns3_factory_backend.acoustic_plots.arrivals_plot", no_arrivals)
                    only = await client.get(base + "/acoustic-plots?source_index=1&view=rays")
                assert only.status_code == 200
                assert "field" not in only.json() and "impulse" not in only.json()
                assert only.json()["ray_plot"]["available"] is (not legacy_single_source)
                assert str(tmp_path) not in deep.text
                invalid = await client.get(base + "/acoustic-plots?source_index=2")
                assert invalid.status_code >= 400
    asyncio.run(run())
    assert arrivals.read_bytes() == original_arrivals and ray.read_bytes() == original_ray


def test_ray_view_does_not_open_large_arrivals_when_build_manifest_is_available(tmp_path, monkeypatch):
    from ns3_factory_backend.acoustic_plots import rays_only_plot
    ray = tmp_path / "checked.ray"
    ray.write_text("'checked'\n25000\n1 1 1\n1 1\n0\n100\n'rz'\n0\n2 0 0\n0 20\n100 30\n")
    artifact = {"arrivals_path": str(tmp_path / "must-not-be-opened.arr"), "arrivals_sha256": "recorded-build-hash",
                "input_summary": {"frequency_hz": 25000, "source_depths_meters": [20]},
                "ray_plot": {"available": True, "path": str(ray), "sha256": file_sha256(ray)}}
    assert rays_only_plot(artifact, 0)["ray_plot"]["available"]
    with pytest.raises(ValueError, match="不能外推"):
        rays_only_plot(artifact, 1)
    artifact["ray_plot"]["sha256"] = "0" * 64
    assert not rays_only_plot(artifact, 0)["ray_plot"]["available"]


def test_optional_waveform_summary_is_strict_and_old_events_remain_valid():
    old = {"reception_id": "1", "transmission_id": "2", "packet_id": "3", "receiver_node_id": "0", "disposition": "NotDecoded", "quality": None}
    assert ReceptionPayload.model_validate(old).waveform is None
    measured = {"payload_bit_count": "100", "bit_error_count": "4", "crc_ok": False, "signal_power_upa2": 1., "noise_power_upa2": 0.}
    assert ReceptionPayload.model_validate({**old, "waveform": measured}).waveform.noise_power_upa2 == 0
    for change in ({"bit_error_count": "101"}, {"noise_power_upa2": -1.}, {"payload_bit_count": "0"}, {"signal_power_upa2": float("nan")}):
        with pytest.raises(ValueError):
            ReceptionPayload.model_validate({**old, "waveform": {**measured, **change}})
