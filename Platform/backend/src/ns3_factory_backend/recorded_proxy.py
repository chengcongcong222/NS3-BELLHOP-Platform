"""Read-only v5 binding preflight. Not an execution or publication endpoint."""
from __future__ import annotations

from fractions import Fraction
from pathlib import Path
import re
from typing import Annotated, Literal

from pydantic import BaseModel, ConfigDict, Field

from .catalog import CatalogError
from .recorded_channels import inspect, inspect_noise

Decimal = Annotated[str, Field(pattern=r"^(0|[1-9][0-9]{0,19})$")]


class StrictModel(BaseModel):
    model_config = ConfigDict(extra="forbid", strict=True, allow_inf_nan=False)


class Node(StrictModel):
    node_id: Decimal
    can_transmit: bool
    can_receive: bool


class Sample(StrictModel):
    asset_id: str
    version: Literal[1]
    start_sample: Decimal
    modeled_reference_delay_ns: Decimal
    relative_amplitude: Annotated[float, Field(gt=0)]


class Override(StrictModel):
    tx_id: Decimal
    rx_id: Decimal
    sample: Sample


class ReceiverNoise(StrictModel):
    rx_id: Decimal
    mode: Literal["None", "ContinuousLibrary"]
    asset_id: str | None = None
    version: Literal[1] | None = None


class ProxyBindings(StrictModel):
    schema_id: Literal["ns3-factory-recorded-proxy-bindings-v1"] = Field(alias="schema")
    nodes: Annotated[list[Node], Field(min_length=2, max_length=256)]
    allocation: Literal["Explicit", "GlobalSame", "GlobalPool"]
    samples: Annotated[list[Sample], Field(max_length=256)]
    overrides: Annotated[list[Override], Field(max_length=65536)]
    receiver_noise: Annotated[list[ReceiverNoise], Field(max_length=256)]
    carrier_hz: Annotated[int, Field(gt=0, le=250000000)]
    baseband_rate_hz: Annotated[int, Field(gt=0, le=1000000)]
    occupied_bandwidth_hz: Annotated[float, Field(gt=0)]
    duration_ns: Decimal


def _fail(message):
    raise CatalogError("RecordedProxyInvalid", message)


