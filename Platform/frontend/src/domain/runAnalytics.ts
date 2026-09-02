import type { RunEventDto } from "../api/types";
import { formatNanoseconds } from "./format";

export interface TrendPoint { timeNs: string; value: number }

export function eventTitle(event: RunEventDto): string {
  const trace = event.trace;
  if (trace.kind === "CycleCommit") return `第 ${trace.payload.cycle_id} 个通信周期完成`;
  if (trace.kind === "Transmission") {
    const target = trace.payload.target.type === "Broadcast" ? "广播" : `发往节点 ${trace.payload.target.node_id}`;
    return `节点 ${trace.payload.sender_node_id} 开始发送（${target}）`;
  }
  if (trace.kind === "ChannelOutcome") return trace.payload.outcome.type === "NoArrival"
    ? `节点 ${trace.payload.receiver_node_id} 未获得有效声学到达`
    : `信号到达节点 ${trace.payload.receiver_node_id}`;
  const disposition = { NotDecoded: "未解码", Overheard: "旁听接收", LocalDelivery: "成功送达本节点", RelayEnqueue: "进入中继队列" }[trace.payload.disposition];
  return `节点 ${trace.payload.receiver_node_id}：${disposition}`;
}

export function eventTechnicalDetail(event: RunEventDto): string {
  const trace = event.trace;
  if (trace.kind === "CycleCommit") return `snapshot ${trace.payload.base_snapshot_version} → ${trace.payload.committed_snapshot_version}`;
  if (trace.kind === "Transmission") return `TransmissionId ${trace.payload.transmission_id} · PacketId ${trace.payload.packet_id}`;
  if (trace.kind === "ChannelOutcome") return trace.payload.outcome.type === "NoArrival"
    ? `TransmissionId ${trace.payload.transmission_id} · formal NoArrival`
    : `TransmissionId ${trace.payload.transmission_id} · ${formatNanoseconds(trace.payload.outcome.first_arrival_delay_ns)} · TL ${trace.payload.outcome.aggregate_transmission_loss_db} dB · ${trace.payload.outcome.path_count} paths`;
  return `ReceptionId ${trace.payload.reception_id} · TransmissionId ${trace.payload.transmission_id}${trace.payload.quality ? ` · SNR ${trace.payload.quality.signal_to_noise_ratio_db} dB · BER ${trace.payload.quality.bit_error_rate}` : " · 无质量证据"}`;
}

export function cumulativeTrends(events: readonly RunEventDto[]) {
  let tx = 0; let reception = 0; let fusionCycles = 0;
  const transmissions: TrendPoint[] = []; const receptions: TrendPoint[] = []; const cycles: TrendPoint[] = [];
  const ber: TrendPoint[] = []; const delay: TrendPoint[] = []; const loss: TrendPoint[] = [];
  for (const event of events) {
    const timeNs = event.trace.occurred_at_ns;
    if (event.trace.kind === "Transmission") transmissions.push({ timeNs, value: ++tx });
    if (event.trace.kind === "Reception") {
      receptions.push({ timeNs, value: ++reception });
      if (event.trace.payload.quality) ber.push({ timeNs, value: event.trace.payload.quality.bit_error_rate });
    }
    if (event.trace.kind === "CycleCommit") cycles.push({ timeNs, value: ++fusionCycles });
    if (event.trace.kind === "ChannelOutcome" && event.trace.payload.outcome.type === "Signal") {
      delay.push({ timeNs, value: Number(BigInt(event.trace.payload.outcome.first_arrival_delay_ns)) / 1e9 });
      loss.push({ timeNs, value: event.trace.payload.outcome.aggregate_transmission_loss_db });
    }
  }
  return { transmissions, receptions, cycles, ber, delay, loss };
}

export interface ActivityLink {
  sender: string | null;
  receiver: string | null;
  outcome: string;
  phase: "Transmission" | "Signal" | "NoArrival" | "Reception";
  transmissionId: string;
}

export function latestActiveLink(events: readonly RunEventDto[]): ActivityLink | null {
  const senders = new Map<string, string>();
  for (const event of events) if (event.trace.kind === "Transmission") senders.set(event.trace.payload.transmission_id, event.trace.payload.sender_node_id);
  const latest = events.at(-1);
  if (!latest) return null;
  if (latest.trace.kind === "Transmission") {
    const receiver = latest.trace.payload.target.type === "Unicast" ? latest.trace.payload.target.node_id : null;
    return { sender: latest.trace.payload.sender_node_id, receiver, outcome: receiver ? "发送开始" : "广播发送", phase: "Transmission", transmissionId: latest.trace.payload.transmission_id };
  }
  if (latest.trace.kind === "ChannelOutcome") {
    const phase = latest.trace.payload.outcome.type === "NoArrival" ? "NoArrival" : "Signal";
    return { sender: senders.get(latest.trace.payload.transmission_id) ?? null, receiver: latest.trace.payload.receiver_node_id, outcome: phase === "Signal" ? "信号到达" : "无有效到达", phase, transmissionId: latest.trace.payload.transmission_id };
  }
  if (latest.trace.kind === "Reception") return { sender: senders.get(latest.trace.payload.transmission_id) ?? null, receiver: latest.trace.payload.receiver_node_id, outcome: latest.trace.payload.disposition, phase: "Reception", transmissionId: latest.trace.payload.transmission_id };
  return null;
}

export function projectionCounts(events: readonly RunEventDto[]) {
  let transmissions = 0; let signals = 0; let noArrivals = 0; let receptions = 0; let cycles = 0;
  let localDeliveries = 0; let overheard = 0; let notDecoded = 0; let relayEnqueue = 0;
  for (const event of events) {
    if (event.trace.kind === "Transmission") transmissions += 1;
    if (event.trace.kind === "ChannelOutcome" && event.trace.payload.outcome.type === "Signal") signals += 1;
    if (event.trace.kind === "ChannelOutcome" && event.trace.payload.outcome.type === "NoArrival") noArrivals += 1;
    if (event.trace.kind === "Reception") {
      receptions += 1;
      if (event.trace.payload.disposition === "LocalDelivery") localDeliveries += 1;
      if (event.trace.payload.disposition === "Overheard") overheard += 1;
      if (event.trace.payload.disposition === "NotDecoded") notDecoded += 1;
      if (event.trace.payload.disposition === "RelayEnqueue") relayEnqueue += 1;
    }
    if (event.trace.kind === "CycleCommit") cycles += 1;
  }
  return { transmissions, signals, noArrivals, receptions, cycles, localDeliveries, overheard, notDecoded, relayEnqueue };
}
