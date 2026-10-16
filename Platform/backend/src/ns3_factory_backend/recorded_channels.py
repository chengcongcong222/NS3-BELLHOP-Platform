"""Immutable recording catalog and run preflight, never a signal processor."""
from __future__ import annotations
import hashlib
import json
import math
from pathlib import Path
import re
from .catalog import CatalogError


def _fail(message):
    raise CatalogError("RecordedChannelInvalid", message)


def _directory(root, asset_id, version):
    if not isinstance(asset_id, str) or not re.fullmatch(r"[a-zA-Z0-9][a-zA-Z0-9_.-]{0,79}", asset_id) or type(version) is not int or version != 1:
        _fail("录音资源标识或版本无效。")
    path = root / asset_id / "1"
    if path.is_symlink() or path.parent.is_symlink():
        _fail("录音资源不能是外部目录链接。")
    return path


def inspect(root: Path, asset_id: str, version: int = 1, *, verify=False):
    path = _directory(root, asset_id, version)
    try:
        if (path / "manifest.json").is_symlink(): raise ValueError("linked manifest")
        encoded = (path / "manifest.json").read_bytes()
        if len(encoded) > 65536: raise ValueError("manifest too large")
        m = json.loads(encoded)
        if (m["schema"] != "ns3-factory-recorded-channel-v1" or m["asset_id"] != asset_id or
                m["version"] != version or m["gain_reference"] != "Normalized" or
                m["geometry"] != "Unspecified" or m["reference_revision"] != "dbf373ca7cd07ac4eb1b24a191d76bea18f73560"):
            raise ValueError("identity mismatch")
        for key in ("fc", "fs_delay", "fs_time", "usable_bandwidth_hz", "duration_s"):
            if type(m[key]) not in (float, int) or not math.isfinite(m[key]) or m[key] <= 0:
                raise ValueError("invalid scientific metadata")
        for key in ("snapshots", "delay_taps", "phase_count", "element", "resample_up", "resample_down"):
            if type(m[key]) is not int or m[key] < 0: raise ValueError("invalid tensor index/count")
        if (m["snapshots"] < 2 or not m["delay_taps"] or m["snapshots"] * m["delay_taps"] > 20_000_000 or
                not 1 <= m["resample_up"] <= 100_000 or not 1 <= m["resample_down"] <= 100_000 or
                m["tracking"] not in {"None", "phi_hat", "theta_hat"} or
                (m["tracking"] == "None") != (m["phase_count"] == 0) or
                m["axis_kind"] not in {"ReceiverElement", "IndependentTrial"} or not 0 <= m["element"] < m["original_axis_count"]):
            raise ValueError("invalid recording dimensions or tracking")
        duration = (m["snapshots"]-1)/m["fs_time"]
        if m["phase_count"]: duration = min(duration, m["phase_count"]/m["fs_delay"])
        if not math.isclose(duration, m["duration_s"], rel_tol=1e-12, abs_tol=1e-12): raise ValueError("invalid recording coverage")
        if set(m["files"]) != {"channel.f64le", "phase.f64le"}: raise ValueError("unexpected binary paths")
        if (m["files"]["channel.f64le"]["bytes"] != m["snapshots"]*m["delay_taps"]*16 or
                m["files"]["phase.f64le"]["bytes"] != m["phase_count"]*8): raise ValueError("tensor size mismatch")
        for name, info in m["files"].items():
            file = path / name
            if file.is_symlink() or file.stat().st_size != info["bytes"]: raise ValueError("size mismatch")
            if verify:
                with file.open("rb") as stream:
                    if hashlib.file_digest(stream, "sha256").hexdigest() != info["sha256"]:
                        raise ValueError("checksum mismatch")
        return {"manifest": m, "manifest_sha256": hashlib.sha256(encoded).hexdigest()}
    except (OSError, ValueError, KeyError, TypeError) as error:
        raise CatalogError("RecordedChannelUnavailable", "录音资源缺失、损坏或版本不一致；未使用替代信道。") from error


