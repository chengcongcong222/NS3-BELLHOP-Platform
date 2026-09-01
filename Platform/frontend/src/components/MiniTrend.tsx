import type { TrendPoint } from "../domain/runAnalytics";

export function MiniTrend({ title, unit, points, empty }: { title: string; unit: string; points: readonly TrendPoint[]; empty: string }) {
  if (!points.length) return <article className="trend-card"><div><strong>{title}</strong><span>{unit}</span></div><p>{empty}</p></article>;
  const times = points.map((point) => BigInt(point.timeNs)); const minT = times.reduce((a, b) => a < b ? a : b); const maxT = times.reduce((a, b) => a > b ? a : b); const values = points.map((point) => point.value); const minV = Math.min(...values); const maxV = Math.max(...values);
  const coords = points.map((point, index) => { const x = 12 + Number((times[index] - minT) * 1000n / (maxT - minT || 1n)) / 1000 * 276; const y = 82 - ((point.value - minV) / Math.max(maxV - minV, 1e-18)) * 62; return `${x},${y}`; }).join(" ");
  return <article className="trend-card"><div><strong>{title}</strong><span>{unit}</span></div><svg viewBox="0 0 300 95" role="img" aria-label={`${title}趋势`}><path d="M12 82H288" className="trend-axis" /><polyline points={coords} className="trend-line" /></svg><small>最新 {points.at(-1)?.value} {unit} · {points.length} 个正式观测点</small></article>;
}
