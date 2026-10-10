// Compatibility guards of the current authoring service, NOT accuracy criteria
// or Bellhop limits. Keep in step with backend/environment_inputs.py.
export const environmentBuildLimits = { depthPoints: 64, rangePoints: 1000, cells: 200_000 };
export type DepthSampling = { mode: "automatic" | "custom"; count: string; depths: string };
// Retain the existing editor's initial depth count; only expand its automatic
// coverage. This is an editable starting point, not a validated accuracy tier.
export const automaticSampling = (count = "5"): DepthSampling => ({ mode: "automatic", count, depths: "" });

export function uniformDepths(start: number, end: number, count: number, waterDepth: number): number[] {
  if (![start, end, count, waterDepth].every(Number.isFinite) || !Number.isInteger(count) || count < 1 || count > environmentBuildLimits.depthPoints)
    throw new Error(`当前服务每组深度支持 1～${environmentBuildLimits.depthPoints} 点；这是计算保护上限，与水深无关。`);
  if (start <= 0 || end >= waterDepth || end < start || (count > 1 && start === end) || (count === 1 && start !== end))
    throw new Error("采样深度须在水面与海底之间。");
  const points = Array.from({ length: count }, (_, i) => count === 1 ? start : start + (end - start) * i / (count - 1));
  if (points.some((p, i) => !Number.isFinite(p) || (i > 0 && p <= points[i - 1]))) throw new Error("深度间隔过小，请减少点数。");
  return points;
}

export function depthSamples(config: DepthSampling, waterDepth: number): { points: number[]; error?: string } {
  try {
    if (!Number.isFinite(waterDepth) || waterDepth <= 0) throw new Error("请先确定有效计算水深。");
    if (config.mode === "automatic") {
      if (!config.count.trim()) throw new Error("请填写深度采样点数。");
      // Explicit UI boundary clearance, not measured ocean data. Scales for
      // shallow tanks; users can override with exact custom depths.
      const margin = Math.min(1, waterDepth / 100), count = Number(config.count);
      return { points: uniformDepths(count === 1 ? waterDepth / 2 : margin, count === 1 ? waterDepth / 2 : waterDepth - margin, count, waterDepth) };
    }
    const points = config.depths.split(",").map(p => p.trim() ? Number(p) : NaN);
    if (!points.length || points.length > environmentBuildLimits.depthPoints) throw new Error(`当前服务每组深度最多 ${environmentBuildLimits.depthPoints} 点。`);
    if (points.some((p, i) => !Number.isFinite(p) || p <= 0 || p >= waterDepth || (i > 0 && p <= points[i - 1])))
      throw new Error("自定义深度须为递增的正数，并浅于计算海底。");
    return { points };
  } catch (error) { return { points: [], error: (error as Error).message }; }
}

export function restoreSampling(axis: unknown, hint: unknown, waterDepth: number): DepthSampling {
  if (!Array.isArray(axis)) return automaticSampling();
  const saved = hint as Partial<DepthSampling> | undefined;
  if (saved?.mode === "automatic" && typeof saved.count === "string") {
    const config = automaticSampling(saved.count), resolved = depthSamples(config, waterDepth);
    if (resolved.points.length === axis.length && resolved.points.every((n, i) => n === axis[i])) return config;
  }
  return { mode: "custom", count: String(axis.length), depths: axis.join(", ") };
}

export function samplingBudget(source: number[], receiver: number[], ranges: number) {
  const countsValid = source.length > 0 && receiver.length > 0 && Number.isInteger(ranges) && ranges >= 2;
  const cells = countsValid ? source.length * receiver.length * ranges : 0;
  const maxRanges = source.length && receiver.length ? Math.min(environmentBuildLimits.rangePoints, Math.floor(environmentBuildLimits.cells / (source.length * receiver.length))) : 0;
  const errors: string[] = [];
  if (!Number.isInteger(ranges) || ranges < 2 || ranges > environmentBuildLimits.rangePoints)
    errors.push(`距离点数须为 2～${environmentBuildLimits.rangePoints} 的整数，当前为 ${Number.isFinite(ranges) ? ranges : "空值"}。`);
  if (cells > environmentBuildLimits.cells)
    errors.push(`共 ${cells.toLocaleString("en-US")} 个计算位置，超过平台当前上限 ${environmentBuildLimits.cells.toLocaleString("en-US")} 个（不是 Bellhop 限制）。`);
  return { cells, errors, maxRanges };
}

export function acousticParameterErrors(p: { bottomSoundSpeed: number; bottomDensity: number; bottomAttenuation: number; beamCount: number; minimumAngle: number; maximumAngle: number }, frequency: number) {
  const errors: string[] = [];
  if (!Number.isFinite(frequency) || frequency <= 0) errors.push("工作频率须大于 0。");
  if (!Number.isFinite(p.bottomSoundSpeed) || p.bottomSoundSpeed <= 0 || !Number.isFinite(p.bottomDensity) || p.bottomDensity <= 0 || !Number.isFinite(p.bottomAttenuation) || p.bottomAttenuation < 0) errors.push("请检查高级设置中的海底声速、密度和衰减。");
  if (!Number.isInteger(p.beamCount) || p.beamCount < 1 || p.beamCount > 100_000) errors.push("波束数须为 1～100000 的整数。");
  if (!Number.isFinite(p.minimumAngle) || !Number.isFinite(p.maximumAngle) || !(p.minimumAngle > -90 && p.minimumAngle < p.maximumAngle && p.maximumAngle < 90)) errors.push("发射角须在 -90°～90° 内，最小值小于最大值。");
  return errors;
}
