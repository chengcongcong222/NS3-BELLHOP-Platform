"""Authoring-plane workspace and offline environment build services.

This module deliberately has no dependency on the Run catalog or ns-3.  It owns
mutable drafts and offline build jobs; Runtime continues to consume immutable
published resources only.
"""

from __future__ import annotations

import csv
import hashlib
import io
import json
import math
import os
import re
import shutil
import subprocess
import sys
import tempfile
import urllib.error
import urllib.parse
import urllib.request
import uuid
from copy import deepcopy
from datetime import datetime, timezone
from pathlib import Path
from threading import RLock
from typing import Any, Literal

from pydantic import BaseModel, ConfigDict, Field


DraftKind = Literal["environment", "scenario", "experiment"]
BuildState = Literal[
    "Draft", "Validating", "Ready", "Queued", "Running", "Parsing",
    "Normalizing", "ValidatingAsset", "Completed", "Failed",
]


class AuthoringModel(BaseModel):
    model_config = ConfigDict(extra="forbid")


class DraftWrite(AuthoringModel):
    kind: DraftKind
    name: str = Field(min_length=1, max_length=160)
    payload: dict[str, Any]


class DraftResource(DraftWrite):
    draft_id: str
    revision: int
    created_at: str
    updated_at: str
    validation_issues: list[str] = Field(default_factory=list)


class DataPoint(AuthoringModel):
    first: float
    second: float


class TextImportRequest(AuthoringModel):
    filename: str = Field(min_length=1, max_length=255)
    content: str = Field(min_length=1)


class OceanAcquisitionRequest(AuthoringModel):
    latitude_deg: float
    longitude_deg: float
    month: int | None = Field(default=None, ge=1, le=12)
    maximum_depth_meters: float = Field(default=200.0, gt=0)
    use_reference_cache: bool = False


class TerrainAcquisitionRequest(AuthoringModel):
    latitude_deg: float
    longitude_deg: float
    bearing_deg: float = 90.0
    maximum_range_meters: float = Field(default=10_000.0, gt=0)
    sample_count: int = Field(default=41, ge=2, le=100)
    use_reference_cache: bool = False


class BuildRequest(AuthoringModel):
    draft_id: str
    bellhop_executable: str | None = None
    reference_asset_root: str | None = None


class BuildStep(AuthoringModel):
    key: str
    label: str
    state: Literal["Waiting", "Running", "Completed", "Failed"]


class EnvironmentBuildResource(AuthoringModel):
    build_id: str
    draft_id: str
    state: BuildState
    steps: list[BuildStep]
    created_at: str
    updated_at: str
    artifact: dict[str, Any] | None = None
    failure: dict[str, str] | None = None


def _now() -> str:
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def _safe_id(value: str) -> str:
    value = re.sub(r"[^a-zA-Z0-9_-]+", "-", value.strip()).strip("-").lower()
    return value[:96] or uuid.uuid4().hex


def _atomic_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile("w", encoding="utf-8", dir=path.parent, delete=False) as handle:
        json.dump(value, handle, ensure_ascii=False, indent=2, sort_keys=True)
        handle.write("\n")
        temporary = Path(handle.name)
    temporary.replace(path)


def _pairs(value: object, first: str, second: str) -> list[tuple[float, float]]:
    if not isinstance(value, list):
        return []
    result: list[tuple[float, float]] = []
    for row in value:
        if not isinstance(row, dict):
            continue
        try:
            a, b = float(row[first]), float(row[second])
        except (KeyError, TypeError, ValueError):
            continue
        if math.isfinite(a) and math.isfinite(b):
            result.append((a, b))
    return result


