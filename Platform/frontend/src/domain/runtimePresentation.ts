import type { ResultDto, RunEventDto } from "../api/types";
import { physicalMode, waveformSignalToNoise, type PhysicalMode } from "./resultAnalysis";

export const defaultEventKinds = ["SignalArrival", "Reception", "Fusion"];
export const eventLabels: Record<string, string> = { Transmission: "开始发送", ChannelOutcome: "信道计算", SignalArrival: "实际到达", Reception: "接收完成", Fusion: "融合结果", CycleCommit: "周期完成" };

/** Pure display selection. The caller retains the original journal for state/statistics. */
export function displayEvents(events: readonly RunEventDto[], kinds: ReadonlySet<string>, overheard = false) {
  const targets = new Map<string, string | null>();
  for (const { trace } of events) if (trace.kind === "Transmission") targets.set(trace.payload.transmission_id, trace.payload.target.type === "Unicast" ? trace.payload.target.node_id : null);
  return events.filter(({ trace }) => {
    if (!kinds.has(trace.kind)) return false;
    if (overheard || !["ChannelOutcome", "SignalArrival", "Reception"].includes(trace.kind)) return true;
    if (!("receiver_node_id" in trace.payload) || !("transmission_id" in trace.payload)) return true;
    const target = targets.get(trace.payload.transmission_id);
    // Missing transmission metadata is not evidence of an intended receiver.
    return target === null || target === trace.payload.receiver_node_id;
  });
}

export function compactNumber(value: number, digits = 3) {
  if (!Number.isFinite(value)) return "—";
  if (value !== 0 && (Math.abs(value) < .001 || Math.abs(value) >= 1e6)) return value.toExponential(2);
  return Number(value.toFixed(digits)).toString();
}
export const secondsLabel = (ns: string) => `${compactNumber(Number(BigInt(ns)) / 1e9)} s`;

// Use captured configuration/provenance, never assume that missing data means A.
export function runPhysicalMode(result: ResultDto | undefined, implementation: string | undefined, events: readonly RunEventDto[]): PhysicalMode {
  if (result) {
    const mode = physicalMode(result, implementation);
    if (mode !== "unknown") return mode;
  }
  if (implementation === "Channel2Waveform" || implementation === "Channel2") return "B";
  if (implementation === "Channel1Abstract" || implementation === "Channel1") return "A";
  return events.some(e => e.trace.kind === "Reception" && e.trace.payload.waveform) ? "B" : "unknown";
}

export function qualityAt(events: readonly RunEventDto[], waveformMode: boolean | "unknown") {
  let bits = 0n, errors = 0n, crcPass = 0, crcFail = 0;
  let receptions = 0, evaluated = 0;
  let snr = "—", ber = "—";
  for (const { trace } of events) if (trace.kind === "Reception") {
    receptions++;
    // Latest-link SNR must not silently retain an earlier link's value.
    snr = "—";
    const w = trace.payload.waveform, q = trace.payload.quality;
    if (waveformMode === true && w) {
      evaluated++;
      bits += BigInt(w.payload_bit_count); errors += BigInt(w.bit_error_count);
      if (w.crc_ok) crcPass++; else crcFail++;
      const receivedSnr = waveformSignalToNoise(w);
      snr = receivedSnr === "no-noise" ? "未加噪声" : receivedSnr === "no-signal" ? "无信号功率" : receivedSnr === null ? "—" : `${compactNumber(receivedSnr, 1)} dB`;
    } else if (waveformMode === false) {
      ber = "—";
      if (!q || q.source !== "Modeled") continue;
      evaluated++;
      snr = `${compactNumber(q.signal_to_noise_ratio_db, 1)} dB`;
      ber = compactNumber(q.bit_error_rate, 4);
    }
  }
  if (waveformMode === true && bits > 0n) ber = compactNumber(Number(errors) / Number(bits), 4);
  return { snr, ber, bits: String(bits), errors: String(errors), crcPass, crcFail,
    receptions, evaluated, missing: receptions - evaluated,
    basis: waveformMode === "unknown" ? "历史记录未标明物理模式，不推测误码率" : waveformMode ? "已接收帧累计实际误码率" : "最近接收链路 · 理论 BPSK/AWGN" };
}

/** Read-only join of one physical transmission, not a guessed MAC/cooperation group. */
export function communicationProgress(events: readonly RunEventDto[], selected: RunEventDto | undefined, shown: readonly RunEventDto[]) {
  if (!selected || !shown.some(e => e.sequence === selected.sequence) || !("transmission_id" in selected.trace.payload)) return null;
  const id = selected.trace.payload.transmission_id;
  const tx = events.find(e => e.trace.kind === "Transmission" && e.trace.payload.transmission_id === id);
  if (!tx || tx.trace.kind !== "Transmission") return null;
  const receivers = new Map<string, { node: string; arrivedAt?: string; completedAt?: string; outcome?: string }>();
  const labels = { LocalDelivery: "数据已交付", RelayEnqueue: "等待转发", Overheard: "旁听 · 不交付", NotDecoded: "解码失败 · 不交付" };
  for (const { trace } of shown) {
    if ((trace.kind !== "SignalArrival" && trace.kind !== "Reception") || trace.payload.transmission_id !== id) continue;
    const node = trace.payload.receiver_node_id;
    const state = receivers.get(node) ?? { node };
    if (trace.kind === "SignalArrival") state.arrivedAt = trace.occurred_at_ns;
    else { state.completedAt = trace.occurred_at_ns; state.outcome = labels[trace.payload.disposition]; }
    receivers.set(node, state);
  }
  return { sender: tx.trace.payload.sender_node_id, target: tx.trace.payload.target, receivers: [...receivers.values()] };
}

export function communicationLinks(events: readonly RunEventDto[], selected: RunEventDto | undefined, shown: readonly RunEventDto[]) {
  if (!selected || !shown.some(e => e.sequence === selected.sequence)) return [];
  const txs = new Map(events.flatMap(e => e.trace.kind === "Transmission" ? [[e.trace.payload.transmission_id, e.trace.payload] as const] : []));
  // Only already-observed events at this exact simulation timestamp are grouped.
  return shown.filter(e => e.trace.occurred_at_ns === selected.trace.occurred_at_ns && e.trace.kind === selected.trace.kind).flatMap<{ sender: string; receiver: string; phase: string; failed: boolean }>(({ trace }) => {
    if (trace.kind === "Transmission") {
      return trace.payload.target.type === "Unicast" ? [{ sender: trace.payload.sender_node_id, receiver: trace.payload.target.node_id, phase: trace.kind, failed: false }] : [];
    }
    if (trace.kind !== "SignalArrival" && trace.kind !== "Reception") return [];
    const tx = txs.get(trace.payload.transmission_id);
    return tx ? [{ sender: tx.sender_node_id, receiver: trace.payload.receiver_node_id, phase: trace.kind, failed: trace.kind === "Reception" && trace.payload.disposition === "NotDecoded" }] : [];
  });
}
