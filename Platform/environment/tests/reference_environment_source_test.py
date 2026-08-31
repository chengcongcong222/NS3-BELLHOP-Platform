from __future__ import annotations

import csv
import hashlib
import json
import math
import sys
from pathlib import Path


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def strictly_increasing(values: list[float]) -> bool:
    return all(left < right for left, right in zip(values, values[1:]))


def main(root: Path) -> None:
    source = root / "source"
    bellhop = root / "bellhop"
    manifest = json.loads((source / "source_manifest.json").read_text())
    config = json.loads((bellhop / "config.json").read_text())
    golden = json.loads((root / "golden_metadata.json").read_text())

    assert manifest["classification"].startswith("reference/proxy")
    assert manifest["proxy_origin"] == {
        "latitude_deg": 18.0,
        "longitude_deg": 110.0,
    }
    assert manifest["sound_speed_profile"]["nearest_grid"] == {
        "latitude_deg": 17.5,
        "longitude_deg": 109.5,
    }
    assert "113.5" not in json.dumps(manifest, sort_keys=True)
    for section in ("sound_speed_profile", "bathymetry"):
        entry = manifest[section]
        assert sha256(source / entry["path"]) == entry["sha256"]
        assert entry["normalization"]["recipe_version"].startswith("reference-")
        assert entry["normalization"]["output_sha256"] == entry["sha256"]
        assert len(entry["source_artifact"]["sha256"]) == 64

    assert manifest["bathymetry"]["selection"] == {
        "origin": {"latitude_deg": 18.0, "longitude_deg": 110.0},
        "bearing_degrees": 90.0,
        "range_minimum_m": 0.0,
        "range_maximum_m": 10_000.0,
        "sample_spacing_m": 250.0,
        "sample_count": 41,
        "variable": "elevation",
    }
    assert manifest["pipeline"] == {
        "bellhop_config_path": "../bellhop/config.json",
        "normalizer_version": "bellhop-raw-arrival-normalizer-v1",
        "final_asset_id": "reference-shallow-water-v1",
        "final_canonical_checksum": {
            "algorithm": "FNV1A64",
            "value": "fb64e543f9042c52",
        },
    }

    with (source / "ssp.csv").open(newline="") as stream:
        rows = list(csv.DictReader(stream))
    depths = [float(row["depth_m"]) for row in rows]
    speeds = [float(row["sound_speed_mps"]) for row in rows]
    assert len(depths) == 24
    assert strictly_increasing(depths)
    assert depths[0] == 0.0 and depths[-1] == 175.0
    assert all(math.isfinite(value) and value > 0.0 for value in speeds)
    assert max(config["source_depth_m"] + config["receiver_depth_m"]) <= depths[-1]

    bathymetry = json.loads((source / "bathymetry.json").read_text())
    ranges = [float(value) for value in bathymetry["range_m"]]
    bottom_depths = [float(value) for value in bathymetry["depth_m"]]
    assert len(ranges) == len(bottom_depths) == 41
    assert strictly_increasing(ranges)
    assert ranges[0] == 0.0 and ranges[-1] == 10_000.0
    assert min(bottom_depths) == 102.0 and max(bottom_depths) == 110.0
    assert all(depth > max(config["source_depth_m"] + config["receiver_depth_m"])
               for depth in bottom_depths)

    assert config["asset_id"] == golden["asset_id"]
    assert config["frequency_hz"] == golden["frequency_hz"][0] == 25_000.0
    assert config["horizontal_range_m"] == {
        "minimum": 0.0,
        "maximum": 2500.0,
        "count": 26,
        "spacing": "linear",
    }
    assert config["bellhop"]["source_commit"] == (
        "b396d40ba49c2f349258b9687cfae8ff8323828f"
    )
    assert config["beam_type"] == "geometric hat beams in Cartesian coordinates"
    assert config["requested_beam_count"] == 0
    assert config["computed_beam_count"] == 12_500
    file_map = {
        "environment_sha256": "reference_shallow_water_v1.env",
        "bathymetry_sha256": "reference_shallow_water_v1.bty",
        "altimetry_sha256": "reference_shallow_water_v1.ati",
        "normalized_ssp_sha256": "reference_shallow_water_v1_ssp.csv",
        "arrival_sha256": "reference_shallow_water_v1.arr",
        "print_log_sha256": "reference_shallow_water_v1.prt",
    }
    for key, filename in file_map.items():
        assert sha256(bellhop / filename) == config["files"][key]

    assert golden["cell_count"] == 650
    assert golden["signal_cell_count"] == 625
    assert golden["no_arrival_cell_count"] == 25
    assert golden["payload_checksum"] == {
        "algorithm": "FNV1A64",
        "value": "fb64e543f9042c52",
    }
    report = json.loads((root / "validation_report.json").read_text())
    assert report["state"] == "Valid"
    assert report["field"]["cell_count"] == golden["cell_count"]
    assert report["field"]["signal_cell_count"] == golden["signal_cell_count"]
    assert report["field"]["no_arrival_cell_count"] == golden[
        "no_arrival_cell_count"
    ]
    assert report["field"]["payload_checksum_fnv1a64"] == golden[
        "payload_checksum"
    ]["value"]


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: reference_environment_source_test.py ASSET_ROOT")
    main(Path(sys.argv[1]))
