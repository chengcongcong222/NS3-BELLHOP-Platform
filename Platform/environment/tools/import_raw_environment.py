#!/usr/bin/env python3
"""Import raw WOA/GEBCO data into the Platform offline asset layout.

This tool is deliberately outside the runtime path. It migrates the legacy
WOA23 NetCDF profile selection, Mackenzie sound-speed conversion, and GEBCO
transect conversion without making simulation-time code depend on Python,
NetCDF, or HTTP.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import io
import json
import math
import os
import re
import sys
import tempfile
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path
from typing import Any, Sequence


GEBCO2020_API = "https://api.opentopodata.org/v1/gebco2020"
MAXIMUM_JSON_BYTES = 8 * 1024 * 1024
ASSET_ID_PATTERN = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$")


class RawEnvironmentImportError(RuntimeError):
    """Expected validation or source-data failure."""


def _require_finite(value: float, label: str) -> float:
    if not math.isfinite(value):
        raise RawEnvironmentImportError(f"{label} must be finite")
    return value


def _normalize_longitude(longitude_deg: float) -> float:
    normalized = ((longitude_deg + 180.0) % 360.0) - 180.0
    if normalized == -180.0 and longitude_deg > 0.0:
        return 180.0
    return normalized


def _longitude_distance_degrees(left: float, right: float) -> float:
    delta = abs(left - right)
    return min(delta, 360.0 - delta)


def _mackenzie_sound_speed_mps(
    temperature_c: float,
    salinity_psu: float,
    depth_m: float,
) -> float:
    """Pure migration of the legacy Mackenzie 1981 calculation."""
    return (
        1448.96
        + 4.591 * temperature_c
        - 5.304e-2 * temperature_c**2
        + 2.374e-4 * temperature_c**3
        + 1.340 * (salinity_psu - 35.0)
        + 1.630e-2 * depth_m
        + 1.675e-7 * depth_m**2
        - 1.025e-2 * temperature_c * (salinity_psu - 35.0)
        - 7.139e-13 * temperature_c * depth_m**3
    )


def _load_scientific_stack() -> tuple[Any, Any]:
    try:
        import numpy as np  # type: ignore
        from netCDF4 import Dataset  # type: ignore
    except ImportError as error:
        raise RawEnvironmentImportError(
            "WOA import requires the Python packages numpy and netCDF4; "
            "install them only in the offline asset-generation environment"
        ) from error
    return np, Dataset


def _require_netcdf_variables(dataset: Any, names: Sequence[str], label: str) -> None:
    missing = [name for name in names if name not in dataset.variables]
    if missing:
        raise RawEnvironmentImportError(
            f"{label} NetCDF is missing variables: {', '.join(missing)}"
        )


def _read_woa_profile(
    temperature_path: Path,
    salinity_path: Path,
    latitude_deg: float,
    longitude_deg: float,
    depth_limit_m: float,
) -> dict[str, Any]:
    """Read the nearest usable WOA temperature/salinity water column."""
    np, Dataset = _load_scientific_stack()
    for path, label in (
        (temperature_path, "temperature"),
        (salinity_path, "salinity"),
    ):
        if not path.is_file():
            raise RawEnvironmentImportError(
                f"WOA {label} NetCDF file was not found: {path}"
            )

    target_longitude = _normalize_longitude(longitude_deg)
    temperature_dataset = None
    salinity_dataset = None
    try:
        temperature_dataset = Dataset(str(temperature_path), mode="r")
        salinity_dataset = Dataset(str(salinity_path), mode="r")
    except Exception as error:
        if temperature_dataset is not None:
            temperature_dataset.close()
        if salinity_dataset is not None:
            salinity_dataset.close()
        raise RawEnvironmentImportError(
            f"Unable to open WOA NetCDF input: {error}"
        ) from error

    try:
        required_axes = ("lat", "lon", "depth")
        _require_netcdf_variables(
            temperature_dataset, (*required_axes, "t_an"), "temperature"
        )
        _require_netcdf_variables(
            salinity_dataset, (*required_axes, "s_an"), "salinity"
        )

        latitudes = np.asarray(
            temperature_dataset.variables["lat"][:], dtype=float
        ).reshape(-1)
        longitudes = np.asarray(
            temperature_dataset.variables["lon"][:], dtype=float
        ).reshape(-1)
        depths = np.asarray(
            temperature_dataset.variables["depth"][:], dtype=float
        ).reshape(-1)
        salinity_latitudes = np.asarray(
            salinity_dataset.variables["lat"][:], dtype=float
        ).reshape(-1)
        salinity_longitudes = np.asarray(
            salinity_dataset.variables["lon"][:], dtype=float
        ).reshape(-1)
        salinity_depths = np.asarray(
            salinity_dataset.variables["depth"][:], dtype=float
        ).reshape(-1)
        if (
            not np.array_equal(latitudes, salinity_latitudes)
            or not np.array_equal(longitudes, salinity_longitudes)
            or not np.array_equal(depths, salinity_depths)
        ):
            raise RawEnvironmentImportError(
                "WOA temperature and salinity coordinate axes do not match"
            )
        if len(latitudes) == 0 or len(longitudes) == 0 or len(depths) < 2:
            raise RawEnvironmentImportError("WOA coordinate axes are incomplete")
        if not (
            np.all(np.isfinite(latitudes))
            and np.all(np.isfinite(longitudes))
            and np.all(np.isfinite(depths))
        ):
            raise RawEnvironmentImportError("WOA coordinate axes contain non-finite values")

        temperature_variable = temperature_dataset.variables["t_an"]
        salinity_variable = salinity_dataset.variables["s_an"]
        if temperature_variable.ndim != 4 or salinity_variable.ndim != 4:
            raise RawEnvironmentImportError(
                "WOA t_an and s_an variables must use [time, depth, lat, lon] axes"
            )
        if temperature_variable.shape != salinity_variable.shape:
            raise RawEnvironmentImportError(
                "WOA temperature and salinity variable shapes do not match"
            )

        latitude_index = int(np.argmin(np.abs(latitudes - latitude_deg)))
        longitude_distances = np.asarray(
            [
                _longitude_distance_degrees(float(value), target_longitude)
                for value in longitudes
            ]
        )
        longitude_index = int(np.argmin(longitude_distances))

        best_candidate: tuple[int, float, int, int, Any, Any] | None = None
        for radius in range(5):
            latitude_start = max(0, latitude_index - radius)
            latitude_end = min(len(latitudes), latitude_index + radius + 1)
            longitude_start = max(0, longitude_index - radius)
            longitude_end = min(len(longitudes), longitude_index + radius + 1)
            for latitude_cursor in range(latitude_start, latitude_end):
                for longitude_cursor in range(longitude_start, longitude_end):
                    temperature_profile = np.asarray(
                        np.ma.filled(
                            temperature_variable[
                                0, :, latitude_cursor, longitude_cursor
                            ],
                            np.nan,
                        ),
                        dtype=float,
                    )
                    salinity_profile = np.asarray(
                        np.ma.filled(
                            salinity_variable[
                                0, :, latitude_cursor, longitude_cursor
                            ],
                            np.nan,
                        ),
                        dtype=float,
                    )
                    valid_mask = np.isfinite(temperature_profile) & np.isfinite(
                        salinity_profile
                    )
                    valid_count = int(np.count_nonzero(valid_mask))
                    if valid_count == 0:
                        continue
                    distance = math.hypot(
                        float(latitudes[latitude_cursor]) - latitude_deg,
                        _longitude_distance_degrees(
                            float(longitudes[longitude_cursor]), target_longitude
                        ),
                    )
                    candidate = (
                        valid_count,
                        -distance,
                        latitude_cursor,
                        longitude_cursor,
                        temperature_profile,
                        salinity_profile,
                    )
                    if best_candidate is None or candidate[:2] > best_candidate[:2]:
                        best_candidate = candidate
            if best_candidate is not None and best_candidate[0] >= 8:
                break

        if best_candidate is None:
            raise RawEnvironmentImportError(
                "WOA profile is unavailable near the requested coordinate"
            )
        (
            _,
            _,
            selected_latitude_index,
            selected_longitude_index,
            temperature_profile,
            salinity_profile,
        ) = best_candidate
        valid_mask = (
            np.isfinite(temperature_profile)
            & np.isfinite(salinity_profile)
            & (depths <= depth_limit_m)
        )
        selected_depths = np.asarray(depths[valid_mask], dtype=float)
        selected_temperatures = np.asarray(
            temperature_profile[valid_mask], dtype=float
        )
        selected_salinities = np.asarray(salinity_profile[valid_mask], dtype=float)
        if len(selected_depths) < 2:
            raise RawEnvironmentImportError(
                "WOA profile has fewer than two valid samples inside the depth limit"
            )
        if np.any(np.diff(selected_depths) <= 0.0):
            raise RawEnvironmentImportError(
                "WOA depth samples must be strictly increasing"
            )

        return {
            "depths_m": [float(value) for value in selected_depths.tolist()],
            "temperature_c": [
                float(value) for value in selected_temperatures.tolist()
            ],
            "salinity_psu": [
                float(value) for value in selected_salinities.tolist()
            ],
            "grid_latitude_deg": float(latitudes[selected_latitude_index]),
            "grid_longitude_deg": float(longitudes[selected_longitude_index]),
        }
    finally:
        temperature_dataset.close()
        salinity_dataset.close()


def _destination_point(
    latitude_deg: float,
    longitude_deg: float,
    bearing_deg: float,
    distance_m: float,
) -> tuple[float, float]:
    earth_radius_m = 6_371_000.0
    angular_distance = distance_m / earth_radius_m
    bearing = math.radians(bearing_deg)
    latitude = math.radians(latitude_deg)
    longitude = math.radians(longitude_deg)
    destination_latitude = math.asin(
        math.sin(latitude) * math.cos(angular_distance)
        + math.cos(latitude) * math.sin(angular_distance) * math.cos(bearing)
    )
    destination_longitude = longitude + math.atan2(
        math.sin(bearing) * math.sin(angular_distance) * math.cos(latitude),
        math.cos(angular_distance)
        - math.sin(latitude) * math.sin(destination_latitude),
    )
    return (
        math.degrees(destination_latitude),
        _normalize_longitude(math.degrees(destination_longitude)),
    )


def _build_ranges(range_max_m: float, sample_count: int) -> list[float]:
    return [
        round(range_max_m * index / (sample_count - 1), 3)
        for index in range(sample_count)
    ]


def _sanitize_bathymetry_depths(depths_m: Sequence[float | None]) -> list[float]:
    sanitized = [
        float(value)
        if value is not None and math.isfinite(value) and value > 0.0
        else math.nan
        for value in depths_m
    ]
    if sum(math.isfinite(value) for value in sanitized) < 2:
        raise RawEnvironmentImportError(
            "GEBCO response contains fewer than two valid ocean depths"
        )
    for index, value in enumerate(sanitized):
        if math.isfinite(value):
            continue
        left_index = next(
            (
                cursor
                for cursor in range(index - 1, -1, -1)
                if math.isfinite(sanitized[cursor])
            ),
            None,
        )
        right_index = next(
            (
                cursor
                for cursor in range(index + 1, len(sanitized))
                if math.isfinite(sanitized[cursor])
            ),
            None,
        )
        if left_index is not None and right_index is not None:
            ratio = (index - left_index) / (right_index - left_index)
            sanitized[index] = sanitized[left_index] + (
                sanitized[right_index] - sanitized[left_index]
            ) * ratio
        elif left_index is not None:
            sanitized[index] = sanitized[left_index]
        elif right_index is not None:
            sanitized[index] = sanitized[right_index]
    return [round(max(1.0, value), 3) for value in sanitized]


def _read_json_object(path: Path) -> dict[str, Any]:
    if not path.is_file():
        raise RawEnvironmentImportError(f"JSON source file was not found: {path}")
    size = path.stat().st_size
    if size == 0 or size > MAXIMUM_JSON_BYTES:
        raise RawEnvironmentImportError(
            f"JSON source is empty or exceeds {MAXIMUM_JSON_BYTES} bytes: {path}"
        )
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise RawEnvironmentImportError(
            f"Unable to read JSON source {path}: {error}"
        ) from error
    if not isinstance(value, dict):
        raise RawEnvironmentImportError("GEBCO JSON response must be an object")
    return value


def _fetch_json_object(url: str) -> dict[str, Any]:
    request = urllib.request.Request(
        url, headers={"User-Agent": "NS3-BELLHOP-Platform/0.1"}
    )
    try:
        with urllib.request.urlopen(request, timeout=120) as response:
            payload = response.read(MAXIMUM_JSON_BYTES + 1)
    except (urllib.error.URLError, TimeoutError, OSError) as error:
        raise RawEnvironmentImportError(
            f"Unable to fetch GEBCO response: {error}"
        ) from error
    if len(payload) > MAXIMUM_JSON_BYTES:
        raise RawEnvironmentImportError("GEBCO response exceeds the size limit")
    try:
        value = json.loads(payload.decode("utf-8"))
    except (UnicodeError, json.JSONDecodeError) as error:
        raise RawEnvironmentImportError(
            f"GEBCO response is not valid UTF-8 JSON: {error}"
        ) from error
    if not isinstance(value, dict):
        raise RawEnvironmentImportError("GEBCO response must be a JSON object")
    return value


def _gebco_response_to_profile(
    response: dict[str, Any], ranges_m: Sequence[float]
) -> dict[str, Any]:
    if str(response.get("status") or "").upper() != "OK":
        raise RawEnvironmentImportError(
            f"GEBCO response status is not OK: {response.get('status')!r}"
        )
    results = response.get("results")
    if not isinstance(results, list) or len(results) != len(ranges_m):
        raise RawEnvironmentImportError(
            "GEBCO response result count does not match the requested transect"
        )
    raw_depths: list[float | None] = []
    for item in results:
        elevation: float | None = None
        if isinstance(item, dict):
            try:
                candidate = float(item.get("elevation"))
                if math.isfinite(candidate):
                    elevation = candidate
            except (TypeError, ValueError):
                pass
        raw_depths.append(
            -elevation if elevation is not None and elevation < 0.0 else None
        )
    if not raw_depths or raw_depths[0] is None:
        raise RawEnvironmentImportError(
            "The requested GEBCO transect does not start over water"
        )
    return {
        "range_m": list(ranges_m),
        "depth_m": _sanitize_bathymetry_depths(raw_depths),
        "sample_count": len(ranges_m),
    }


def _load_gebco_profile(
    latitude_deg: float,
    longitude_deg: float,
    bearing_deg: float,
    range_max_m: float,
    sample_count: int,
    response_file: Path | None,
) -> tuple[dict[str, Any], str, bytes]:
    ranges_m = _build_ranges(range_max_m, sample_count)
    locations = [
        _destination_point(latitude_deg, longitude_deg, bearing_deg, range_m)
        for range_m in ranges_m
    ]
    location_query = "|".join(
        f"{latitude:.6f},{longitude:.6f}"
        for latitude, longitude in locations
    )
    encoded_locations = urllib.parse.quote(location_query, safe="|,.-")
    url = (
        f"{GEBCO2020_API}?locations={encoded_locations}"
        "&interpolation=nearest"
    )
    if response_file is not None:
        response = _read_json_object(response_file)
        raw_response = response_file.read_bytes()
        source_uri = response_file.resolve().as_uri()
    else:
        response = _fetch_json_object(url)
        raw_response = json.dumps(
            response, ensure_ascii=False, sort_keys=True
        ).encode("utf-8")
        source_uri = url
    return _gebco_response_to_profile(response, ranges_m), source_uri, raw_response


def _sha256_bytes(content: bytes) -> str:
    return "sha256:" + hashlib.sha256(content).hexdigest()


def _sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        while block := source.read(1024 * 1024):
            digest.update(block)
    return "sha256:" + digest.hexdigest()


def _atomic_write_text(path: Path, content: str, overwrite: bool) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists() and not overwrite:
        raise RawEnvironmentImportError(
            f"Output already exists; pass --overwrite to replace it: {path}"
        )
    temporary_name: str | None = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w",
            encoding="utf-8",
            newline="",
            dir=path.parent,
            prefix=path.name + ".",
            suffix=".tmp",
            delete=False,
        ) as temporary:
            temporary.write(content)
            temporary.flush()
            os.fsync(temporary.fileno())
            temporary_name = temporary.name
        os.replace(temporary_name, path)
    finally:
        if temporary_name is not None:
            Path(temporary_name).unlink(missing_ok=True)


def _sound_speed_csv(profile: dict[str, Any]) -> tuple[str, list[float]]:
    output = io.StringIO(newline="")
    writer = csv.writer(output, lineterminator="\n")
    writer.writerow(("depth_m", "sound_speed_mps"))
    speeds: list[float] = []
    for depth, temperature, salinity in zip(
        profile["depths_m"],
        profile["temperature_c"],
        profile["salinity_psu"],
        strict=True,
    ):
        speed = _mackenzie_sound_speed_mps(temperature, salinity, depth)
        if not math.isfinite(speed) or speed <= 0.0:
            raise RawEnvironmentImportError(
                "Mackenzie calculation produced an invalid sound speed"
            )
        speeds.append(speed)
        writer.writerow((round(depth, 3), round(speed, 3)))
    return output.getvalue(), speeds


def _validate_arguments(arguments: argparse.Namespace) -> None:
    if not ASSET_ID_PATTERN.fullmatch(arguments.asset_id):
        raise RawEnvironmentImportError(
            "asset id must contain only letters, digits, '.', '_' or '-', "
            "must not start with punctuation, and must be at most 128 characters"
        )
    if not arguments.asset_name.strip() or not arguments.time_label.strip():
        raise RawEnvironmentImportError("asset name and time label must not be empty")
    _require_finite(arguments.latitude, "latitude")
    _require_finite(arguments.longitude, "longitude")
    _require_finite(arguments.depth_limit_m, "depth limit")
    _require_finite(arguments.bearing_deg, "bearing")
    _require_finite(arguments.range_max_m, "maximum range")
    if not -90.0 <= arguments.latitude <= 90.0:
        raise RawEnvironmentImportError("latitude is outside [-90, 90]")
    if not -180.0 <= arguments.longitude <= 180.0:
        raise RawEnvironmentImportError("longitude is outside [-180, 180]")
    if arguments.depth_limit_m <= 0.0 or arguments.range_max_m <= 0.0:
        raise RawEnvironmentImportError(
            "depth limit and maximum range must be positive"
        )
    if not 2 <= arguments.sample_count <= 100:
        raise RawEnvironmentImportError("GEBCO sample count must be in [2, 100]")


def _import_raw_environment(arguments: argparse.Namespace) -> dict[str, Any]:
    _validate_arguments(arguments)
    temperature_path = arguments.woa_temperature.resolve()
    salinity_path = arguments.woa_salinity.resolve()
    output_root = arguments.output_root.resolve()
    ssp_relative = Path("data") / "ssp" / f"{arguments.asset_id}.csv"
    bathymetry_relative = (
        Path("data") / "bathymetry" / f"{arguments.asset_id}.json"
    )
    manifest_relative = (
        Path("data") / "woss_sources" / f"{arguments.asset_id}.json"
    )
    output_targets = [
        output_root / ssp_relative,
        output_root / bathymetry_relative,
        output_root / manifest_relative,
    ]
    if not arguments.overwrite:
        existing_targets = [str(path) for path in output_targets if path.exists()]
        if existing_targets:
            raise RawEnvironmentImportError(
                "Output already exists; pass --overwrite to replace it: "
                + ", ".join(existing_targets)
            )
    woa_profile = _read_woa_profile(
        temperature_path,
        salinity_path,
        arguments.latitude,
        arguments.longitude,
        arguments.depth_limit_m,
    )
    sound_speed_content, sound_speeds = _sound_speed_csv(woa_profile)
    bathymetry, gebco_source_uri, gebco_raw_response = _load_gebco_profile(
        arguments.latitude,
        arguments.longitude,
        arguments.bearing_deg,
        arguments.range_max_m,
        arguments.sample_count,
        arguments.gebco_response_file.resolve()
        if arguments.gebco_response_file is not None
        else None,
    )

    bathymetry_payload = {
        "description": (
            f"GEBCO2020 transect; bearing={arguments.bearing_deg:.6f} deg; "
            f"range={arguments.range_max_m:.3f} m"
        ),
        "range_m": bathymetry["range_m"],
        "depth_m": bathymetry["depth_m"],
    }
    bathymetry_content = json.dumps(
        bathymetry_payload, ensure_ascii=False, indent=2
    ) + "\n"
    manifest = {
        "id": arguments.asset_id,
        "name": arguments.asset_name,
        "provider": "woss",
        "mode": "raw-import",
        "artifacts": {
            "ssp_file": ssp_relative.as_posix(),
            "bathymetry_file": bathymetry_relative.as_posix(),
        },
        "location": {
            "latitude_deg": arguments.latitude,
            "longitude_deg": arguments.longitude,
            "woa_grid_latitude_deg": woa_profile["grid_latitude_deg"],
            "woa_grid_longitude_deg": woa_profile["grid_longitude_deg"],
            "transect_bearing_deg": arguments.bearing_deg,
            "range_max_m": arguments.range_max_m,
        },
        "time_reference": {"label": arguments.time_label},
        "datasets": {
            "woa": arguments.woa_dataset,
            "gebco": arguments.gebco_dataset,
        },
        "cache": {"source_kind": "woa23-gebco2020-raw-import"},
        "provenance": {
            "generated_by": "environment/tools/import_raw_environment.py",
            "sound_speed_equation": "Mackenzie 1981",
            "temperature_source": temperature_path.as_uri(),
            "temperature_digest": _sha256_file(temperature_path),
            "salinity_source": salinity_path.as_uri(),
            "salinity_digest": _sha256_file(salinity_path),
            "gebco_source": gebco_source_uri,
            "gebco_response_digest": _sha256_bytes(gebco_raw_response),
        },
    }
    manifest_content = json.dumps(manifest, ensure_ascii=False, indent=2) + "\n"

    _atomic_write_text(
        output_root / ssp_relative, sound_speed_content, arguments.overwrite
    )
    _atomic_write_text(
        output_root / bathymetry_relative,
        bathymetry_content,
        arguments.overwrite,
    )
    _atomic_write_text(
        output_root / manifest_relative, manifest_content, arguments.overwrite
    )
    return {
        "manifest": str(output_root / manifest_relative),
        "ssp": str(output_root / ssp_relative),
        "bathymetry": str(output_root / bathymetry_relative),
        "woa_grid": [
            woa_profile["grid_latitude_deg"],
            woa_profile["grid_longitude_deg"],
        ],
        "ssp_sample_count": len(sound_speeds),
        "bathymetry_sample_count": bathymetry["sample_count"],
    }


def _run_self_test() -> None:
    speed = _mackenzie_sound_speed_mps(10.0, 35.0, 100.0)
    if not math.isclose(speed, 1491.435067861, rel_tol=0.0, abs_tol=1e-9):
        raise AssertionError(f"unexpected Mackenzie result: {speed}")
    ranges = _build_ranges(1000.0, 3)
    if ranges != [0.0, 500.0, 1000.0]:
        raise AssertionError(f"unexpected ranges: {ranges}")
    response = {
        "status": "OK",
        "results": [
            {"elevation": -200.0},
            {"elevation": 20.0},
            {"elevation": -300.0},
        ],
    }
    profile = _gebco_response_to_profile(response, ranges)
    if profile["depth_m"] != [200.0, 250.0, 300.0]:
        raise AssertionError(f"unexpected bathymetry: {profile}")
    latitude, longitude = _destination_point(18.0, 130.0, 90.0, 0.0)
    if not math.isclose(latitude, 18.0) or not math.isclose(longitude, 130.0):
        raise AssertionError("zero-distance destination changed the coordinate")
    print("raw environment import self-test passed")


def _build_argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Offline import of local WOA NetCDF and an explicit GEBCO "
            "response into Platform environment assets"
        )
    )
    parser.add_argument("--woa-temperature", type=Path, required=True)
    parser.add_argument("--woa-salinity", type=Path, required=True)
    parser.add_argument("--latitude", type=float, required=True)
    parser.add_argument("--longitude", type=float, required=True)
    parser.add_argument("--depth-limit-m", type=float, required=True)
    parser.add_argument("--bearing-deg", type=float, required=True)
    parser.add_argument("--range-max-m", type=float, required=True)
    parser.add_argument("--sample-count", type=int, required=True)
    parser.add_argument("--asset-id", required=True)
    parser.add_argument("--asset-name", required=True)
    parser.add_argument("--time-label", required=True)
    parser.add_argument("--woa-dataset", required=True)
    parser.add_argument("--gebco-dataset", required=True)
    parser.add_argument("--output-root", type=Path, required=True)
    gebco = parser.add_mutually_exclusive_group(required=True)
    gebco.add_argument(
        "--gebco-response-file",
        type=Path,
        help="Recorded OpenTopoData/GEBCO JSON response for deterministic import",
    )
    gebco.add_argument(
        "--fetch-gebco",
        action="store_true",
        help="Explicitly permit an offline HTTP request to the GEBCO endpoint",
    )
    parser.add_argument(
        "--overwrite",
        action="store_true",
        help="Replace existing generated artifacts",
    )
    return parser


def main(arguments: Sequence[str] | None = None) -> int:
    command_line = list(arguments if arguments is not None else sys.argv[1:])
    if command_line == ["--self-test"]:
        _run_self_test()
        return 0
    parser = _build_argument_parser()
    parsed = parser.parse_args(command_line)
    try:
        result = _import_raw_environment(parsed)
    except (RawEnvironmentImportError, OSError, ValueError) as error:
        print(f"raw environment import failed: {error}", file=sys.stderr)
        return 2
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
