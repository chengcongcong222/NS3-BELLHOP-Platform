"""Pre-run geometry checks, not a motion controller or a Runtime clock.

ProfileExtrusion is an explicitly selected 2D test-domain assumption: scene X is
distance along the saved transect, and bottom depth is invariant along scene Y.
It is not a georeferenced 3D bathymetry reconstruction or a new channel model.
"""
from bisect import bisect_right
import math

from .catalog import CatalogError


def fail(message):
    raise CatalogError("SceneDomainViolation", message)


def normalize_domain(raw, inputs):
    if raw is None:
        return None
    fields = {"model", "x_min_m", "x_max_m", "y_min_m", "y_max_m", "surface_clearance_m", "bottom_clearance_m"}
    if not isinstance(raw, dict) or set(raw) != fields or raw.get("model") != "ProfileExtrusion":
        fail("请选择明确的二维剖面试验域；不能推测三维海底范围。")
    for name in fields - {"model"}:
        try:
            valid = type(raw[name]) in (int, float) and math.isfinite(raw[name])
        except OverflowError:
            valid = False
        if not valid:
            fail("场景边界与安全距离必须为有限数字。")
    if raw["x_min_m"] >= raw["x_max_m"] or raw["y_min_m"] >= raw["y_max_m"] or min(raw["surface_clearance_m"], raw["bottom_clearance_m"]) < 0:
        fail("场景上下界或安全距离不合法。")
    terrain = input_pairs(inputs, "bathymetry", "range_meters", "depth_meters")
    if not terrain or raw["x_min_m"] < terrain[0][0] or raw["x_max_m"] > terrain[-1][0]:
        fail("场景 X 范围必须位于已保存的地形测线范围内，不能外推海底。")
    return dict(raw)


def input_pairs(inputs, name, x, y):
    payload = inputs.get("payload") if isinstance(inputs, dict) and inputs.get("available") else None
    values = payload.get(name) if isinstance(payload, dict) else None
    if not isinstance(values, list) or len(values) < 2:
        return []
    try:
        pairs = [(float(p[x]), float(p[y])) for p in values]
        if not all(math.isfinite(a) and math.isfinite(b) for a, b in pairs) or any(a[0] >= b[0] for a, b in zip(pairs, pairs[1:])):
            return []
        return pairs
    except (TypeError, KeyError, ValueError, OverflowError):
        return []


def at(points, times, t):
    i = max(0, bisect_right(times, t) - 1)
    a = points[i]
    if i == len(points) - 1 or t == times[i]:
        return (a.x_m, a.y_m, a.z_m)
    b = points[i + 1]
    ratio = (t - times[i]) / (times[i + 1] - times[i])
    return tuple(getattr(a, axis) + ratio * (getattr(b, axis) - getattr(a, axis)) for axis in ("x_m", "y_m", "z_m"))


