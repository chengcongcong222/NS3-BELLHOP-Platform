import { useState } from "react";
import { useQuery } from "@tanstack/react-query";
import { apiClient } from "../api/client";
import { compactNumber } from "../domain/runtimePresentation";
import { ErrorState } from "./common";

type Plot = { available: boolean; reason?: string; frequency_hz: number;
  source_depths_m: number[]; receiver_depths_m: number[]; ranges_m: number[];
  field: { receiver_index: number; range_index: number; tl_db: number | null }[];
  arrivals_sha256: string; tl_definition: string;
  ray_plot: { available: boolean; reason?: string; source_depth_m: number; frequency_hz: number; sha256: string; display_sampling?: { original_points: number; displayed_points: number }; rays: { angle_degrees: number; points: number[][] }[] };
};

export function AcousticPlots({ environmentId, buildId }: { environmentId?: string; buildId?: string }) {
  // Sample indices belong to one immutable calculation, not to the surrounding editor.
  return <ResourcePlots key={buildId ? `build:${buildId}` : `environment:${environmentId}`} environmentId={environmentId} buildId={buildId} />;
}

function ResourcePlots({ environmentId, buildId }: { environmentId?: string; buildId?: string }) {
  const [source, setSource] = useState(0);
  const [rayAngle, setRayAngle] = useState("all");
  const query = useQuery({ queryKey: ["acoustic-plots", environmentId, buildId, source],
    queryFn: async () => {
      const base = buildId ? `/authoring/environment-builds/${encodeURIComponent(buildId)}` : `/environments/${encodeURIComponent(environmentId!)}`;
      const response = await fetch(apiClient.url(`${base}/acoustic-plots?source_index=${source}&receiver_index=0&range_index=0&view=rays`));
      if (!response.ok) {
        const detail = await response.json().catch(() => null);
        throw new Error(typeof detail?.error?.message === "string" ? detail.error.message : "声线读取失败，请重试或检查已保存的计算结果。");
      }
      return await response.json() as Plot;
    }, retry: false });
  const data = query.data;
  const ray = data?.ray_plot;
  // A stale server/legacy artifact must not relabel the first saved source as
  // the depth selected by the user. The API also validates this association.
  const matchingRay = data?.available && ray?.available &&
    Math.abs(ray.source_depth_m - data.source_depths_m[source]) <= Math.max(1e-5, Math.abs(ray.source_depth_m) * 1e-6);
  const selectedRays = matchingRay ? ray.rays.filter(r => rayAngle === "all" || String(r.angle_degrees) === rayAngle) : [];
  return <section className="acoustic-plots" aria-label="Bellhop 信道图">
    <header><div><h2>水声信道</h2><small>{data?.available ? `${compactNumber(data.frequency_hz / 1000)} kHz · 离线计算结果` : "读取正式离线计算结果"}</small></div>
      <strong>声线路径</strong></header>
    {query.isPending && <p role="status">正在校验并读取声线…</p>}{query.error && <><ErrorState error={query.error} /><button onClick={() => query.refetch()}>重试</button></>}
    {data && !data.available && <p>{data.reason}</p>}
    {data?.available && <>
      <div className="acoustic-selectors"><label>发射深度<select value={source} onChange={e => { setSource(Number(e.target.value)); setRayAngle("all"); }}>{data.source_depths_m.map((v, i) => <option key={i} value={i}>{compactNumber(v, 3)} m</option>)}</select></label>
        {matchingRay && <label>发射角（已计算声线）<select value={rayAngle} onChange={e => setRayAngle(e.target.value)}><option value="all">全部（{ray!.rays.length} 条）</option>{[...new Set(ray!.rays.map(r => r.angle_degrees))].sort((a, b) => a - b).map(angle => <option key={angle} value={String(angle)}>{compactNumber(angle, 3)}°</option>)}</select></label>}
      </div>
      {matchingRay ? <><Rays data={ray!} selectedRays={selectedRays} /><p>发射深度 {ray!.source_depth_m} m · {compactNumber(ray!.frequency_hz / 1000)} kHz · 显示 {selectedRays.length} / {ray!.rays.length} 条真实声线。</p><div className="ray-legend"><span>红色：初始朝海面</span><span>蓝色：初始水平或朝海底</span><small>颜色区分发射方向，不表示声强。</small></div></> : <p>{ray?.available ? `未保存发射深度 ${data.source_depths_m[source]} m 的声线；请另存并重新计算，不以其他深度的声线替代。` : ray?.reason}</p>}
      <details><summary>计算说明与数据来源</summary><p>仅绘制已保存的声线；筛选角度不重算、不改变坐标轴范围。修改频率、环境或采样范围后需要重新计算。</p>{ray?.display_sampling && ray.display_sampling.displayed_points < ray.display_sampling.original_points && <p>显示已抽稀：保留原始端点和转折点，未修改计算数据。</p>}<code>声场构建记录 SHA256 {data.arrivals_sha256}</code>{matchingRay && <p><code>声线数据 SHA256 {ray!.sha256}</code></p>}</details>
    </>}
  </section>;
}