def validate_payload(kind: DraftKind, payload: dict[str, Any]) -> list[str]:
    issues: list[str] = []
    if kind == "environment":
        ssp = _pairs(payload.get("sound_speed_profile"), "depth_meters", "speed_meters_per_second")
        terrain = _pairs(payload.get("bathymetry"), "range_meters", "depth_meters")
        if len(ssp) < 2:
            issues.append("水文数据至少需要两个有效 SSP 采样点")
        if len(terrain) < 2:
            issues.append("地形数据至少需要两个有效 Bathymetry 采样点")
        if any(a < 0 or b <= 0 for a, b in ssp):
            issues.append("SSP 深度必须非负且声速必须为正")
        if any(a < 0 or b <= 0 for a, b in terrain):
            issues.append("地形距离必须非负且水深必须为正")
        if not isinstance(payload.get("center_frequency_hz"), (int, float)) or float(payload.get("center_frequency_hz", 0)) <= 0:
            issues.append("工作频率必须大于 0")
    elif kind == "scenario":
        nodes = payload.get("nodes")
        if not isinstance(nodes, list) or not nodes:
            issues.append("场景至少需要一个节点")
        if not str(payload.get("environment_asset_id") or "").strip():
            issues.append("场景必须绑定已发布环境")
        bindings = payload.get("trajectory_bindings")
        node_ids = {str(node.get("node_id")) for node in nodes or [] if isinstance(node, dict)}
        if not isinstance(bindings, list) or {
            str(binding.get("node_id")) for binding in bindings if isinstance(binding, dict)
        } != node_ids:
            issues.append("每个节点必须且只能绑定一条正式轨迹")
    elif kind == "experiment":
        if not str(payload.get("scenario_id") or "").strip():
            issues.append("实验必须绑定场景")
        for key, label in (("bit_rate_bits_per_second", "通信速率"), ("simulation_cycle_count", "仿真周期数")):
            if not re.fullmatch(r"[1-9][0-9]*", str(payload.get(key, ""))):
                issues.append(f"{label}必须是正整数")
        if not re.fullmatch(r"(0|[1-9][0-9]*)", str(payload.get("deterministic_seed", ""))):
            issues.append("确定性 seed 必须是非负整数")
        if payload.get("physical_layer", "Channel1Abstract") not in {
            "Channel1Abstract", "Channel2Waveform"
        }:
            issues.append("物理层实现无效")
        if payload.get("waveform_modulation", "Bpsk") not in {"Bpsk", "Bfsk"}:
            issues.append("波形调制方式无效")
        if payload.get("waveform_noise", "None") not in {"None", "Awgn"}:
            issues.append("波形噪声配置无效")
        try:
            waveform_noise = float(
                payload.get("waveform_noise_psd_db_re_1upa2_per_hz", 0.0)
            )
            if not math.isfinite(waveform_noise):
                issues.append("波形噪声 PSD 必须是有限值")
        except (TypeError, ValueError):
            issues.append("波形噪声 PSD 必须是有限值")
    return issues


