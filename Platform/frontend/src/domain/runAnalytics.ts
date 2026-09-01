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

export function latestActiveLink(events: readonly RunEventDto[]) {
  const senders = new Map<string, string>();
  for (const event of events) if (event.trace.kind === "Transmission") senders.set(event.trace.payload.transmission_id, event.trace.payload.sender_node_id);
  const latest = [...events].reverse().find((event) => event.trace.kind === "ChannelOutcome" || event.trace.kind === "Reception");
  if (!latest || (latest.trace.kind !== "ChannelOutcome" && latest.trace.kind !== "Reception")) return null;
  return { sender: senders.get(latest.trace.payload.transmission_id) ?? null, receiver: latest.trace.payload.receiver_node_id, outcome: latest.trace.kind === "ChannelOutcome" ? latest.trace.payload.outcome.type : latest.trace.payload.disposition };
}
