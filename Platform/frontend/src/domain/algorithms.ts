export interface AlgorithmParameter {
  id: string; label: string; type: "integer" | "duration_ns" | "number" | "boolean" | "enum";
  unit: string; default: string; min: string; max: string; help: string; advanced: boolean; choices: string[];
}
export interface AlgorithmDescriptor {
  id: string; version: string; package_sha256: string; interface: "planning-mac-v1";
  label: string; description: string; parameters: AlgorithmParameter[];
}
export interface AlgorithmSelection {
  id: string; version: string; package_sha256: string; interface: "planning-mac-v1";
  parameters: Record<string, string>;
}
export interface AlgorithmCatalog {
  schema: "platform.algorithm-catalog.v1"; worker_sha256: string; algorithms: AlgorithmDescriptor[];
}
export const algorithmKey = (algorithm: Pick<AlgorithmSelection, "id" | "version" | "package_sha256">) =>
  `${algorithm.id}@${algorithm.version}@${algorithm.package_sha256}`;
export function algorithmDefaults(algorithm: AlgorithmDescriptor): AlgorithmSelection {
  return { id: algorithm.id, version: algorithm.version, package_sha256: algorithm.package_sha256,
    interface: algorithm.interface, parameters: Object.fromEntries(algorithm.parameters.map(field => [field.id, field.default])) };
}
export function algorithmIssues(selection: AlgorithmSelection | undefined, catalog: AlgorithmCatalog | undefined): string[] {
  if (!catalog) return ["算法目录尚未加载，请稍后重试。"];
  if (!selection) return ["请选择已安装算法。"];
  const algorithm = catalog.algorithms.find(item => algorithmKey(item) === algorithmKey(selection));
  if (!algorithm || selection.interface !== algorithm.interface) return ["原算法版本已不可用，请显式选择已安装版本；原参数未被替换。"];
  if (Object.keys(selection.parameters).length !== algorithm.parameters.length) return ["算法参数缺失或含无关字段，请核对配置。"];
  return algorithm.parameters.flatMap(field => {
    const value = selection.parameters[field.id];
    let valid = typeof value === "string" && value.length <= 128;
    if (valid && (field.type === "integer" || field.type === "duration_ns")) {
      valid = /^(0|-?[1-9]\d*)$/.test(value) && BigInt(value) >= BigInt(field.min) && BigInt(value) <= BigInt(field.max);
    } else if (valid && field.type === "number") {
      valid = /^-?(0|[1-9]\d*)(\.\d+)?([eE][+-]?\d+)?$/.test(value) && Number.isFinite(Number(value)) && Number(value) >= Number(field.min) && Number(value) <= Number(field.max);
    } else if (valid && field.type === "boolean") valid = ["true", "false"].includes(value);
    else if (valid && field.type === "enum") valid = field.choices.includes(value);
    return valid ? [] : [`${field.label}：请填写允许范围内的值。`];
  });
}
export function isAlgorithmCatalog(value: unknown): value is AlgorithmCatalog {
  if (!value || typeof value !== "object") return false;
  const c = value as AlgorithmCatalog;
  try {
  return c.schema === "platform.algorithm-catalog.v1" && typeof c.worker_sha256 === "string" && Array.isArray(c.algorithms) && c.algorithms.every(a =>
    a && [a.id, a.version, a.package_sha256, a.label, a.description].every(v => typeof v === "string") && a.interface === "planning-mac-v1" && Array.isArray(a.parameters) && a.parameters.every(p =>
      p && [p.id, p.label, p.unit, p.default, p.min, p.max, p.help].every(v => typeof v === "string") && ["integer", "duration_ns", "number", "boolean", "enum"].includes(p.type) && typeof p.advanced === "boolean" && Array.isArray(p.choices) && p.choices.every(v => typeof v === "string")) &&
      new Set(a.parameters.map(p => p.id)).size === a.parameters.length && algorithmIssues(algorithmDefaults(a), c).length === 0);
  } catch { return false; }
}
