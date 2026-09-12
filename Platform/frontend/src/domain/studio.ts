import type { ResultDto, RunEventDto, PositionSample } from "../api/types";
import { eventTitle } from "./runAnalytics";

// Extracted from the read-only legacy StudioCanvas buildEventCurve; DTO-independent.
export function eventCurve(tx: { x: number; y: number }, rx: { x: number; y: number }, arcIndex = 0) {
  const dx = rx.x - tx.x, dy = rx.y - tx.y, length = Math.max(Math.hypot(dx, dy), 1);
  const curvature = Math.min(42, 16 + arcIndex * 10);
  const x = (tx.x + rx.x) / 2 - dy / length * curvature;
  const y = (tx.y + rx.y) / 2 + dx / length * curvature;
  return `M ${tx.x.toFixed(1)} ${tx.y.toFixed(1)} Q ${x.toFixed(1)} ${y.toFixed(1)} ${rx.x.toFixed(1)} ${rx.y.toFixed(1)}`;
}
export interface StudioEvent {
  key: string; at: string; kind: string; title: string;
  event?: RunEventDto; fusion?: ResultDto["fusion_results"][number];
}
export function studioEvents(events: readonly RunEventDto[], result?: ResultDto): StudioEvent[] {
  // Preserve journal order; never manufacture a RunEventSequence for result observations.
  const rows: StudioEvent[] = events.map(event => ({ key: `trace-${event.sequence}`, at: event.trace.occurred_at_ns,
    kind: event.trace.kind, title: event.trace.kind === "ChannelOutcome"
      ? `信道查询 · N${event.trace.payload.receiver_node_id} · ${event.trace.payload.outcome.type === "Signal" ? "存在传播路径" : "无有效路径"}`
      : eventTitle(event), event }));
  for (const fusion of result?.fusion_results ?? []) {
    const row = { key: `fusion-${fusion.fusion_sequence}`, at: fusion.completed_at_ns, kind: "Fusion", title: `融合轮次 ${fusion.fusion_sequence} 完成`, fusion };
    // Result completion occurs before the same-time CycleCommit, not an SSE event.
    const next = rows.findIndex(item => BigInt(item.at) > BigInt(row.at) || (item.at === row.at && item.kind === "CycleCommit"));
    if (next < 0) rows.push(row); else rows.splice(next, 0, row);
  }
  return rows;
}
export function positionAt(samples: readonly PositionSample[], nodeId: string, at: string) {
  // Exact authoritative query sample only. No browser extrapolation or velocity integration.
  return samples.find(p => p.node_id === nodeId && p.at_ns === at) ?? null;
}
export function timePercent(at: string, first: string, last: string) {
  const span = BigInt(last) - BigInt(first);
  return span <= 0n ? 0 : Number((BigInt(at) - BigInt(first)) * 10000n / span) / 100;
}
export function seekTime(rows: readonly StudioEvent[], percent: number) {
  if (!rows.length) return 0;
  const first = BigInt(rows[0].at), last = BigInt(rows.at(-1)!.at);
  const target = first + (last - first) * BigInt(Math.round(percent * 100)) / 10000n;
  let best = 0, distance: bigint | null = null;
  rows.forEach((row, i) => { const delta = BigInt(row.at) - target; const d = delta < 0n ? -delta : delta;
    if (distance === null || d < distance) { best = i; distance = d; } });
  return best;
}
export function replayDelay(a: string, b: string, speed: number) {
  const ms = Number(BigInt(b) - BigInt(a)) / 1e6 / speed;
  return { milliseconds: Math.max(100, Math.min(2000, ms)), compressed: ms > 2000 };
}
