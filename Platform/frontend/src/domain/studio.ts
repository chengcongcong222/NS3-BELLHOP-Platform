import type { RunEventDto, PositionSample } from "../api/types";
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
  event: RunEventDto;
}
export function studioEvents(events: readonly RunEventDto[]): StudioEvent[] {
  // Position observations update the canvas but do not become separate
  // operator timeline steps. All other rows retain authoritative journal order.
  return events.filter(event => event.trace.kind !== "NodePosition").map(event => ({
    key: `trace-${event.sequence}`,
    at: event.trace.occurred_at_ns,
    kind: event.trace.kind,
    title: eventTitle(event),
    event,
  }));
}
export function positionSamples(events: readonly RunEventDto[]): PositionSample[] {
  return events.flatMap(event => event.trace.kind === "NodePosition" ? [{
    node_id: event.trace.payload.node_id,
    at_ns: event.trace.occurred_at_ns,
    x_meters: event.trace.payload.x_meters,
    y_meters: event.trace.payload.y_meters,
    z_meters: event.trace.payload.z_meters,
  }] : []);
}
export function positionAt(samples: readonly PositionSample[], nodeId: string, at: string) {
  const node = samples.filter(p => p.node_id === nodeId).sort((a, b) => BigInt(a.at_ns) < BigInt(b.at_ns) ? -1 : BigInt(a.at_ns) > BigInt(b.at_ns) ? 1 : 0);
  const exact = node.find(p => p.at_ns === at);
  if (exact) return exact;
  const time = BigInt(at);
  const before = [...node].reverse().find(p => BigInt(p.at_ns) < time);
  const after = node.find(p => BigInt(p.at_ns) > time);
  if (!before || !after) return null;
  const numerator = Number(time - BigInt(before.at_ns));
  const denominator = Number(BigInt(after.at_ns) - BigInt(before.at_ns));
  const alpha = numerator / denominator;
  return {
    node_id: nodeId,
    at_ns: at,
    x_meters: before.x_meters + alpha * (after.x_meters - before.x_meters),
    y_meters: before.y_meters + alpha * (after.y_meters - before.y_meters),
    z_meters: before.z_meters + alpha * (after.z_meters - before.z_meters),
  };
}
export function timePercent(at: string, first: string, last: string) {
  const span = BigInt(last) - BigInt(first);
  return span <= 0n ? 0 : Number((BigInt(at) - BigInt(first)) * 10000n / span) / 100;
}
export function seekTime(rows: readonly Pick<StudioEvent, "at">[], percent: number) {
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