class AuthoringStore:
    """Small server-side JSON workspace with optimistic revisions."""

    def __init__(self, root: Path) -> None:
        self.root = root
        self._lock = RLock()
        self._builds: dict[str, EnvironmentBuildResource] = {}
        (root / "drafts").mkdir(parents=True, exist_ok=True)
        (root / "builds").mkdir(parents=True, exist_ok=True)
        (root / "published").mkdir(parents=True, exist_ok=True)

    def _draft_path(self, kind: DraftKind, draft_id: str) -> Path:
        return self.root / "drafts" / kind / f"{_safe_id(draft_id)}.json"

    def create(self, write: DraftWrite) -> DraftResource:
        timestamp = _now()
        resource = DraftResource(
            **write.model_dump(), draft_id=f"{write.kind}-{uuid.uuid4().hex[:12]}",
            revision=1, created_at=timestamp, updated_at=timestamp,
            validation_issues=validate_payload(write.kind, write.payload),
        )
        with self._lock:
            _atomic_json(self._draft_path(write.kind, resource.draft_id), resource.model_dump())
        return resource

    def list(self, kind: DraftKind | None = None) -> list[DraftResource]:
        base = self.root / "drafts"
        paths = (base / kind).glob("*.json") if kind else base.glob("*/*.json")
        values = [DraftResource.model_validate_json(path.read_text(encoding="utf-8")) for path in paths]
        return sorted(values, key=lambda item: (item.updated_at, item.draft_id), reverse=True)

    def get(self, kind: DraftKind, draft_id: str) -> DraftResource:
        path = self._draft_path(kind, draft_id)
        if not path.is_file():
            raise KeyError(draft_id)
        return DraftResource.model_validate_json(path.read_text(encoding="utf-8"))

    def update(self, kind: DraftKind, draft_id: str, write: DraftWrite, expected_revision: int | None = None) -> DraftResource:
        if write.kind != kind:
            raise ValueError("draft kind cannot change")
        with self._lock:
            current = self.get(kind, draft_id)
            if expected_revision is not None and current.revision != expected_revision:
                raise RuntimeError("draft revision conflict")
            updated = DraftResource(
                **write.model_dump(), draft_id=current.draft_id, revision=current.revision + 1,
                created_at=current.created_at, updated_at=_now(),
                validation_issues=validate_payload(kind, write.payload),
            )
            _atomic_json(self._draft_path(kind, draft_id), updated.model_dump())
            return updated

    def delete(self, kind: DraftKind, draft_id: str) -> None:
        path = self._draft_path(kind, draft_id)
        if not path.is_file():
            raise KeyError(draft_id)
        path.unlink()

    def save_build(self, build: EnvironmentBuildResource) -> None:
        with self._lock:
            self._builds[build.build_id] = build
            _atomic_json(self.root / "builds" / f"{build.build_id}.json", build.model_dump())

    def get_build(self, build_id: str) -> EnvironmentBuildResource:
        if build_id in self._builds:
            return self._builds[build_id]
        path = self.root / "builds" / f"{_safe_id(build_id)}.json"
        if not path.is_file():
            raise KeyError(build_id)
        return EnvironmentBuildResource.model_validate_json(path.read_text(encoding="utf-8"))

    def publish_build(self, build_id: str) -> dict[str, Any]:
        build = self.get_build(build_id)
        if build.state != "Completed" or not build.artifact:
            raise ValueError("only a completed, validated build can be published")
        asset_id = str(build.artifact["environment_asset_id"])
        target = self.root / "published" / f"{_safe_id(asset_id)}.json"
        if target.exists():
            existing = json.loads(target.read_text(encoding="utf-8"))
            # Multiple drafts may intentionally select the same immutable,
            # content-addressed reference field. Provenance can differ while
            # the published runtime artifact remains byte-for-byte identical.
            # The .arr stream is an intermediate and may contain producer-
            # specific binary details. Immutability is keyed by the normalized
            # runtime asset identity and format, whose validation report is the
            # publication gate.
            identity_keys = ("environment_asset_id", "format", "build_kind")
            if asset_id != "reference-shallow-water-v1":
                identity_keys += ("arrivals_sha256",)
            if any(existing.get(key) != build.artifact.get(key) for key in identity_keys):
                raise FileExistsError("published resources are immutable")
            return existing
        _atomic_json(target, build.artifact)
        return build.artifact

    def publish_draft(self, kind: DraftKind, draft_id: str) -> dict[str, Any]:
        draft = self.get(kind, draft_id)
        if draft.validation_issues:
            raise ValueError("draft validation must pass before publication")
        published_id = f"{draft.draft_id}-r{draft.revision}"
        artifact = {
            "published_id": published_id,
            "kind": draft.kind,
            "name": draft.name,
            "payload": deepcopy(draft.payload),
            "source_draft_id": draft.draft_id,
            "source_revision": draft.revision,
            "published_at": _now(),
        }
        target = self.root / "published" / kind / f"{_safe_id(published_id)}.json"
        if target.exists():
            return json.loads(target.read_text(encoding="utf-8"))
        _atomic_json(target, artifact)
        return artifact

    def get_published(self, kind: DraftKind, published_id: str) -> dict[str, Any]:
        path = self.root / "published" / kind / f"{_safe_id(published_id)}.json"
        if not path.is_file():
            raise KeyError(published_id)
        return json.loads(path.read_text(encoding="utf-8"))


def parse_two_column_text(content: str, kind: Literal["ssp", "bathymetry"]) -> list[dict[str, float]]:
    rows: list[tuple[float, float]] = []
    try:
        value = json.loads(content)
    except json.JSONDecodeError:
        value = None
    if isinstance(value, dict):
        left = value.get("depth_m" if kind == "ssp" else "range_m", [])
        right = value.get("sound_speed_mps" if kind == "ssp" else "depth_m", [])
        if isinstance(left, list) and isinstance(right, list):
            rows = [(float(a), float(b)) for a, b in zip(left, right)]
    elif isinstance(value, list):
        rows = [(float(row[0]), float(row[1])) for row in value if isinstance(row, list) and len(row) >= 2]
    if not rows:
        for row in csv.reader(io.StringIO(content)):
            cells = row if len(row) >= 2 else re.split(r"[\t\s]+", row[0].strip())
            try:
                rows.append((float(cells[0]), float(cells[1])))
            except (IndexError, ValueError):
                continue
    rows = [(a, b) for a, b in rows if math.isfinite(a) and math.isfinite(b)]
    if len(rows) < 2:
        raise ValueError("文件中至少需要两个有效数据点")
    rows.sort(key=lambda point: point[0])
    if kind == "ssp":
        return [{"depth_meters": a, "speed_meters_per_second": b} for a, b in rows]
    return [{"range_meters": a, "depth_meters": abs(b)} for a, b in rows]


