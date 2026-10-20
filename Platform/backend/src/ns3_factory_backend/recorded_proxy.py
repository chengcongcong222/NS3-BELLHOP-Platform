"""V5 proxy binding validation, immutable execution inputs and resource checks."""
from __future__ import annotations

from fractions import Fraction
from pathlib import Path
import re
import math
import hashlib
import json
from typing import Annotated, Literal

from pydantic import BaseModel, ConfigDict, Field

from .catalog import CatalogError
from .recorded_channels import inspect, inspect_noise

Decimal = Annotated[str, Field(pattern=r"^(0|[1-9][0-9]{0,19})$")]
# Deployment safety policy, NOT a UWA/Bellhop or scientific node-count limit.
# Covers retained tensors/noise and conservative binary-loader temporaries;
# waveform-window limits remain separately enforced by the PHY.
RESOURCE_LOAD_BUDGET_BYTES = 256 * 1024 * 1024


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


class TransmitterModulation(StrictModel):
    node_id: Decimal
    modulation: Literal["Bpsk", "Bfsk", "Osdm"]


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
    transmitter_modulations: Annotated[list[TransmitterModulation], Field(max_length=256)] = Field(default_factory=list)


def _fail(message):
    raise CatalogError("RecordedProxyInvalid", message)


def settings_issues(payload):
    from pydantic import ValidationError
    issues = []
    if ("recorded_channel" in payload or payload.get("physical_layer") != "Channel3Recorded" or
        payload.get("execution_mode") != "PlanningV1" or payload.get("workload") != "CommunicationOnly" or
        payload.get("routing_mode") != "Direct" or payload.get("mac_mode") not in {"Tdma", "AdaptiveTdma"}):
        issues.append("代理网络需要模式 C、定向通信与已支持的 TDMA；不能同时携带旧单链路配置。")
    if payload.get("waveform_noise") != "None" or payload.get("source_level_db") != 0:
        issues.append("代理网络使用显式相对幅度和每个接收端的连续噪声，不使用绝对声源级。")
    try:
        ProxyBindings.model_validate(payload["recorded_proxy"])
    except (ValidationError, KeyError, TypeError):
        issues.append("代理网络链路、采样时钟或接收噪声配置不完整。")
    return issues


def freeze(root: Path, payload, scenario, nodes, assets):
    issues = settings_issues(payload)
    if issues:
        _fail("；".join(issues))
    if scenario.get("environment_source") != "Recorded" or scenario.get("spatial_domain") is not None:
        _fail("代理网络只接受实录资源和逻辑布局，不能附加几何声场边界。")
    spec = ProxyBindings.model_validate(payload["recorded_proxy"])
    expected_nodes = sorted(
        [{"node_id": str(n["node_id"]), "can_transmit": n["can_transmit"], "can_receive": n["can_receive"]}
         for n in nodes], key=lambda n: int(n["node_id"]))
    if sorted([n.model_dump() for n in spec.nodes], key=lambda n: int(n["node_id"])) != expected_nodes:
        _fail("链路配置与场景节点能力不一致，请重新检查链路。")
    if any(n["initial_position"] != {"x_meters":0, "y_meters":0, "z_meters":-1} for n in nodes):
        _fail("代理节点使用固定适配点；画布布局须单独保存，不能写入物理位置。")
    rate = int(payload["bit_rate_bits_per_second"])
    fs = (rate / (2 if payload["osdm"]["constellation"] == "Qpsk" else 1)
          if payload.get("waveform_modulation") == "Osdm" else rate*100)
    if spec.transmitter_modulations and payload.get("waveform_modulation") != "Osdm":
        _fail("逐节点混合调制采用公共 OSDM 时钟预设，请先选择全局 OSDM。")
    duration = int(payload["simulation_cycle_count"])*int(payload["planning_cycle_duration_ns"])
    if (spec.baseband_rate_hz != fs or spec.carrier_hz != payload["center_frequency_hz"] or
        spec.occupied_bandwidth_hz != payload["occupied_bandwidth_hz"] or int(spec.duration_ns) != duration):
        _fail("代理信道时钟、带宽或覆盖时长与实际实验参数不一致。")
    for asset in assets.values():
        for track in asset.tracks:
            first = track.points[0]
            if any((p.x_m, p.y_m, p.z_m) != (first.x_m, first.y_m, first.z_m) for p in track.points):
                _fail("代理网络不能附加运动多普勒；节点位置仅作逻辑布局。")
    result = resolve(root, spec)
    # Preview release-readiness is not a scientific input or an execution flag.
    result.pop("execution_ready")
    result.pop("execution_blocker")
    quantum = 1
    for link in result["resolved_links"]:
        for rate in (Fraction(4*spec.carrier_hz), Fraction(str(link["manifest"]["fs_delay"]))):
            quantum = math.lcm(quantum, 1_000_000_000*rate.denominator //
                               math.gcd(rate.numerator, 1_000_000_000*rate.denominator))
    if (int(payload["planning_cycle_duration_ns"]) % quantum or
        (payload["mac_mode"] == "Tdma" and int(payload["slot_duration_ns"]) % quantum)):
        _fail(f"周期或配置时隙必须与录音时钟对齐（{quantum} ns 的整数倍）；不会自动移动发送事件。")
    result["receiver_clock"]["emission_quantum_ns"] = str(quantum)
    # Identity of the anchor resource is retained for old resource-chain consumers;
    # it never replaces the complete directed mapping used by the receiver.
    if scenario["environment_asset_id"] not in {l["manifest"]["asset_id"] for l in result["resolved_links"]}:
        _fail("场景主资源须属于已解析的信道集合。")
    return result