def list_recordings(root: Path):
    if not root.exists(): return []
    result = []
    for path in sorted(root.glob("*/1/manifest.json")):
        result.append(inspect(root, path.parent.parent.name))
    return result


def inspect_noise(root: Path, asset_id: str, version: int = 1, *, verify=False):
    path = _directory(root, asset_id, version)
    try:
        if (path / "manifest.json").is_symlink(): raise ValueError("linked manifest")
        encoded = (path / "manifest.json").read_bytes()
        if len(encoded) > 65536: raise ValueError("manifest too large")
        m = json.loads(encoded)
        if (m["schema"] != "recorded-continuous-noise-v1" or m["asset_id"] != asset_id or m["version"] != version or
                m["dtype"] != "little-endian-float64" or m["layout"] != "time,selected_element" or
                m["reference_noisegen_sha256"] != "11f09a3b51ee10abd0fef8f4276e0b6a246cd9de705fd3e249212e0cb3966550" or
                type(m["shape"][0]) is not int or not 1 <= m["shape"][0] <= 10_000_000 or m["shape"][1] != 1 or
                not 0 < m["alpha"] <= 2 or type(m["sample_rate_hz"]) is not int or m["sample_rate_hz"] <= 0):
            raise ValueError("invalid continuous noise metadata")
        binary = path / "noise.f64le"
        if binary.is_symlink() or binary.stat().st_size != m["bytes"] or m["bytes"] != m["shape"][0] * 8:
            raise ValueError("noise size mismatch")
        if verify:
            with binary.open("rb") as stream:
                if hashlib.file_digest(stream, "sha256").hexdigest() != m["samples_sha256"]:
                    raise ValueError("noise checksum mismatch")
        return {"manifest": m, "manifest_sha256": hashlib.sha256(encoded).hexdigest()}
    except (OSError, ValueError, KeyError, TypeError, IndexError) as error:
        raise CatalogError("RecordedNoiseUnavailable", "连续噪声资源缺失、损坏或格式不符；未改用白噪声。") from error


def list_noise(root: Path):
    return [inspect_noise(root, p.parent.parent.name) for p in sorted(root.glob("*/1/manifest.json"))] if root.exists() else []


def settings_issues(payload):
    c = payload.get("recorded_channel")
    if payload.get("physical_layer") != "Channel3Recorded":
        return ["只有模式 C 可以携带录音信道设置"] if c is not None else []
    issues = []
    if (payload.get("execution_mode") != "PlanningV1" or payload.get("workload") != "CommunicationOnly" or
            payload.get("routing_mode") != "Direct" or payload.get("mac_mode") not in {"AdaptiveTdma", "Tdma"}):
        issues.append("模式 C 当前支持定向通信实验与 TDMA；不伪造测量几何、反向信道或融合观测。")
    if payload.get("waveform_noise") != "None" or payload.get("source_level_db", payload.get("source_level_db_re_1upa_at_1m")) != 0:
        issues.append("模式 C 使用相对幅度；不能叠加模式 B 的绝对声源级或噪声设置。")
    fields = {"asset_id", "version", "start_time_s", "modeled_delay_s", "relative_amplitude", "noise"}
    if not isinstance(c, dict) or set(c) != fields:
        return issues + ["请完整选择录音、起始时刻、建模传播时延及相对幅度。"]
    noise = c["noise"]
    noise_valid = noise == {"mode": "None"} or (isinstance(noise, dict) and
        set(noise) == {"mode", "asset_id", "version"} and noise["mode"] == "ContinuousLibrary" and
        isinstance(noise["asset_id"], str) and type(noise["version"]) is int and noise["version"] == 1)
    if type(c["version"]) is not int or c["version"] != 1 or not noise_valid:
        issues.append("录音版本或连续噪声配置尚未支持。")
    for key in ("start_time_s", "modeled_delay_s", "relative_amplitude"):
        value = c[key]
        if type(value) not in (int, float) or not math.isfinite(value) or value < 0 or (key == "relative_amplitude" and value == 0):
            issues.append("录音时刻、建模时延和相对幅度必须为有效非负数；幅度必须大于零。")
    return issues


