"""Validate explicit OSDM framing; this module never executes the modem."""
from __future__ import annotations

import math


def osdm_airtime_ns(payload_bytes: int, bit_rate: int, config: dict) -> int:
    bits_per_symbol = 2 if config["constellation"] == "Qpsk" else 1
    frame_bits = (payload_bytes + 4) * 8
    block_bits = config["block_length"] * bits_per_symbol
    blocks = (frame_bits + block_bits - 1) // block_bits + 1  # training block
    samples = blocks * (config["block_length"] + config["cyclic_prefix"])
    return (samples * bits_per_symbol * 1_000_000_000 + bit_rate - 1) // bit_rate


def validate_osdm(payload: dict) -> list[str]:
    config = payload.get("osdm")
    if payload.get("waveform_modulation") != "Osdm":
        return ["非 OSDM 调制不能携带 OSDM 参数"] if config is not None else []
    if payload.get("physical_layer") not in {"Channel2Waveform", "Channel3Recorded"} or payload.get("execution_mode") != "PlanningV1":
        return ["OSDM 需要波形模式和正式规划实验，不能用于历史预置执行方式"]
    fields = {"block_length", "vector_length", "cyclic_prefix", "constellation",
              "minimum_sync_correlation", "maximum_sync_search"}
    if not isinstance(config, dict) or set(config) != fields:
        return ["请完整设置 OSDM 分块、循环前缀和同步参数"]
    try:
        for key in ("block_length", "vector_length", "cyclic_prefix", "maximum_sync_search"):
            if type(config[key]) is not int or not 1 <= config[key] <= 16384:
                raise ValueError("OSDM 样本计数须为 1～16384 的整数（服务计算预算）")
        k, m, cp = (config[key] for key in ("block_length", "vector_length", "cyclic_prefix"))
        if k & (k - 1) or k % m or (k // m) & (k // m - 1) or cp > k:
            raise ValueError("OSDM 的 K 与 K/M 须为 2 的幂，循环前缀不能超过 K")
        if config["maximum_sync_search"] < cp:
            raise ValueError("OSDM 同步搜索须覆盖循环前缀")
        threshold = config["minimum_sync_correlation"]
        if type(threshold) not in (int, float) or not math.isfinite(threshold) or not 0 < threshold <= 1:
            raise ValueError("OSDM 同步相关门限须在 (0, 1] 内")
        if config["constellation"] not in {"Bpsk", "Qpsk"}:
            raise ValueError("OSDM 星座须为 BPSK 或 QPSK")
        rate = int(payload["bit_rate_bits_per_second"])
        sample_rate = rate / (2 if config["constellation"] == "Qpsk" else 1)
        if sample_rate > float(payload.get("occupied_bandwidth_hz", 4000)):
            raise ValueError("OSDM 复基带采样率不能超过配置带宽；请降低标称比特率或扩大带宽")
    except (ValueError, TypeError, KeyError, OverflowError) as error:
        return [str(error)]
    return []