def validate_paths(nodes, bindings, assets, environment, inputs, raw_domain, duration_ns):
    if type(duration_ns) is not int or not 0 < duration_ns <= 2**63 - 1:
        fail("运行时长必须在有效的纳秒时间范围内。")
    domain = normalize_domain(raw_domain, inputs)
    terrain = input_pairs(inputs, "bathymetry", "range_meters", "depth_meters")
    ssp = input_pairs(inputs, "sound_speed_profile", "depth_meters", "speed_meters_per_second")
    if domain and not ssp:
        fail("该环境未保存有效声速剖面，不能确认剖面试验域。")
    if environment is None and domain:
        fail("缺少正式声场范围，不能确认场景边界。")
    surface = environment.coordinate_frame.surface_z_meters if environment else 0
    if environment and environment.coordinate_frame.vertical_axis != "PositiveUp":
        fail("轨迹为 Z 向上坐标，本场景检查不支持其他垂直坐标定义。")
    terrain_x = [p[0] for p in terrain]
    def floor(x):
        i = max(0, min(len(terrain) - 2, bisect_right(terrain_x, x) - 1))
        a, b = terrain[i:i + 2]
        return a[1] + (x - a[0]) / (b[0] - a[0]) * (b[1] - a[1])
    paths = {}
    for node in nodes:
        binding = next(b for b in bindings if str(b["node_id"]) == str(node["node_id"]))
        asset = assets[(binding["asset_id"], binding["version"])]
        points = next(t.points for t in asset.tracks if t.track_id == binding["track_id"])
        times = []
        for p in points:
            nanoseconds = p.time_s * 1e9
            if not math.isfinite(nanoseconds):
                fail("轨迹时间超出有效的纳秒时间范围。")
            instant = round(nanoseconds)
            if not 0 <= instant <= 2**63 - 1:
                fail("轨迹时间超出有效的纳秒时间范围。")
            times.append(instant)
        if times[0] > 0 or times[-1] < duration_ns:
            fail(f"节点 N{node['node_id']} 的轨迹不能覆盖本次 {duration_ns / 1e9:g} 秒运行，未延长轨迹。")
        if any(a >= b for a, b in zip(times, times[1:])):
            fail("轨迹在纳秒精度下存在重复或倒序时间。")
        checks = sorted({0, duration_ns, *(t for t in times if 0 < t < duration_ns)})
        paths[str(node["node_id"])] = (points, times, checks)
        ranges = []
        if environment:
            if node["can_transmit"]: ranges.append(environment.axes.source_depth)
            if node["can_receive"]: ranges.append(environment.axes.receiver_depth)
        for index, t in enumerate(checks):
            pos = at(points, times, t)
            test_positions = [(t, pos)]
            # Crossing an interior terrain knot can hit a ridge even when both
            # trajectory endpoints are safe. Piecewise linear clearance extrema
            # occur at these crossings or segment endpoints.
            if domain and index:
                old_t = checks[index - 1]
                old = at(points, times, old_t)
                if pos[0] != old[0]:
                    lo, hi = sorted((old[0], pos[0]))
                    for x in terrain_x[bisect_right(terrain_x, lo):bisect_right(terrain_x, hi)]:
                        alpha = (x - old[0]) / (pos[0] - old[0])
                        test_positions.append((old_t + alpha * (t - old_t), tuple(a + alpha * (b - a) for a, b in zip(old, pos))))
            for when, (x, y, z) in test_positions:
                depth = surface - z
                prefix = f"节点 N{node['node_id']} 在 {when / 1e9:g} 秒："
                if depth <= 0:
                    fail(prefix + "到达或越过水面。")
                if any(depth < axis.minimum or depth > axis.maximum for axis in ranges):
                    fail(prefix + "超出声场已计算的收发深度范围。")
                if ssp and not ssp[0][0] <= depth <= ssp[-1][0]:
                    fail(prefix + "超出声速剖面的有效深度。")
                if domain:
                    if not domain["x_min_m"] <= x <= domain["x_max_m"] or not domain["y_min_m"] <= y <= domain["y_max_m"]:
                        fail(prefix + "移出所选场景范围。请调整起点、轨迹或试验域，不会自动限制节点运动。")
                    if depth < domain["surface_clearance_m"]:
                        fail(prefix + "未留足距水面安全距离。")
                    if depth >= floor(x) or depth + domain["bottom_clearance_m"] > floor(x):
                        fail(prefix + "触及海底或未留足离底安全距离。")
    if environment:
        axis = environment.axes.horizontal_range
        for sender in nodes:
            if not sender["can_transmit"]: continue
            for receiver in nodes:
                if sender["node_id"] == receiver["node_id"] or not receiver["can_receive"]: continue
                a, ta, ca = paths[str(sender["node_id"])]
                b, tb, cb = paths[str(receiver["node_id"])]
                times = sorted(set(ca + cb))
                for i, t in enumerate(times):
                    p, q = at(a, ta, t), at(b, tb, t)
                    delta = (p[0] - q[0], p[1] - q[1])
                    distances = [math.hypot(*delta)]
                    if i and axis.minimum > 0:
                        old_a, old_b = at(a, ta, times[i - 1]), at(b, tb, times[i - 1])
                        old = (old_a[0] - old_b[0], old_a[1] - old_b[1])
                        change = (delta[0] - old[0], delta[1] - old[1])
                        norm = sum(v * v for v in change)
                        alpha = max(0, min(1, -sum(u * v for u, v in zip(old, change)) / norm)) if norm else 0
                        distances.append(math.hypot(*(u + alpha * v for u, v in zip(old, change))))
                    if any(d < axis.minimum or d > axis.maximum for d in distances):
                        fail(f"节点 N{sender['node_id']} 与 N{receiver['node_id']} 的轨迹间距超出声场计算范围 {axis.minimum:g}～{axis.maximum:g} m。旁听节点也需要有效传播查询。")
    return domain
