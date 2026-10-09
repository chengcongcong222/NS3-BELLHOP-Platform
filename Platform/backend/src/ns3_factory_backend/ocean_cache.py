"""Content-checked offline hydrology profiles; never a Runtime data provider."""
from __future__ import annotations

from contextlib import contextmanager
from datetime import datetime, timezone
import hashlib
import json
import math
from pathlib import Path
import re
import tempfile
from threading import RLock
from typing import Any, Iterator


_locks_guard = RLock()
_locks: dict[str, RLock] = {}
_TRANSIENT_FIELDS = {"cache_id", "result_cache_reused", "cache_files_reused"}


def _encoded(value: Any) -> bytes:
    return json.dumps(value, ensure_ascii=False, sort_keys=True,
                      separators=(",", ":"), allow_nan=False).encode("utf-8")


@contextmanager
def hydrology_acquisition_lock(cache_root: Path) -> Iterator[None]:
    """Single flight per monthly-data directory, including different queries.

    A second coordinate in the same month must not overwrite an in-flight
    NetCDF download. The API's async lock remains responsible for its requests.
    """
    key = str(cache_root.resolve())
    with _locks_guard:
        lock = _locks.setdefault(key, RLock())
    with lock:
        yield


def woa_selection(request: dict[str, Any]) -> dict[str, Any]:
    """Every parameter affecting extraction is part of the exact cache key."""
    return {"provider_revision": "woa23-decav-1deg-mackenzie1981-v1",
            "latitude_deg": float(request["latitude_deg"]),
            "longitude_deg": float(request["longitude_deg"]),
            "month": request.get("month"),
            "maximum_depth_meters": float(request["maximum_depth_meters"]),
            "use_reference_cache": bool(request.get("use_reference_cache", False))}


def _validated_result(result: dict[str, Any]) -> dict[str, Any]:
    if not isinstance(result, dict) or not isinstance(result.get("source"), str):
        raise ValueError("水文缓存必须保留数据来源")
    cleaned = {key: value for key, value in result.items() if key not in _TRANSIENT_FIELDS}
    # Serialization also validates nested metadata and detaches mutable callers.
    cleaned = json.loads(_encoded(cleaned))
    points = cleaned.get("sound_speed_profile")
    if not isinstance(points, list) or len(points) < 2:
        raise ValueError("水文缓存至少需要两个有效声速采样点")
    previous = -1.0
    for point in points:
        if not isinstance(point, dict):
            raise ValueError("水文缓存采样点格式无效")
        depth, speed = point.get("depth_meters"), point.get("speed_meters_per_second")
        if (type(depth) not in (int, float) or type(speed) not in (int, float)
                or not math.isfinite(depth) or not math.isfinite(speed)
                or depth < 0 or depth <= previous or speed <= 0):
            raise ValueError("水文缓存深度须非负且递增，声速须为有限正数")
        previous = depth
    return cleaned


def _content(result: dict[str, Any], selection: dict[str, Any] | None) -> dict[str, Any]:
    if selection is not None and not isinstance(selection, dict):
        raise ValueError("水文缓存选择参数必须是对象或空值")
    return {"schema_version": 1, "selection": json.loads(_encoded(selection)),
            "result": _validated_result(result)}


def _record(content: dict[str, Any], created_at: str) -> dict[str, Any]:
    result = content["result"]
    points = result["sound_speed_profile"]
    return {**content, "cache_id": hashlib.sha256(_encoded(content)).hexdigest(),
            "created_at": created_at, "source": result["source"],
            "provider": result.get("provider"), "filename": result.get("filename"),
            "point_count": len(points),
            "minimum_depth_meters": points[0]["depth_meters"],
            "maximum_depth_meters": points[-1]["depth_meters"]}


def get_hydrology_profile(cache_root: Path, cache_id: str) -> dict[str, Any]:
    if not re.fullmatch(r"[a-f0-9]{64}", cache_id):
        raise ValueError("水文缓存编号无效")
    path = cache_root / f"{cache_id}.json"
    if not path.is_file():
        raise KeyError(cache_id)
    try:
        raw = json.loads(path.read_text(encoding="utf-8"))
        if (not isinstance(raw, dict) or type(raw.get("schema_version")) is not int
                or raw["schema_version"] != 1):
            raise ValueError("水文缓存版本无效")
        if not isinstance(raw.get("created_at"), str):
            raise ValueError("水文缓存保存时间无效")
        datetime.fromisoformat(raw["created_at"])
        expected = _record(_content(raw["result"], raw["selection"]), raw["created_at"])
        if raw != expected or expected["cache_id"] != cache_id:
            raise ValueError("水文缓存内容校验失败，未替换为其他剖面")
        return expected
    except (OSError, UnicodeError, KeyError, TypeError, ValueError) as error:
        raise ValueError(f"水文缓存不可读取：{error}") from error


def store_hydrology_profile(cache_root: Path, result: dict[str, Any], *,
                            selection: dict[str, Any] | None = None) -> dict[str, Any]:
    record = _record(_content(result, selection),
                     datetime.now(timezone.utc).isoformat().replace("+00:00", "Z"))
    with hydrology_acquisition_lock(cache_root):
        cache_root.mkdir(parents=True, exist_ok=True)
        target = cache_root / f"{record['cache_id']}.json"
        if target.exists():
            return get_hydrology_profile(cache_root, record["cache_id"])
        with tempfile.NamedTemporaryFile("wb", prefix=".profile-", suffix=".tmp",
                                         dir=cache_root, delete=False) as handle:
            handle.write(_encoded(record))
            temporary = Path(handle.name)
        temporary.replace(target)
    return record


def list_hydrology_profiles(cache_root: Path) -> list[dict[str, Any]]:
    records = []
    for path in sorted(cache_root.glob("*.json")):
        try:
            record = get_hydrology_profile(cache_root, path.stem)
        except (KeyError, ValueError):
            # Keep damaged/unrelated user files, but never offer them as usable.
            continue
        records.append({key: value for key, value in record.items() if key != "result"})
    return sorted(records, key=lambda record: (record["created_at"], record["cache_id"]), reverse=True)


def find_woa_profile(cache_root: Path, selection: dict[str, Any]) -> dict[str, Any] | None:
    for record in list_hydrology_profiles(cache_root):
        if record["selection"] == selection and record["source"] == "online-real-data":
            return get_hydrology_profile(cache_root, record["cache_id"])
    return None
