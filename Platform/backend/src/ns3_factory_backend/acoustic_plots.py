"""Read-only Bellhop plot adapter. Never used by Runtime/channel queries.

ASCII 2D arrivals layout follows the existing Platform importer. Ray layout
follows bellhopcuda src/mode/ray.hpp OpenRAYFile/WriteRay (metres/degrees).
No spatial interpolation, synthetic ray or online Bellhop calculation.
"""
from __future__ import annotations

import hashlib
import math
from pathlib import Path


def _document(path: Path, expected_sha: str | None = None) -> tuple[list[str], str]:
    if path.stat().st_size > 32 * 1024 * 1024:
        raise ValueError("图形源文件超过 32 MiB，请降低离线采样密度")
    raw = path.read_bytes()
    if len(raw) > 32 * 1024 * 1024:
        raise ValueError("图形源文件超过 32 MiB，请降低离线采样密度")
    sha = hashlib.sha256(raw).hexdigest()
    if expected_sha is not None and sha != expected_sha:
        raise ValueError("声学图形源文件校验失败，不能展示已改变的结果")
    return raw.decode("utf-8").splitlines(), sha


def arrivals_plot(path: Path, expected_sha: str | None, source_index: int, receiver_index: int, range_index: int) -> dict:
    lines, sha = _document(path, expected_sha)
    lines = iter(line.strip() for line in lines if line.strip())
    def values() -> list[float]:
        try:
            row = [float(x.replace("D", "E").replace("d", "e")) for x in next(lines).split()]
        except (StopIteration, ValueError) as error:
            raise ValueError("Bellhop 到达数据不完整") from error
        if not row or not all(math.isfinite(x) for x in row):
            raise ValueError("Bellhop 到达数据包含非法值")
        return row
    def count() -> int:
        row = values()
        if len(row) != 1 or row[0] != int(row[0]) or not 0 <= row[0] <= 100000:
            raise ValueError("Bellhop 到达数量非法")
        return int(row[0])
    def axis() -> list[float]:
        row = values()
        if row[0] != len(row) - 1 or len(row) < 2 or row[1] < 0 or any(a >= b for a, b in zip(row[1:], row[2:])):
            raise ValueError("Bellhop 采样轴非法")
        return row[1:]
    if next(lines, "").strip("'\"") != "2D":
        raise ValueError("只支持 Bellhop 二维文本到达产物")
    frequency = values()
    if len(frequency) != 1 or frequency[0] <= 0:
        raise ValueError("Bellhop 频率非法")
    sources, receivers, ranges = axis(), axis(), axis()
    if len(sources) * len(receivers) * len(ranges) > 200000:
        raise ValueError("声学采样网格过大")
    if not (0 <= source_index < len(sources) and 0 <= receiver_index < len(receivers) and 0 <= range_index < len(ranges)):
        raise ValueError("请选择存在的发送深度、接收深度和距离采样，不能外推")
    grid, selected = [], []
    for si, _ in enumerate(sources):
        maximum = count()
        for ri, _ in enumerate(receivers):
            for xi, _ in enumerate(ranges):
                n = count()
                if n > maximum:
                    raise ValueError("到达数量超过文件声明")
                power, paths = 0.0, []
                for _ in range(n):
                    p = values()
                    if len(p) != 8 or p[0] < 0 or p[2] < 0 or p[3] != 0 or any(v < 0 or v != int(v) for v in p[6:]):
                        raise ValueError("不支持或非法的到达路径记录")
                    power += p[0] * p[0]
                    if (si, ri, xi) == (source_index, receiver_index, range_index):
                        paths.append({"delay_s": p[2], "pressure_gain": p[0], "phase_degrees": p[1],
                                      "surface_bounces": int(p[6]), "bottom_bounces": int(p[7])})
                if not math.isfinite(power):
                    raise ValueError("多径能量溢出")
                if n > 0 and power <= 0:
                    raise ValueError("到达记录存在但总能量为零或下溢，不能标记为无到达")
                if si == source_index:
                    grid.append({"receiver_index": ri, "range_index": xi, "tl_db": -10 * math.log10(power) if power > 0 else None})
                if (si, ri, xi) == (source_index, receiver_index, range_index):
                    selected = sorted(paths, key=lambda p: (p["delay_s"], p["pressure_gain"], p["phase_degrees"]))
    if next(lines, None) is not None:
        raise ValueError("Bellhop 到达文件含多余记录")
    return {"available": True, "frequency_hz": frequency[0], "source_depths_m": sources,
            "receiver_depths_m": receivers, "ranges_m": ranges,
            "selection": {"source_index": source_index, "receiver_index": receiver_index, "range_index": range_index},
            "impulse": selected, "field": grid, "arrivals_sha256": sha,
            "tl_definition": "非相干多径能量损失：-10 log10 Σ|a|²；不是相干声场，不补齐无到达网格"}


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _display_points(points: list[list[float]]) -> list[list[float]]:
    """Thin only the drawing, retaining actual vertices and turning points."""
    if len(points) <= 1200:
        return points
    keep = set(range(0, len(points), math.ceil(len(points) / 1200))) | {len(points) - 1}
    for axis in (0, 1):
        direction = 0
        for i in range(1, len(points)):
            delta = points[i][axis] - points[i - 1][axis]
            next_direction = (delta > 0) - (delta < 0)
            if next_direction:
                if direction and direction != next_direction:
                    keep.update((i - 1, i))
                direction = next_direction
    if len(keep) > 8000:
        raise ValueError("单条声线转折过多，超出显示规模")
    return [points[i] for i in sorted(keep)]