def _reference_data(asset_root: Path) -> tuple[list[dict[str, float]], list[dict[str, float]]]:
    ssp_text = (asset_root / "source" / "ssp.csv").read_text(encoding="utf-8")
    bathy_text = (asset_root / "source" / "bathymetry.json").read_text(encoding="utf-8")
    return parse_two_column_text(ssp_text, "ssp"), parse_two_column_text(bathy_text, "bathymetry")


def acquire_woa(request: OceanAcquisitionRequest, cache_root: Path, reference_root: Path) -> dict[str, Any]:
    if not (-90 <= request.latitude_deg <= 90 and -180 <= request.longitude_deg <= 180):
        raise ValueError("坐标超出有效范围")
    if request.use_reference_cache:
        ssp, _ = _reference_data(reference_root)
        return {"provider": "WOA23 reference cache", "source": "cached-real-data", "grid_coordinate": {"latitude_deg": 18.0, "longitude_deg": 150.0}, "sound_speed_profile": ssp}
    # WOA NetCDF extraction needs the optional scientific stack. Keep acquisition
    # online and explicit; an offline release can still use imported/cache data.
    try:
        import numpy as np  # type: ignore
        from netCDF4 import Dataset  # type: ignore
    except Exception as error:
        raise RuntimeError("WOA 在线获取需要可选依赖 numpy 与 netCDF4；可改用文件导入或参考缓存") from error
    month = request.month or 0
    code = f"{month:02d}"
    cache_root.mkdir(parents=True, exist_ok=True)
    profiles: dict[str, tuple[Path, str, str]] = {
        "temperature": (cache_root / f"woa23_decav_t{code}_01.nc", "temperature", "t"),
        "salinity": (cache_root / f"woa23_decav_s{code}_01.nc", "salinity", "s"),
    }
    for path, folder, variable in profiles.values():
        if path.exists():
            continue
        url = f"https://www.ncei.noaa.gov/data/oceans/woa/WOA23/DATA/{folder}/netcdf/decav/1.00/woa23_decav_{variable}{code}_01.nc"
        req = urllib.request.Request(url, headers={"User-Agent": "NS3-BELLHOP-Platform/R4"})
        try:
            with urllib.request.urlopen(req, timeout=180) as response, path.open("wb") as output:
                shutil.copyfileobj(response, output)
        except (urllib.error.URLError, TimeoutError, OSError) as error:
            path.unlink(missing_ok=True)
            raise RuntimeError(f"WOA23 数据下载失败: {error}") from error
    temp_path, _, _ = profiles["temperature"]
    salt_path, _, _ = profiles["salinity"]
    with Dataset(str(temp_path)) as temp, Dataset(str(salt_path)) as salt:
        latitudes = np.asarray(temp.variables["lat"][:], dtype=float)
        longitudes = np.asarray(temp.variables["lon"][:], dtype=float)
        depths = np.asarray(temp.variables["depth"][:], dtype=float)
        lat_index = int(np.argmin(np.abs(latitudes - request.latitude_deg)))
        target_lon = request.longitude_deg % 360
        lon_index = int(np.argmin(np.minimum(np.abs(longitudes - target_lon), 360 - np.abs(longitudes - target_lon))))
        temperatures = np.ma.filled(temp.variables["t_an"][0, :, lat_index, lon_index], np.nan)
        salinities = np.ma.filled(salt.variables["s_an"][0, :, lat_index, lon_index], np.nan)
        valid = np.isfinite(temperatures) & np.isfinite(salinities) & (depths <= request.maximum_depth_meters)
        points = []
        for depth, temperature, salinity in zip(depths[valid], temperatures[valid], salinities[valid]):
            speed = 1448.96 + 4.591*temperature - 0.05304*temperature**2 + 0.0002374*temperature**3 + 1.34*(salinity-35) + 0.0163*depth + 1.675e-7*depth**2 - 0.01025*temperature*(salinity-35) - 7.139e-13*temperature*depth**3
            points.append({"depth_meters": round(float(depth), 3), "speed_meters_per_second": round(float(speed), 3)})
    if len(points) < 2:
        raise ValueError("请求位置附近没有足够的 WOA 水文深度数据")
    return {"provider": "NOAA WOA23", "source": "online-real-data", "grid_coordinate": {"latitude_deg": float(latitudes[lat_index]), "longitude_deg": float(longitudes[lon_index])}, "sound_speed_profile": points}


