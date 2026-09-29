"""Offline spatial placement creates a new asset; Runtime still reads only points."""
from __future__ import annotations

import json
import math

from pydantic import BaseModel, ConfigDict, Field

from .catalog import CatalogError
from .trajectory_authoring import Asset, Point, Store, Track, _finalize
from .wire import FiniteFloat


class TranslateTrajectoryRequest(BaseModel):
    model_config = ConfigDict(extra="forbid", strict=True)
    source_asset_id: str = Field(pattern=r"^[A-Za-z0-9][A-Za-z0-9_.-]{0,127}$")
    source_version: int = Field(ge=1)
    source_track_id: str = Field(pattern=r"^[A-Za-z0-9_.-]{1,128}$")
    asset_id: str = Field(pattern=r"^[A-Za-z0-9][A-Za-z0-9_.-]{0,127}$")
    name: str = Field(min_length=1, max_length=160)
    x_m: FiniteFloat
    y_m: FiniteFloat
    z_m: FiniteFloat


def translate_trajectory(request: TranslateTrajectoryRequest, store: Store) -> Asset:
    try:
        source = store.get(request.source_asset_id, request.source_version)
        if (source.asset_id, source.version) != (request.source_asset_id, request.source_version):
            raise ValueError("source identity mismatch")
    except KeyError as error:
        raise CatalogError("TrajectoryNotFound", "指定的轨迹资源或版本不存在。") from error
    except (ValueError, OSError) as error:
        raise CatalogError("TrajectoryResourceInvalid", "原轨迹文件损坏或校验失败，不能派生新轨迹。") from error
    tracks = [track for track in source.tracks if track.track_id == request.source_track_id]
    if not tracks:
        raise CatalogError("TrajectoryTrackNotFound", "指定的节点轨迹不存在，不会改用其他轨迹。")
    if len(tracks) != 1:
        raise CatalogError("TrajectoryResourceInvalid", "原资源含有重复的轨迹编号，不能确定派生来源。")
    original = tracks[0]
    first = original.points[0]
    target = {axis: getattr(request, f"{axis}_m") for axis in "xyz"}
    offsets = {axis: target[axis] - getattr(first, f"{axis}_m") for axis in "xyz"}
    if not all(math.isfinite(value) for value in offsets.values()):
        raise CatalogError("InvalidRequest", "平移距离超出可表示范围，请检查起点坐标。")
    try:
        # Subtract the source origin before adding the new origin. In particular
        # the first point equals the requested origin exactly. There is no
        # resampling, time offset, controller evaluation, or Runtime transform.
        points = [Point.model_validate({
            **point.model_dump(),
            **{f"{axis}_m": target[axis] + (getattr(point, f"{axis}_m") - getattr(first, f"{axis}_m"))
               for axis in "xyz"},
        }) for point in original.points]
        notice = ("从已发布轨迹派生：仅平移空间起点，不改变轨迹时刻、姿态、速度或角速度；"
                  "原资源保留不变。")
        if source.source_notice:
            notice += " 原资源说明：" + source.source_notice
        asset = _finalize(Asset(
            asset_id=request.asset_id, version=1, name=request.name,
            coordinate_system=source.coordinate_system,
            source=json.dumps({"operation": "spatial-translation-v1",
                               "asset_id": source.asset_id, "version": source.version,
                               "track_id": original.track_id, "checksum_sha256": source.checksum_sha256,
                               "translation_m": offsets}, ensure_ascii=False, sort_keys=True, separators=(",", ":")),
            source_notice=notice,
            tracks=[Track(track_id=original.track_id, points=points)],
        ))
        return store.save(asset)
    except FileExistsError as error:
        raise CatalogError("ImmutableResourceConflict", "新轨迹编号已存在，请换一个编号；不会覆盖已有资源。") from error
    except ValueError as error:
        raise CatalogError("InvalidRequest", "平移后的轨迹出现非法坐标，未保存新资源。") from error