def resolve(root: Path, spec: ProxyBindings):
    """Canonical physical fan-out, independent of flows and browser coordinates.

    Limits above bound request parsing only, not a verified runnable network size.
    The caller must not label this result runtime-ready.
    """
    nodes = sorted(spec.nodes, key=lambda n: int(n.node_id))
    ids = [n.node_id for n in nodes]
    if len(set(ids)) != len(ids) or any(int(n) > 2**64-1 for n in ids):
        _fail("节点编号重复或超出范围。")
    duration = int(spec.duration_ns)
    if not 0 < duration <= 2**63-1:
        _fail("运行时长须为有效的正整数纳秒。")
    if spec.occupied_bandwidth_hz >= 2*spec.carrier_hz:
        _fail("占用带宽与载频不兼容。")
    if ((spec.allocation == "Explicit" and spec.samples) or
        (spec.allocation == "GlobalSame" and len(spec.samples) != 1) or
        (spec.allocation == "GlobalPool" and not spec.samples)):
        _fail("全局信道分配方式与样本数量不一致。")
    pairs = [(a.node_id, b.node_id) for a in nodes for b in nodes
             if a.can_transmit and b.can_receive and a.node_id != b.node_id]
    if not pairs:
        _fail("没有具备发送与接收能力的有向链路。")
    overrides = {}
    for override in spec.overrides:
        key = (override.tx_id, override.rx_id)
        if key not in pairs or key in overrides:
            _fail(f"无效或重复的链路覆盖：N{key[0]} → N{key[1]}。")
        overrides[key] = override.sample
    passband_rate = 4*spec.carrier_hz
    resources = {}
    links = []
    for index, (tx, rx) in enumerate(pairs):
        sample = overrides.get((tx, rx))
        origin = "EdgeOverride"
        if sample is None:
            if spec.allocation == "Explicit":
                _fail(f"缺少物理接收链路 N{tx} → N{rx}，包含旁听链路也须绑定；未推断反向信道。")
            sample = spec.samples[index % len(spec.samples)]
            origin = spec.allocation
        key = (sample.asset_id, sample.version)
        if key not in resources:
            resources[key] = inspect(root, *key, verify=True)
        resource = resources[key]
        manifest = resource["manifest"]
        if not re.fullmatch(r"[0-9a-f]{64}", manifest.get("source_sha256", "")):
            _fail("录音缺少有效原始文件摘要，不能作为代理链路依据。")
        if manifest["fs_delay"] != int(manifest["fs_delay"]) or manifest["fs_delay"] > 1_000_000_000:
            _fail("代理接收当前要求整数录音采样率；尚未支持该采样时钟转换。")
        if manifest["fc"] != spec.carrier_hz or spec.occupied_bandwidth_hz > manifest["usable_bandwidth_hz"]:
            _fail(f"N{tx} → N{rx} 的载频或带宽与录音不兼容。")
        delay, start = int(sample.modeled_reference_delay_ns), int(sample.start_sample)
        if not 0 < delay <= 2**63-1 or start > 2**64-1:
            _fail("当前事件规则要求正建模时延，且录音起点必须在整数范围内。")
        if delay*passband_rate % 1_000_000_000:
            _fail("建模时延不在接收通带采样网格上；不自动取整。")
        coverage = Fraction(str(manifest["duration_s"]))
        end = (Fraction(start + manifest["delay_taps"], 1)/Fraction(str(manifest["fs_delay"]))
               + Fraction(duration, 1_000_000_000))
        if end > coverage:
            _fail(f"N{tx} → N{rx} 的录音未覆盖运行窗口及信道尾部。")
        links.append({"tx_id": tx, "rx_id": rx, **sample.model_dump(),
                      **resource, "origin": origin,
                      "evidence": "ProxySharedCIR" if spec.allocation == "GlobalSame" and origin != "EdgeOverride"
                      else "ProxySampledLink"})
    noise_by_rx = {}
    receivers = {rx for _, rx in pairs}
    for item in spec.receiver_noise:
        if item.rx_id not in receivers or item.rx_id in noise_by_rx:
            _fail("噪声必须按实际接收节点唯一配置，不能按链路重复加入。")
        if item.mode == "None":
            if item.asset_id is not None or item.version is not None:
                _fail("无噪声配置不能残留噪声资源。")
            noise_by_rx[item.rx_id] = {"rx_id": item.rx_id, "mode": "None"}
            continue
        if item.asset_id is None or item.version is None:
            _fail("请完整指定接收端连续噪声资源。")
        loaded = inspect_noise(root.parent/"recorded-noise", item.asset_id, item.version, verify=True)
        m = loaded["manifest"]
        max_delay = max(int(l["modeled_reference_delay_ns"]) for l in links if l["rx_id"] == item.rx_id)
        if (m["sample_rate_hz"] != passband_rate or m["center_frequency_hz"] != spec.carrier_hz or
            m["bandwidth_hz"] < spec.occupied_bandwidth_hz or
            m["shape"][0]*1_000_000_000 < (duration + max_delay)*passband_rate):
            _fail(f"N{item.rx_id} 连续噪声频率、带宽或覆盖不足。")
        noise_by_rx[item.rx_id] = {"rx_id": item.rx_id, "mode": item.mode, **loaded}
    if set(noise_by_rx) != receivers:
        _fail("每个实际接收节点都须明确选择连续噪声或无噪声。")
    return {
        "schema": "ns3-factory-resolved-proxy-bindings-v1",
        "execution_ready": False,
        "execution_blocker": "v5 Worker admission and end-to-end verification are not complete",
        "nodes": [n.model_dump() for n in nodes], "resolved_links": links,
        "rx_noise": [noise_by_rx[n] for n in sorted(receivers, key=int)],
        "receiver_clock": {"passband_rate_hz": passband_rate, "baseband_rate_hz": spec.baseband_rate_hz,
                           "carrier_hz": spec.carrier_hz, "oscillator": "PacketResetTxDesiredArrivalRx"},
        "assumptions": {"real_joint_multitransmitter_measured": False, "geometry_drives_channel": False,
                        "link_temporal_relation": "SyntheticOffsets",
                        "noise_spatial_relation": "SyntheticAssignment"},
    }