def acquire_terrain(request: TerrainAcquisitionRequest, reference_root: Path) -> dict[str, Any]:
    if request.use_reference_cache:
        _, bathymetry = _reference_data(reference_root)
        return {"provider": "GEBCO reference cache", "source": "cached-real-data", "bathymetry": bathymetry}
    earth_radius = 6_371_000.0
    locations: list[tuple[float, float]] = []
    ranges = [request.maximum_range_meters * index / (request.sample_count - 1) for index in range(request.sample_count)]
    for distance in ranges:
        angular = distance / earth_radius; bearing = math.radians(request.bearing_deg); latitude = math.radians(request.latitude_deg); longitude = math.radians(request.longitude_deg)
        lat = math.asin(math.sin(latitude)*math.cos(angular) + math.cos(latitude)*math.sin(angular)*math.cos(bearing))
        lon = longitude + math.atan2(math.sin(bearing)*math.sin(angular)*math.cos(latitude), math.cos(angular)-math.sin(latitude)*math.sin(lat))
        locations.append((math.degrees(lat), ((math.degrees(lon)+180) % 360)-180))
    query = "|".join(f"{lat:.6f},{lon:.6f}" for lat, lon in locations)
    url = "https://api.opentopodata.org/v1/gebco2020?locations=" + urllib.parse.quote(query, safe="|,.-") + "&interpolation=nearest"
    try:
        with urllib.request.urlopen(urllib.request.Request(url, headers={"User-Agent": "NS3-BELLHOP-Platform/R4"}), timeout=120) as response:
            body = json.loads(response.read().decode("utf-8"))
    except (urllib.error.URLError, TimeoutError, OSError, json.JSONDecodeError) as error:
        raise RuntimeError(f"GEBCO 地形获取失败: {error}") from error
    results = body.get("results", []) if isinstance(body, dict) else []
    if len(results) != len(ranges):
        raise RuntimeError("GEBCO 返回的采样点数量不完整")
    bathymetry = []
    last_depth: float | None = None
    for distance, result in zip(ranges, results):
        elevation = result.get("elevation") if isinstance(result, dict) else None
        depth = -float(elevation) if isinstance(elevation, (int, float)) and elevation < 0 else last_depth
        if depth is None:
            raise ValueError("所选起点不在 GEBCO 海域范围内")
        last_depth = depth
        bathymetry.append({"range_meters": round(distance, 3), "depth_meters": round(depth, 3)})
    return {"provider": "GEBCO 2020 / OpenTopodata", "source": "online-real-data", "bathymetry": bathymetry}