def ray_plot(path: Path, expected_sha: str, source_depth_m: float | None = None) -> dict:
    # Read one trace at a time, not the entire file into Python strings/lists.
    # This is a renderer budget, independent of the authoritative arrival field.
    limit = 256 * 1024 * 1024
    if path.stat().st_size > limit:
        raise ValueError("声线文件超过当前绘图读取上限 256 MiB")
    digest = hashlib.sha256()
    def records(stream):
        size = 0
        while raw := stream.readline(65537):
            size += len(raw)
            if len(raw) > 65536 or size > limit:
                raise ValueError("声线文件超出绘图读取规模")
            digest.update(raw)
            line = raw.decode("utf-8").strip()
            if line:
                yield line
    stream = path.open("rb")
    rows = records(stream)
    try:
        next(rows)  # upstream title
        frequency = float(next(rows))
        sources = [int(v) for v in next(rows).split()]
        beams = [int(v) for v in next(rows).split()]
        top, bottom = float(next(rows)), float(next(rows))
        if (next(rows).strip("'\"") != "rz" or len(sources) != 3
                or sources[:2] != [1, 1] or not 1 <= sources[2] <= 64
                or len(beams) != 2 or any(not 1 <= v <= 100000 for v in beams)):
            raise ValueError("不支持的声线坐标或声源布局")
        rays, total, selected_count, display_count = [], 0, 0, 0
        origins = set()
        selected_depth = source_depth_m
        if selected_depth is not None and (type(selected_depth) not in (int, float) or not math.isfinite(selected_depth)):
            raise ValueError("声源深度必须为有限数字")
        for angle_text in rows:
            angle = float(angle_text)
            n, surface, seabed = [int(v) for v in next(rows).split()]
            total += n
            if not 1 <= n <= 250000 or total > 8000000 or min(surface, seabed) < 0:
                raise ValueError("声线图超出安全采样规模")
            points = [[float(v) for v in next(rows).split()] for _ in range(n)]
            if any(len(p) != 2 or not all(math.isfinite(v) for v in p) for p in points) or not math.isfinite(angle):
                raise ValueError("非法声线采样")
            origin = tuple(points[0])
            origins.add(origin)
            if source_depth_m is None and (selected_depth is None or origin[1] < selected_depth):
                selected_depth, rays, selected_count, display_count = origin[1], [], 0, 0
            if math.isclose(origin[1], selected_depth, rel_tol=1e-6, abs_tol=1e-5):
                display = _display_points(points)
                selected_count += n
                display_count += len(display)
                if display_count > 100000:
                    raise ValueError("所选深度的声线超出显示规模")
                rays.append({"angle_degrees": angle, "points": display})
        if (not origins or not all(math.isfinite(v) for v in [frequency, top, bottom])
                or frequency <= 0 or bottom <= top):
            raise ValueError("没有有效声线")
        # Upstream .ray records carry their actual source as the first point.
        # Group by those saved origins, never by ray order or an assumed beam
        # count: upstream omits rays that did not produce a valid trace.
        if (len(origins) != sources[2] or len({p[0] for p in origins}) != 1
                or any(not top < p[1] < bottom for p in origins)):
            raise ValueError("声线的实际发射位置与声明的源深度数量不一致")
        depths = sorted(p[1] for p in origins)
        matching = [depth for depth in depths if math.isclose(depth, selected_depth, rel_tol=1e-6, abs_tol=1e-5)]
        if len(matching) > 1:
            raise ValueError("声源深度精度不足，无法区分已保存声线")
    except (ValueError, StopIteration) as error:
        raise ValueError("Bellhop 声线数据格式不完整或不受支持") from error
    finally:
        rows.close()
        stream.close()
    sha = digest.hexdigest()
    if sha != expected_sha:
        raise ValueError("声学图形源文件校验失败，不能展示已改变的结果")
    if not matching:
        return {"available": False, "source_depths_m": depths,
                "reason": f"未保存发射深度 {selected_depth:g} m 的声线。此产物只包含 "
                          + "、".join(f"{depth:g} m" for depth in depths)
                          + "；请另存并重新计算以生成所需声源的真实声线。"}
    return {"available": True, "frequency_hz": frequency, "rays": rays, "sha256": sha,
            "source_depth_m": matching[0], "source_depths_m": depths,
            "display_sampling": {"original_points": selected_count, "displayed_points": display_count,
                                 "method": "saved-vertices-with-turning-points"}}