def physics_digest(definition, execution):
    """Scientific identity excludes resource-label/revision and UI-only layout."""
    value = {
        "schema": "ns3-factory-proxy-physics-v1",
        "nodes": sorted(definition["nodes"],key=lambda n:int(n["node_id"])),
        "recorded_proxy": definition["recorded_proxy"],
        "experiment": {k:v for k,v in definition["experiment"].items() if k != "name"},
        "execution": execution.model_dump(mode="json"),
    }
    return hashlib.sha256(json.dumps(value,ensure_ascii=False,sort_keys=True,
                                    separators=(",",":"),allow_nan=False).encode()).hexdigest()


async def reception_evidence(catalog, run_id):
    """Compact terminal observation projection, never used by the receiver."""
    transmissions, receptions = {}, []
    async for event in catalog.stream_after(run_id, 0):
        trace = event.trace
        if trace.kind == "Transmission":
            transmissions[trace.payload.transmission_id] = trace.payload.sender_node_id
        elif trace.kind == "Reception":
            p = trace.payload
            receptions.append({
                "transmission_id": p.transmission_id,
                "receiver_node_id": p.receiver_node_id,
                "at_ns": trace.occurred_at_ns, "disposition": p.disposition,
                "mixture": p.mixture.model_dump(mode="json") if p.mixture else None,
                "waveform": p.waveform.model_dump(mode="json") if p.waveform else None,
            })
    for item in receptions:
        # Missing evidence is explicit, not guessed from business destinations.
        item["sender_node_id"] = transmissions.get(item["transmission_id"])
    return receptions


def verify_frozen_resources(root: Path, frozen):
    checked = {}
    for entry in frozen["resolved_links"]:
        m = entry["manifest"]
        key = (m["asset_id"], m["version"])
        if key not in checked:
            checked[key] = inspect(root/"recorded-channels", *key, verify=True)
        if checked[key] != {k: entry[k] for k in ("manifest", "manifest_sha256")}:
            _fail("原信道资源发生变化，未替换录音或回退声场。")
    checked = {}
    for entry in frozen["rx_noise"]:
        if entry["mode"] == "None":
            continue
        m = entry["manifest"]
        key = (m["asset_id"], m["version"])
        if key not in checked:
            checked[key] = inspect_noise(root/"recorded-noise", *key, verify=True)
        if checked[key] != {k: entry[k] for k in ("manifest", "manifest_sha256")}:
            _fail("原接收噪声资源发生变化，未重新生成替代样本。")


def resolve(root: Path, spec: ProxyBindings, *, resource_budget_bytes=RESOURCE_LOAD_BUDGET_BYTES):
    """Canonical physical fan-out, independent of flows and browser coordinates.

    Limits above bound request parsing only, not a verified runnable network size.
    The caller must not label this result runtime-ready.
    """
    nodes = sorted(spec.nodes, key=lambda n: int(n.node_id))
    ids = [n.node_id for n in nodes]
    if len(set(ids)) != len(ids) or any(int(n) > 2**64-1 for n in ids):
        _fail("节点编号重复或超出范围。")
    modem_ids = set()
    for modem in spec.transmitter_modulations:
        if modem.node_id in modem_ids or not any(n.node_id == modem.node_id and n.can_transmit for n in nodes):
            _fail("逐节点调制只能配置实际发送节点，且不能重复。")
        modem_ids.add(modem.node_id)
        if modem.modulation != "Osdm" and spec.baseband_rate_hz % 100:
            _fail("混合 BPSK/BFSK 的公共采样率须是 100 的整数倍；不会自动改变采样时钟。")
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
        scaled_rate = manifest["fs_delay"]*1_000_000
        if scaled_rate != int(scaled_rate) or manifest["fs_delay"] > 1_000_000_000:
            _fail("录音采样率无法精确表示为受支持的有理时钟；不自动取整。")
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
    unique = {l["manifest_sha256"]: l["manifest"] for l in links}
    channel_bytes = [sum(int(f["bytes"]) for f in m["files"].values()) for m in unique.values()]
    noise_bytes = [int(n["manifest"]["bytes"]) for n in noise_by_rx.values() if n["mode"] != "None"]
    estimated = sum(channel_bytes) + sum(noise_bytes) + 2*max(channel_bytes, default=0) + max(noise_bytes, default=0)
    if estimated > resource_budget_bytes:
        _fail(f"信道与噪声载入估算 {estimated/1024**2:.1f} MiB，超过当前部署的 "
              f"{resource_budget_bytes/1024**2:.1f} MiB 资源载入预算。请减少不同信道资产或导入较短的录音资产；仅修改回放起点不会缩小载入量。这不是物理模型上限。")
    return {
        "schema": "ns3-factory-resolved-proxy-bindings-v1",
        "execution_ready": False,
        "execution_blocker": "Binding preview only; save and start to validate the complete experiment, timing and resource coverage",
        "nodes": [n.model_dump() for n in nodes], "resolved_links": links,
        "transmitter_modulations": [m.model_dump() for m in sorted(spec.transmitter_modulations,key=lambda m:int(m.node_id))],
        "rx_noise": [noise_by_rx[n] for n in sorted(receivers, key=int)],
        "receiver_clock": {"passband_rate_hz": passband_rate, "baseband_rate_hz": spec.baseband_rate_hz,
                           "carrier_hz": spec.carrier_hz, "oscillator": "PacketResetTxDesiredArrivalRx"},
        "assumptions": {"real_joint_multitransmitter_measured": False, "geometry_drives_channel": False,
                        "link_temporal_relation": "SyntheticOffsets",
                        "noise_spatial_relation": "SyntheticAssignment"},
    }
