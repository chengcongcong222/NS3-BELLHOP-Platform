"""Validated, deterministic offline Bellhop input generation (never Runtime I/O)."""
from __future__ import annotations

import hashlib
import json
import math
from pathlib import Path
from typing import Any


def finite(value: object, label: str, *, positive: bool = False) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(f"{label}必须是有限数字")
    number = float(value)
    if not math.isfinite(number) or (positive and number <= 0):
        raise ValueError(f"{label}必须是{'正的' if positive else ''}有限数字")
    return number


def sample_pairs(value: object, x: str, y: str, label: str) -> list[tuple[float, float]]:
    if not isinstance(value, list) or len(value) < 2:
        raise ValueError(f"{label}至少需要两个有效采样点")
    points = []
    for item in value:
        if not isinstance(item, dict):
            raise ValueError(f"{label}包含非法采样点")
        first = finite(item.get(x), f"{label}坐标")
        second = finite(item.get(y), f"{label}数值", positive=True)
        if first < 0 or (points and first <= points[-1][0]):
            raise ValueError(f"{label}坐标必须非负且严格递增，不允许重复或乱序")
        points.append((first, second))
    if points[0][0] != 0:
        raise ValueError(f"{label}必须从 0 开始，不能静默外推")
    return points


def validate_environment_samples(payload: dict[str, Any]) -> tuple[list[tuple[float, float]], list[tuple[float, float]]]:
    ssp = sample_pairs(payload.get("sound_speed_profile"), "depth_meters", "speed_meters_per_second", "声速剖面")
    terrain = sample_pairs(payload.get("bathymetry"), "range_meters", "depth_meters", "海底地形")
    finite(payload.get("center_frequency_hz"), "工作频率", positive=True)
    if ssp[-1][0] < max(depth for _, depth in terrain):
        raise ValueError("声速剖面未覆盖地形最深处；请补充更深的水文数据，不能静默延拓")
    return ssp, terrain


def clip_profile(ssp: list[tuple[float, float]], bottom: float) -> list[tuple[float, float]]:
    """Interpolate only the final in-domain point; never clamp/extrapolate samples."""
    points = [point for point in ssp if point[0] <= bottom]
    if points[-1][0] != bottom:
        low = points[-1]
        high = next(point for point in ssp if point[0] > bottom)
        fraction = (bottom - low[0]) / (high[0] - low[0])
        points.append((bottom, low[1] + fraction * (high[1] - low[1])))
    return points


def _depth_axis(value: object, water_depth: float, label: str) -> list[float]:
    if not isinstance(value, list) or not 1 <= len(value) <= 64:
        raise ValueError(f"请指定{label}（1 至 64 个深度，单位米）")
    result = [finite(item, label, positive=True) for item in value]
    if any(a >= b for a, b in zip(result, result[1:])) or result[-1] >= water_depth:
        raise ValueError(f"{label}必须严格递增，并浅于剖面最浅海底")
    return result


