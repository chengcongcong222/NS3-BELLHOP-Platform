import importlib.util
from pathlib import Path

import pytest

from ns3_factory_backend.acoustic_plots import file_sha256
from ns3_factory_backend.authoring import AuthoringStore, EnvironmentBuildResource

spec = importlib.util.spec_from_file_location("repair_ray_preview", Path(__file__).resolve().parents[1] / "tools/repair_ray_preview.py")
tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tool)


def saved_build(tmp_path):
    store = AuthoringStore(tmp_path)
    root = tmp_path / "builds" / "saved"
    ray_root = root / "ray-preview"
    ray_root.mkdir(parents=True)
    arrivals = root / "environment.arr"
    arrivals.write_text("authoritative arrivals, untouched by repair")
    ray = ray_root / "environment.ray"
    ray.write_text("'source'\n25000\n1 1 1\n1 1\n0\n100\n'rz'\n0\n2 0 0\n0 20\n100 30\n")
    artifact = {"arrivals_path": str(arrivals), "arrivals_sha256": file_sha256(arrivals), "published": False,
                "input_summary": {"frequency_hz": 25000, "source_depths_meters": [20]},
                "ray_plot": {"available": False, "reason": "old renderer limit"}}
    build = EnvironmentBuildResource(build_id="saved", draft_id="draft", state="Completed", steps=[], created_at="", updated_at="", artifact=artifact)
    store.save_build(build)
    return store, build, arrivals, ray


def test_repair_only_attaches_checked_existing_rays_and_keeps_a_backup(tmp_path):
    store, build, arrivals, ray = saved_build(tmp_path)
    before = (arrivals.read_bytes(), ray.read_bytes(), (tmp_path / "builds/saved.json").read_bytes())
    result = tool.repair(tmp_path, "saved")
    assert result["status"] == "recovered-existing-output"
    assert (arrivals.read_bytes(), ray.read_bytes()) == before[:2]
    assert Path(result["backup"]).read_bytes() == before[2]
    repaired = AuthoringStore(tmp_path).get_build("saved")
    assert repaired.artifact["ray_plot"]["available"]
    assert repaired.artifact["arrivals_sha256"] == build.artifact["arrivals_sha256"]
    assert tool.repair(tmp_path, "saved")["status"] == "already-available"


@pytest.mark.parametrize("failure", ["published", "checksum", "depth", "frequency"])
def test_repair_refuses_changed_fields_or_published_assets(tmp_path, failure):
    store, build, arrivals, ray = saved_build(tmp_path)
    if failure == "published":
        build.artifact["published"] = True
    elif failure == "checksum":
        arrivals.write_text("changed")
    elif failure == "depth":
        build.artifact["input_summary"]["source_depths_meters"] = [60]
    else:
        build.artifact["input_summary"]["frequency_hz"] = 12000
    store.save_build(build)
    before = (tmp_path / "builds/saved.json").read_bytes()
    with pytest.raises(ValueError):
        tool.repair(tmp_path, "saved")
    assert (tmp_path / "builds/saved.json").read_bytes() == before
    assert not (tmp_path / "builds/saved/ray-preview-metadata-before-repair.json").exists()
