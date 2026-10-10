import type { CommunicationFlowDto } from "../api/types";
export interface AlgorithmParameter {
  id: string; label: string; type: "integer" | "duration_ns" | "number" | "boolean" | "enum";
  unit: string; default: string; min: string; max: string; help: string; advanced: boolean; choices: string[];
}
export interface AlgorithmDescriptor {
  id: string; version: string; package_sha256: string; interface: "planning-mac-v1" | "planning-mac-v2";
  label: string; description: string; parameters: AlgorithmParameter[];
  knowledge?: "global-simulation";
  output?: "serial-flow-schedule-v1";
  inputs?: { name: string; required: boolean }[];
}
export interface AlgorithmSelection {
  id: string; version: string; package_sha256: string; interface: "planning-mac-v1" | "planning-mac-v2";
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
export function editableAlgorithm(selection: AlgorithmSelection): AlgorithmSelection {
  return { id: selection.id, version: selection.version, package_sha256: selection.package_sha256,
    interface: selection.interface, parameters: structuredClone(selection.parameters) };
}
export function isAlgorithmCatalog(value: unknown): value is AlgorithmCatalog {
  if (!value || typeof value !== "object") return false;
  const c = value as AlgorithmCatalog;
  try {
  return c.schema === "platform.algorithm-catalog.v1" && typeof c.worker_sha256 === "string" && Array.isArray(c.algorithms) && c.algorithms.every(a =>
    a && [a.id, a.version, a.package_sha256, a.label, a.description].every(v => typeof v === "string") &&
      (a.interface === "planning-mac-v1" || (a.interface === "planning-mac-v2" && a.knowledge === "global-simulation" &&
        a.output === "serial-flow-schedule-v1" && Array.isArray(a.inputs) &&
        new Set(a.inputs.map(input => input.name)).size === a.inputs.length &&
        ["node-state-v1", "flow-demand-v1", "routing-view-v1"].every(name => a.inputs?.some(input => input.name === name && input.required)) &&
        a.inputs.every(input => ["node-state-v1", "flow-demand-v1", "routing-view-v1", "link-prediction-v1", "previous-feedback-v1"].includes(input.name) && typeof input.required === "boolean"))) &&
      Array.isArray(a.parameters) && a.parameters.every(p =>
      p && [p.id, p.label, p.unit, p.default, p.min, p.max, p.help].every(v => typeof v === "string") && ["integer", "duration_ns", "number", "boolean", "enum"].includes(p.type) && typeof p.advanced === "boolean" && Array.isArray(p.choices) && p.choices.every(v => typeof v === "string")) &&
      new Set(a.parameters.map(p => p.id)).size === a.parameters.length && algorithmIssues(algorithmDefaults(a), c).length === 0);
  } catch { return false; }
}

/** Assigned on explicit flow creation/interface selection, never from row index
 * during serialization. Reorder and edits retain existing identities. */
export function bindFlowIdentities(flows: CommunicationFlowDto[], flowScoped: boolean): CommunicationFlowDto[] {
  if (!flowScoped) return flows.map(flow => { const copy = { ...flow }; delete copy.flow_id; return copy; });
  const used = new Set(flows.flatMap(flow => flow.flow_id == null ? [] : [flow.flow_id]));
  let next = 0n;
  for (const id of used) if (/^(0|[1-9]\d*)$/.test(id) && BigInt(id) >= next) next = BigInt(id) + 1n;
  return flows.map(flow => {
    if (flow.flow_id != null) return { ...flow };
    if (next > 18446744073709551615n) next = 0n;
    while (used.has(String(next))) next++;
    const flow_id = String(next++); used.add(flow_id);
    return { ...flow, flow_id };
  });
}