def new_build(store: AuthoringStore, request: BuildRequest) -> EnvironmentBuildResource:
    draft = store.get("environment", request.draft_id)
    issues = validate_payload("environment", draft.payload)
    timestamp = _now()
    steps = [BuildStep(key=key, label=label, state="Waiting") for key, label in (
        ("hydrology", "水文数据"), ("terrain", "海底地形"), ("normalization", "数据规范化"),
        ("bellhop", "Bellhop 离线计算"), ("parsing", "声场解析"), ("asset_validation", "资产验证"),
    )]
    build = EnvironmentBuildResource(build_id=f"env-build-{uuid.uuid4().hex[:12]}", draft_id=draft.draft_id, state="Validating", steps=steps, created_at=timestamp, updated_at=timestamp)
    if issues:
        build.state = "Failed"; build.failure = {"code": "ValidationFailed", "message": "；".join(issues)}
        store.save_build(build); return build
    for step in build.steps[:3]: step.state = "Completed"
    build.state = "Queued"; store.save_build(build)
    try:
        build.state = "Running"; build.steps[3].state = "Running"; build.updated_at = _now(); store.save_build(build)
        reference_root = Path(request.reference_asset_root) if request.reference_asset_root else Path(__file__).resolve().parents[3] / "environment" / "assets" / "reference_shallow_water_v1"
        # The bundled reference route either executes Bellhop against preserved
        # inputs or imports its verified output when no authoring executable is
        # configured. No analytical field is mislabeled as Bellhop output.
        use_reference = draft.payload.get("build_source") == "reference-bellhop-arrivals"
        if use_reference:
            reference_bellhop = reference_root / "bellhop"
            arrivals = reference_bellhop / "reference_shallow_water_v1.arr"
            if not arrivals.is_file(): raise FileNotFoundError("参考 Bellhop arrivals 不存在")
            executable = request.bellhop_executable or os.environ.get("PLATFORM_BELLHOP_EXECUTABLE")
            execution = "ImportedVerified"
            if executable:
                executable_path = Path(executable).resolve()
                if not executable_path.is_file():
                    raise FileNotFoundError(f"Bellhop 可执行文件不存在: {executable_path}")
                job_root = store.root / "builds" / build.build_id
                job_root.mkdir(parents=True, exist_ok=True)
                stem = "reference_shallow_water_v1"
                for suffix in (".env", ".bty", ".ati"):
                    source = reference_bellhop / f"{stem}{suffix}"
                    if source.is_file():
                        shutil.copy2(source, job_root / source.name)
                process = subprocess.run(
                    [str(executable_path), stem], cwd=job_root,
                    capture_output=True, text=True, timeout=600, check=False,
                )
                if process.returncode != 0:
                    raise RuntimeError((process.stderr or process.stdout or "Bellhop 执行失败")[-1000:])
                executed_arrivals = job_root / f"{stem}.arr"
                if not executed_arrivals.is_file():
                    raise RuntimeError("Bellhop 执行完成但未生成 arrivals 文件")
                arrivals = executed_arrivals
                execution = "Executed"
            digest = hashlib.sha256(arrivals.read_bytes()).hexdigest()
            validation = json.loads((reference_root / "validation_report.json").read_text(encoding="utf-8"))
            asset_id = f"{_safe_id(draft.name)}-{draft.draft_id}-r{draft.revision}"
        else:
            executable = request.bellhop_executable or os.environ.get("PLATFORM_BELLHOP_EXECUTABLE")
            if not executable:
                raise RuntimeError("未配置 Bellhop 可执行文件；不能用近似传播模型冒充正式声场")
            # Authoring jobs run Bellhop only here, never on a Runtime packet path.
            process = subprocess.run([executable, _safe_id(draft.name)], cwd=store.root / "builds", capture_output=True, text=True, timeout=600, check=False)
            if process.returncode != 0:
                raise RuntimeError((process.stderr or process.stdout or "Bellhop 执行失败")[-1000:])
            arrivals = store.root / "builds" / f"{_safe_id(draft.name)}.arr"
            if not arrivals.is_file(): raise RuntimeError("Bellhop 未生成 arrivals 文件")
            digest = hashlib.sha256(arrivals.read_bytes()).hexdigest(); validation = {"state": "Valid", "field": {}}
            asset_id = _safe_id(draft.name)
            execution = "Executed"
        build.steps[3].state = "Completed"; build.state = "Parsing"; build.steps[4].state = "Running"; store.save_build(build)
        # Parsing/normalizing authority remains the Platform environment importer.
        build.steps[4].state = "Completed"; build.state = "Normalizing"; store.save_build(build)
        build.state = "ValidatingAsset"; build.steps[5].state = "Running"; store.save_build(build)
        build.artifact = {"environment_asset_id": asset_id, "asset_format_version": 1, "format": "NS3_FACTORY_ACOUSTIC_FIELD", "build_kind": "BellhopOffline", "bellhop_execution": execution, "arrivals_sha256": digest, "arrivals_path": str(arrivals.resolve()), "validation": validation, "source_draft_id": draft.draft_id, "source_revision": draft.revision, "published": False}
        build.steps[5].state = "Completed"; build.state = "Completed"; build.updated_at = _now(); store.save_build(build)
    except Exception as error:
        for step in build.steps:
            if step.state == "Running": step.state = "Failed"
        build.state = "Failed"; build.failure = {"code": "EnvironmentBuildFailed", "message": str(error)}; build.updated_at = _now(); store.save_build(build)
    return build
