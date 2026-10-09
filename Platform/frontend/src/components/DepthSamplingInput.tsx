import { useEffect, useState } from "react";

export function uniformDepths(start: number, end: number, count: number, waterDepth: number): number[] {
  if (![start, end, count, waterDepth].every(Number.isFinite) || !Number.isInteger(count) || count < 1 || count > 64)
    throw new Error("采样点数须为 1～64 的整数。");
  if (start <= 0 || end >= waterDepth || end < start || (count > 1 && start === end) || (count === 1 && start !== end))
    throw new Error("深度须在水面与海底之间；固定深度使用 1 点且起止相同。");
  const points = Array.from({ length: count }, (_, i) => count === 1 ? start : start + (end - start) * i / (count - 1));
  if (points.some((p, i) => !Number.isFinite(p) || (i > 0 && p <= points[i - 1]))) throw new Error("深度间隔过小，请减少采样点数。");
  return points;
}

export function DepthSamplingInput({ label, value, waterDepth, onChange }: {
  label: string; value: string; waterDepth: number; onChange: (value: string) => void;
}) {
  const points = value.split(",").map(p => p.trim() === "" ? NaN : Number(p));
  const [start, setStart] = useState(String(points[0]));
  const [end, setEnd] = useState(String(points.at(-1)));
  const [count, setCount] = useState(String(points.length));
  const [error, setError] = useState("");
  const [notice, setNotice] = useState("");
  useEffect(() => {
    const items = value.split(",").map(Number);
    setStart(String(items[0])); setEnd(String(items.at(-1))); setCount(String(items.length));
    setError("");
  }, [value]);
  const valid = points.every((p, i) => Number.isFinite(p) && p > 0 && p < waterDepth && (!i || p > points[i - 1]));
  const step = points.length > 1 ? (points.at(-1)! - points[0]) / (points.length - 1) : 0;
  const uniform = valid && points.every((p, i) => Math.abs(p - points[0] - i * step) <= 1e-8 * Math.max(1, Math.abs(p)));
  const apply = () => {
    try {
      if ([start, end, count].some(v => !v.trim())) throw new Error("请填写起点、终点和采样点数。");
      onChange(uniformDepths(Number(start), Number(end), Number(count), waterDepth).join(", "));
      setError(""); setNotice("均分已应用；保存或计算后写入服务器。");
    } catch (e) { setError((e as Error).message); setNotice(""); }
  };
  return <fieldset className="depth-sampling"><legend>{label}深度采样</legend>
    <div className="sampling-inputs">
      <label>起点（m）<input aria-label={`${label}深度起点（m）`} type="number" value={start} onChange={e => { setStart(e.target.value); setNotice(""); }} /></label>
      <label>终点（m）<input aria-label={`${label}深度终点（m）`} type="number" value={end} onChange={e => { setEnd(e.target.value); setNotice(""); }} /></label>
      <label>点数<input aria-label={`${label}深度采样点数`} type="number" min="1" max="64" value={count} onChange={e => { setCount(e.target.value); setNotice(""); }} /></label>
      <button type="button" onClick={apply}>应用均分</button>
    </div>
    <small>{!valid ? "当前采样点超界或无效，请调整。" : uniform ? points.length === 1 ? `当前：固定深度 ${points[0]} m` : `当前：${points.length} 点，间隔 ${Number(step.toPrecision(5))} m` : `当前：${points.length} 个自定义采样点（保留原分布）`}</small>
    {error && <p role="alert">{error}</p>}{notice && <p role="status">{notice}</p>}
    <details><summary>查看 / 编辑具体采样点</summary><label>{label}深度采样（m，逗号分隔）<input value={value} onChange={e => { setNotice(""); onChange(e.target.value); }} /></label></details>
  </fieldset>;
}