def write_bellhop_inputs(payload: dict[str, Any], job_root: Path) -> dict[str, Any]:
    """Write one 2D range-dependent-bottom, depth-dependent-SSP input set.

    Format: Bellhop manual, .env depths in metres/ranges in kilometres;
    bottom halfspace: depth, compressional speed, shear speed, density,
    attenuation. W selects dB/wavelength. C selects linear SSP interpolation.
    """
    ssp, terrain = validate_environment_samples(payload)
    bottom_depth = max(depth for _, depth in terrain)
    range_m = terrain[-1][0]
    bottom = payload.get("bottom")
    config = payload.get("bellhop")
    if not isinstance(bottom, dict) or not isinstance(config, dict):
        raise ValueError("请填写底质和 Bellhop 计算参数")
    cp = finite(bottom.get("sound_speed_meters_per_second"), "底质声速", positive=True)
    density = finite(bottom.get("density_grams_per_cubic_centimeter"), "底质密度", positive=True)
    attenuation = finite(bottom.get("attenuation_db_per_wavelength"), "底质衰减")
    if attenuation < 0:
        raise ValueError("底质衰减不能为负")
    beams = config.get("beam_count")
    count = config.get("range_sample_count")
    if isinstance(beams, bool) or not isinstance(beams, int) or not 1 <= beams <= 100_000:
        raise ValueError("波束数量必须为 1 至 100000 的整数")
    if isinstance(count, bool) or not isinstance(count, int) or not 2 <= count <= 1000:
        raise ValueError("距离采样数必须为 2 至 1000 的整数")
    low = finite(config.get("minimum_launch_angle_degrees"), "最小发射角")
    high = finite(config.get("maximum_launch_angle_degrees"), "最大发射角")
    if not -90 < low < high < 90:
        raise ValueError("发射角须在 -90° 至 90° 内且最小值小于最大值")
    if config.get("output") != "Arrivals":
        raise ValueError("当前运行资源需要多径到达结果")
    shallowest = min(depth for _, depth in terrain)
    sources = _depth_axis(config.get("source_depths_meters"), shallowest, "发送深度采样")
    receivers = _depth_axis(config.get("receiver_depths_meters"), shallowest, "接收深度采样")
    if len(sources) * len(receivers) * count > 200_000:
        raise ValueError("声场网格过大，请减少深度或距离采样数量")
    clipped = clip_profile(ssp, bottom_depth)
    # The numerical ray-termination box is NOT the physical seabed. Bellhopcxx
    # terminates at depth >= Box.z, including a point just reflected at the bed.
    # Enclose the bed strictly so reflected paths can continue. This does not
    # extend the SSP or change the physical bottom/receiver coverage.
    tracing_depth = bottom_depth + max(1.0, bottom_depth * 0.01)
    number = lambda n: format(n, ".17g")
    axis = lambda values: f"{len(values)}\n" + " ".join(number(n) for n in values) + " /"
    content = ["'User authored acoustic environment'", number(payload["center_frequency_hz"]), "1", "'CVW'",
               f"{len(clipped)} 0 {number(bottom_depth)}"]
    content += [f"{number(depth)} {number(speed)} /" for depth, speed in clipped]
    content += ["'A*' 0", f"{number(bottom_depth)} {number(cp)} 0 {number(density)} {number(attenuation)} /",
                axis(sources), axis(receivers), f"{count}\n0 {number(range_m / 1000)} /",
                "'A'", str(beams), f"{number(low)} {number(high)} /",
                f"0 {number(tracing_depth)} {number(range_m / 1000)}"]
    files = {
        "environment.env": "\n".join(content) + "\n",
        "environment.bty": "'L'\n" + str(len(terrain)) + "\n" + "\n".join(
            f"{number(distance / 1000)} {number(depth)}" for distance, depth in terrain) + "\n",
        "input.json": json.dumps(payload, sort_keys=True, ensure_ascii=False, allow_nan=False, indent=2) + "\n",
    }
    job_root.mkdir(parents=True, exist_ok=False)
    for name, data in files.items():
        (job_root / name).write_text(data, encoding="utf-8")
    return {
        "input_files_sha256": {name: hashlib.sha256(data.encode()).hexdigest() for name, data in files.items()},
        "frequency_hz": payload["center_frequency_hz"], "water_depth_meters": bottom_depth,
        "tracing_box_depth_meters": tracing_depth,
        "maximum_range_meters": range_m, "source_depths_meters": sources,
        "receiver_depths_meters": receivers, "range_sample_count": count,
        "ssp_interpolation": "linear-with-in-domain-bottom-interpolation",
    }


def verify_arrival_axes(path: Path, inputs: dict[str, Any]) -> None:
    """Check output frequency/axes before handing the full parse to the C++ importer."""
    with path.open(encoding="utf-8") as handle:
        if handle.readline().strip().strip("'\"") != "2D":
            raise ValueError("Bellhop 未生成支持的二维文本到达结果")
        frequency = float(handle.readline())
        rows = [handle.readline().split() for _ in range(3)]
    expected_axes = [inputs["source_depths_meters"], inputs["receiver_depths_meters"],
                     [inputs["maximum_range_meters"] * i / (inputs["range_sample_count"] - 1)
                      for i in range(inputs["range_sample_count"])]]
    if not math.isclose(frequency, inputs["frequency_hz"], rel_tol=1e-7):
        raise ValueError("Bellhop 输出频率与输入不一致")
    for row, expected in zip(rows, expected_axes):
        if len(row) != len(expected) + 1 or int(row[0]) != len(expected):
            raise ValueError("Bellhop 输出网格与输入不一致")
        if any(not math.isclose(float(actual), target, rel_tol=1e-6, abs_tol=1e-5)
               for actual, target in zip(row[1:], expected)):
            raise ValueError("Bellhop 输出坐标与输入不一致（结果距离必须以米表示）")
