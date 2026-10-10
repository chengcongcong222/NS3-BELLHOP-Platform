import { automaticSampling, depthSamples, type DepthSampling } from "../domain/environmentSampling";
export { uniformDepths } from "../domain/environmentSampling";

export function DepthSamplingInput({ label, value, waterDepth, onChange }: {
  label: string; value: DepthSampling; waterDepth: number; onChange: (value: DepthSampling) => void;
}) {
  const { points, error } = depthSamples(value, waterDepth);
  const compact = (n: number) => Number(n.toPrecision(5));
  const automatic = value.mode === "automatic";
  return <fieldset className="depth-sampling"><legend>{label}深度采样</legend>
    <div className="sampling-count-row"><label>采样点数<input aria-label={`${label}深度采样点数`} type="number" min="1" step="1" disabled={!automatic} value={automatic ? value.count : points.length || value.depths.split(",").length} onChange={e => onChange({ ...value, count: e.target.value })} /></label>
      <span>{automatic ? "自动覆盖计算水深" : "保留已保存的自定义深度"}</span></div>
    {error ? <p role="alert">{error}</p> : <small>{points.length === 1 ? `固定深度 ${compact(points[0])} m` : `${compact(points[0])}～${compact(points.at(-1)!)} m · ${points.length} 点${automatic ? ` · 间隔 ${compact((points.at(-1)! - points[0]) / (points.length - 1))} m` : ""}`}</small>}
    <details><summary>高级：自定义深度</summary>
      <label className="sampling-custom-toggle"><input type="checkbox" checked={!automatic} onChange={e => onChange(e.target.checked ? { mode: "custom", count: String(points.length), depths: points.join(", ") } : automaticSampling(value.count))} />使用自定义深度</label>
      {!automatic && <label>{label}深度采样（m，逗号分隔）<textarea rows={3} value={value.depths} onChange={e => onChange({ ...value, depths: e.target.value, count: String(e.target.value.split(",").length) })} /></label>}
      <p>自动范围由计算水深确定；水面、海底各留 {compact(Math.min(1, waterDepth / 100))} m。这是可改的采样边界设置，不是水文数据。单点取水深中点。</p>
    </details>
    {!automatic && <button type="button" onClick={() => onChange(automaticSampling(value.count))}>改为全水深均分</button>}
  </fieldset>;
}