function Axes({ xmax, ymax, xLabel, yLabel, down = false, ymin = 0 }: { xmax: number; ymax: number; xLabel: string; yLabel: string; down?: boolean; ymin?: number }) {
  return <g className="acoustic-axes"><text x="14" y="19">{yLabel}</text><text x="335" y="276" textAnchor="middle">{xLabel}</text>{[0, .25, .5, .75, 1].map(f => <g key={f}><path d={`M65 ${45 + f * 190} H605 M${65 + f * 540} 45 V235`} fill="none" stroke="#dde5eb" /><text x="57" y={49 + f * 190} textAnchor="end">{compactNumber(ymin + (ymax - ymin) * (down ? f : 1 - f))}</text><text x={65 + f * 540} y="254" textAnchor="middle">{compactNumber(xmax * f)}</text></g>)}</g>;
}
// Frozen presentation retained for historical component regression only. No product route exposes it.
export function FrozenLossField({ data }: { data: Pick<Plot, "receiver_depths_m" | "ranges_m" | "field"> }) {
  if (data.receiver_depths_m.length === 1) return <LossCurve data={data} />;
  const xs = data.ranges_m, ys = data.receiver_depths_m;
  const xmax = Math.max(1, xs.at(-1)!), ymax = Math.max(1, ys.at(-1)!);
  const vals = data.field.flatMap(c => c.tl_db === null ? [] : [c.tl_db]);
  const low = vals.reduce((min, v) => Math.min(min, v), Infinity), high = vals.reduce((max, v) => Math.max(max, v), -Infinity);
  const edges = (axis: number[], i: number, end: number) => [i ? (axis[i - 1] + axis[i]) / 2 : axis[0], i === axis.length - 1 ? end : (axis[i] + axis[i + 1]) / 2];
  return <><svg viewBox="0 0 660 285" role="img" aria-label="非相干传输损失距离深度图">
    {data.field.map((cell, i) => { const [a, b] = edges(xs, cell.range_index, xmax), [c, d] = edges(ys, cell.receiver_index, ymax); const fraction = cell.tl_db === null ? 0 : (cell.tl_db - low) / Math.max(high - low, 1); return <rect key={i} x={65 + a / xmax * 540} y={45 + c / ymax * 190} width={Math.max(.5, (b - a) / xmax * 540)} height={Math.max(.5, (d - c) / ymax * 190)} fill={cell.tl_db === null ? "#d8d8d8" : `hsl(204 62% ${30 + fraction * 62}%)`}><title>{`${xs[cell.range_index]} m · 深度 ${ys[cell.receiver_index]} m · ${cell.tl_db === null ? "无到达" : compactNumber(cell.tl_db, 1) + " dB"}`}</title></rect>; })}
    <Axes xmax={xmax} ymax={ymax} xLabel="收发距离 / m" yLabel="接收深度 / m" down />
  </svg><div className="tl-legend">{vals.length ? <><span>{compactNumber(low, 1)} dB</span><i /><span>{compactNumber(high, 1)} dB</span></> : "本深度网格均无到达"}<small>颜色越浅，传播损失越大</small></div></>;
}
function LossCurve({ data, receiverIndex = 0 }: { data: Pick<Plot, "receiver_depths_m" | "ranges_m" | "field">; receiverIndex?: number }) {
  const cells = data.field.filter(cell => cell.receiver_index === receiverIndex).sort((a, b) => a.range_index - b.range_index);
  const valid = cells.filter((cell): cell is typeof cell & { tl_db: number } => cell.tl_db !== null);
  if (!valid.length) return <p role="status">该接收深度的所有距离采样均无到达，无法绘制损失曲线。</p>;
  const low = valid.reduce((min, cell) => Math.min(min, cell.tl_db), Infinity);
  const high = valid.reduce((max, cell) => Math.max(max, cell.tl_db), -Infinity);
  const padding = Math.max(1, (high - low) * .1), ymin = low - padding, ymax = high + padding;
  const xmax = Math.max(1, data.ranges_m.at(-1)!);
  const x = (index: number) => 65 + data.ranges_m[index] / xmax * 540;
  const y = (tl: number) => 235 - (tl - ymin) / (ymax - ymin) * 190;
  return <svg viewBox="0 0 660 285" role="img" aria-label="单深度传输损失距离曲线">
    <Axes xmax={xmax} ymin={ymin} ymax={ymax} xLabel="收发距离 / m" yLabel="非相干传输损失 / dB" />
    {cells.map((cell, i) => { const previous = cells[i - 1]; return <g key={cell.range_index}>
      {cell.tl_db !== null && previous !== undefined && previous.tl_db !== null && previous.range_index + 1 === cell.range_index && <line x1={x(previous.range_index)} y1={y(previous.tl_db)} x2={x(cell.range_index)} y2={y(cell.tl_db)} stroke="#12648e" strokeWidth="2" />}
      {cell.tl_db === null ? <path d={`M${x(cell.range_index) - 4} 231 l8 8 m-8 0 l8 -8`} stroke="#6b7280" fill="none"><title>{data.ranges_m[cell.range_index]} m · 无到达（不表示零损失）</title></path> : <circle cx={x(cell.range_index)} cy={y(cell.tl_db)} r="4" fill="#12648e"><title>{data.ranges_m[cell.range_index]} m · {compactNumber(cell.tl_db, 1)} dB</title></circle>}
    </g>; })}
  </svg>;
}
function Rays({ data, selectedRays }: { data: Plot["ray_plot"]; selectedRays: Plot["ray_plot"]["rays"] }) {
  let xmax = 1, ymax = 1;
  for (const ray of data.rays) for (const [x, y] of ray.points) { xmax = Math.max(xmax, x); ymax = Math.max(ymax, y); }
  return <svg viewBox="0 0 660 285" role="img" aria-label="Bellhop 真实声线路径"><Axes xmax={xmax} ymax={ymax} xLabel="传播距离 / m" yLabel="深度 / m" down />
    {selectedRays.map((r, i) => <polyline key={i} points={r.points.map(([x, y]) => `${65 + x / xmax * 540},${45 + y / ymax * 190}`).join(" ")} fill="none" stroke={r.angle_degrees < 0 ? "#b32636" : "#176b87"} strokeWidth=".8"><title>发射角 {compactNumber(r.angle_degrees, 1)}°</title></polyline>)}
  </svg>;
}
