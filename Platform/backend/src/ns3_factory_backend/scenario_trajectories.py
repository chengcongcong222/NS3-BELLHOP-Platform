"""Compile reusable paths and scene origins into immutable, ordinary trajectories.

This runs before worker creation. No placement transform or motion model enters
Runtime; both the source and the placed asset remain independently auditable.
"""
from __future__ import annotations

import hashlib
import json
import math
import re

from .catalog import CatalogError
from .trajectory_authoring import Asset, Point, Store, Track, _finalize


def internal_placement(asset: Asset) -> bool:
    try:
        source = json.loads(asset.source)
        return isinstance(source, dict) and source.get("operation") == "scenario-placement-v1"
    except (ValueError, TypeError):
        return False


def compile_bindings(nodes: list[dict], bindings: list[dict], store: Store):
    if (not isinstance(nodes, list) or not isinstance(bindings, list)
            or any(not isinstance(node, dict) or "node_id" not in node for node in nodes)
            or any(not isinstance(binding, dict) or "node_id" not in binding for binding in bindings)):
        raise CatalogError("InvalidReference", "场景节点与轨迹绑定必须是包含节点编号的对象列表。")
    by_node = {str(node["node_id"]): node for node in nodes}
    ids = [str(binding.get("node_id")) for binding in bindings]
    if len(by_node) != len(nodes) or len(ids) != len(set(ids)) or set(ids) != set(by_node):
        raise CatalogError("InvalidReference", "每个场景节点必须且只能绑定一条轨迹。")
    compiled, assets = [], {}
    for binding in bindings:
        try:
            asset_id, track_id = binding["asset_id"], binding["track_id"]
            version = binding["version"]
            # Match trajectory::ValidId; '_' and '.' prefixes are legitimate,
            # but path components and either kind of slash are not identities.
            if (any(not isinstance(identity, str) or identity in {".", ".."}
                    or not re.fullmatch(r"[A-Za-z0-9_.-]{1,128}", identity)
                    for identity in (asset_id, track_id))
                    or type(version) is not int or version <= 0):
                raise ValueError("invalid identity")
            source = store.get(asset_id, version)
            if (source.asset_id, source.version) != (asset_id, version):
                raise ValueError("identity mismatch")
        except KeyError as error:
            raise CatalogError("TrajectoryNotFound", "绑定的轨迹资源或版本不存在。") from error
        except (ValueError, TypeError, OSError) as error:
            raise CatalogError("TrajectoryResourceInvalid", "轨迹身份、内容或校验值不合法。") from error
        tracks = [track for track in source.tracks if track.track_id == track_id]
        if len(tracks) != 1:
            raise CatalogError("TrajectoryTrackNotFound", "绑定的路径不存在或编号重复。")
        mode = binding.get("placement_mode", "Absolute")
        if mode not in {"Absolute", "RelativeToNodeStart"}:
            raise CatalogError("InvalidReference", "未知的轨迹布设方式。")
        asset = source
        if mode == "RelativeToNodeStart":
            track, origin = tracks[0], tracks[0].points[0]
            if origin.time_s != 0:
                raise CatalogError("InvalidReference", "相对轨迹必须从仿真 0 秒开始。")
            try:
                position = by_node[str(binding["node_id"])]["initial_position"]
                target = {axis: float(position[f"{axis}_meters"]) for axis in "xyz"}
                if not all(math.isfinite(value) for value in target.values()):
                    raise ValueError("nonfinite origin")
                provenance = {"operation": "scenario-placement-v1", "asset_id": source.asset_id,
                    "version": source.version, "track_id": track_id,
                    "checksum_sha256": source.checksum_sha256, "scene_origin_m": target}
                canonical = json.dumps(provenance, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
                placed_id = "scene-path-" + hashlib.sha256(canonical.encode()).hexdigest()
                points = [Point.model_validate({**point.model_dump(), **{
                    f"{axis}_m": target[axis] + (getattr(point, f"{axis}_m") - getattr(origin, f"{axis}_m"))
                    for axis in "xyz"}}) for point in track.points]
                asset = _finalize(Asset(asset_id=placed_id, version=1,
                    name="场景布设轨迹", source=canonical, source_notice=source.source_notice,
                    tracks=[Track(track_id=track_id, points=points)]))
                try:
                    store.save(asset)
                except FileExistsError:
                    existing = store.get(asset.asset_id, asset.version)
                    if existing != asset:
                        raise ValueError("placed asset mismatch")
            except (KeyError, TypeError, ValueError, OSError) as error:
                raise CatalogError("TrajectoryPlacementInvalid", "节点起点或布设后的轨迹不合法，未启动运行。") from error
        compiled.append({"node_id": binding["node_id"], "asset_id": asset.asset_id,
                         "version": asset.version, "track_id": track_id})
        assets[(asset.asset_id, asset.version)] = asset
    return compiled, assets