def freeze(root: Path, payload, scenario, nodes, assets):
    issues = settings_issues(payload)
    if issues: _fail("；".join(issues))
    c = payload["recorded_channel"]
    inspected = inspect(root, c["asset_id"], c["version"], verify=True)
    m = inspected["manifest"]
    if (scenario.get("environment_source") != "Recorded" or scenario.get("environment_asset_id") != c["asset_id"] or
            int(scenario.get("environment_version", 0)) != c["version"] or scenario.get("spatial_domain") is not None):
        _fail("场景必须明确绑定同一录音版本；录音示意布局不能作为 Bellhop 海域。")
    tx = [n for n in nodes if n["can_transmit"]]
    rx = [n for n in nodes if n["can_receive"]]
    if len(nodes) != 2 or len(tx) != 1 or len(rx) != 1 or tx[0]["node_id"] == rx[0]["node_id"]:
        _fail("此版本录音是单向链路：请配置一个只发节点和一个只收节点，不将阵元当作独立节点。")
    for asset in assets.values():
        for track in asset.tracks:
            first = track.points[0]
            if any((p.x_m, p.y_m, p.z_m) != (first.x_m, first.y_m, first.z_m) for p in track.points):
                _fail("录音已包含实测运动影响；不能叠加自由轨迹或再次加入多普勒。画布仅作示意。")
    if payload["center_frequency_hz"] != m["fc"] or payload["occupied_bandwidth_hz"] > m["usable_bandwidth_hz"]:
        _fail("工作频率或带宽超出录音支持范围。")
    rate = int(payload["bit_rate_bits_per_second"])
    fs = rate / (2 if payload.get("osdm", {}).get("constellation") == "Qpsk" else 1) if payload.get("waveform_modulation") == "Osdm" else rate*100
    if fs != int(fs) or not 1 <= fs <= 1_000_000:
        _fail("当前录音桥接需要整数基带采样率，且不得超过 1 MHz 服务计算预算。")
    start = math.floor(c["start_time_s"] * m["fs_delay"])
    duration = int(payload["planning_cycle_duration_ns"]) * int(payload["simulation_cycle_count"]) / 1e9
    if c["start_time_s"] + duration > m["duration_s"]:
        _fail(f"录音可用时长 {m['duration_s']:.3f} 秒，当前起点加运行时长超过覆盖范围。")
    delay = round(c["modeled_delay_s"] * 1e9)
    if not 0 <= delay <= 2**63-1 or not 0 <= start <= 2**64-1:
        _fail("录音时刻或建模时延超出整数范围。")
    noise = c["noise"]
    if noise["mode"] == "ContinuousLibrary":
        loaded = inspect_noise(root.parent / "recorded-noise", noise["asset_id"], noise["version"], verify=True)
        nm = loaded["manifest"]
        if (nm["sample_rate_hz"] != math.ceil(4 * m["fc"]) or nm["center_frequency_hz"] != m["fc"] or
                nm["bandwidth_hz"] < payload["occupied_bandwidth_hz"] or
                nm["shape"][0] / nm["sample_rate_hz"] < duration or
                m["axis_kind"] != "ReceiverElement" or nm["elements"] != [m["element"]]):
            _fail("连续噪声的频率、带宽、阵元或时长不匹配；请导入与所选录音相符的噪声实现。")
        noise = {"mode": "ContinuousLibrary", **loaded}
    return {**inspected, "transmitter_node_id": tx[0]["node_id"], "receiver_node_id": rx[0]["node_id"],
            "start_sample": str(start), "modeled_reference_delay_ns": str(delay),
            "relative_amplitude": c["relative_amplitude"], "noise": noise}