def rays_only_plot(artifact: dict, source_index: int) -> dict:
    """No arrivals body read, no TL/impulse calculation, no Bellhop invocation."""
    summary = artifact.get("input_summary", {})
    sources, frequency = summary.get("source_depths_meters"), summary.get("frequency_hz")
    if sources is None or frequency is None:
        # Compatibility for old artifacts without an input manifest: bounded
        # header only, cross-checked against the independently hashed .ray.
        with Path(artifact["arrivals_path"]).open("rb") as stream:
            header = [stream.readline(65537) for _ in range(3)]
        if any(len(line) > 65536 for line in header) or header[0].strip().strip(b"'\"") != b"2D":
            raise ValueError("无法读取历史声场的发射深度")
        frequency = float(header[1])
        axis = [float(v) for v in header[2].split()]
        if not axis or axis[0] != len(axis) - 1:
            raise ValueError("声场发射深度数量不一致")
        sources = axis[1:]
    if (not isinstance(sources, list) or not 1 <= len(sources) <= 64
            or any(type(v) not in (int, float) or not math.isfinite(v) or v <= 0 for v in sources)
            or any(a >= b for a, b in zip(sources, sources[1:]))
            or type(frequency) not in (int, float) or not math.isfinite(frequency) or frequency <= 0):
        raise ValueError("声场输入摘要中的频率或发射深度无效")
    if not 0 <= source_index < len(sources):
        raise ValueError("请选择已计算的发射深度，不能外推")
    return {"available": True, "frequency_hz": frequency, "source_depths_m": sources,
            "arrivals_sha256": artifact.get("arrivals_sha256"),
            "ray_plot": ray_plot_for_source(artifact.get("ray_plot", {}), sources[source_index], frequency)}


def ray_plot_for_source(artifact: dict, source_depth_m: float, frequency_hz: float) -> dict:
    """Select an already-computed source; GET requests never invoke Bellhop."""
    if not artifact.get("available"):
        return {"available": False, "reason": artifact.get(
            "reason", "本环境未保留声线产物；请另存并重新计算")}
    try:
        selected = ray_plot(Path(artifact["path"]), artifact["sha256"], source_depth_m)
        if selected["available"] and not math.isclose(selected["frequency_hz"], frequency_hz, rel_tol=1e-7):
            raise ValueError("声线频率与到达产物不一致")
        return selected
    except (ValueError, OSError, KeyError, TypeError):
        # Auxiliary plots do not invalidate the separately verified arrivals.
        return {"available": False, "reason": "声线产物校验或格式无效；请另存并重新计算，不以其他深度或示意曲线替代。"}


def produce_ray_plot(payload: dict, job_root: Path, executable: Path) -> dict:
    """Additional offline observation job; the main arrivals input is untouched."""
    import copy
    import subprocess
    from .environment_inputs import write_bellhop_inputs
    inputs = copy.deepcopy(payload)
    inputs["bellhop"]["beam_count"] = min(31, inputs["bellhop"]["beam_count"])
    root = job_root / "ray-preview"
    try:
        write_bellhop_inputs(inputs, root)
        path = root / "environment.env"
        text = path.read_text()
        if text.count("\n'A'\n") != 1:
            raise ValueError("无法辨认声线计算模式")
        path.write_text(text.replace("\n'A'\n", "\n'R'\n"))
        command = [str(executable)] + (["-1"] if executable.name.startswith("bellhopcxx") else []) + ["environment"]
        result = subprocess.run(command, cwd=root, capture_output=True, text=True, timeout=120, check=False)
        if result.returncode != 0:
            raise ValueError("声线辅助计算未完成")
        ray = root / "environment.ray"
        sha = file_sha256(ray)
        parsed = ray_plot(ray, sha)  # validate every saved source before publishing
        expected = inputs["bellhop"]["source_depths_meters"]
        if (len(parsed["source_depths_m"]) != len(expected)
                or any(not math.isclose(actual, target, rel_tol=1e-6, abs_tol=1e-5)
                       for actual, target in zip(parsed["source_depths_m"], expected))
                or not math.isclose(parsed["frequency_hz"], inputs["center_frequency_hz"], rel_tol=1e-7)):
            raise ValueError("声线输出与离线输入不一致")
        return {"available": True, "path": str(ray.resolve()), "sha256": sha}
    except (ValueError, OSError, subprocess.TimeoutExpired):
        return {"available": False, "reason": "本次声线辅助图未生成；到达结果独立保存，不以示意曲线替代"}
