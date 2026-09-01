import type { EnvironmentDto, ExperimentDto, ScenarioDto } from "../api/types";

export type DraftKind = "environment" | "scenario" | "experiment";

export interface EnvironmentDraft {
  kind: "environment";
  id: string;
  name: string;
  description: string;
  maximumDepthMeters: number;
  maximumRangeMeters: number;
  centerFrequencyHz: number;
  soundSpeedProfile: Array<{ depthMeters: number; speedMetersPerSecond: number }>;
  bathymetry: Array<{ rangeMeters: number; depthMeters: number }>;
  constructionMode: "BellhopOffline";
  updatedAt: string;
}

export interface ScenarioDraft {
  kind: "scenario";
  id: string;
  name: string;
  environmentAssetId: string;
  fusionCenterNodeId: string;
  nodes: ScenarioDto["nodes"];
  updatedAt: string;
}

export interface ExperimentDraft {
  kind: "experiment";
  id: string;
  name: string;
  sourceExperimentId: string | null;
  scenarioId: string;
  routingMode: string;
  macMode: string;
  bitRateBitsPerSecond: string;
  centerFrequencyHz: number;
  occupiedBandwidthHz: number;
  sourceLevelDb: number;
  guardIntervalNs: string;
  simulationCycleCount: string;
  deterministicSeed: string;
  networkUpdateIntervalCycles: string;
  updatedAt: string;
}

export type WorkspaceDraft = EnvironmentDraft | ScenarioDraft | ExperimentDraft;
const key = "ns3-bellhop-platform.workbench-drafts.v1";

function now(): string {
  return new Date().toISOString();
}

export function loadDrafts(): WorkspaceDraft[] {
  try {
    const parsed = JSON.parse(localStorage.getItem(key) ?? "[]") as unknown;
    return Array.isArray(parsed) ? parsed.filter(isDraft) : [];
  } catch {
    return [];
  }
}

export function saveDraft(draft: WorkspaceDraft): WorkspaceDraft[] {
  const next = [...loadDrafts().filter((item) => !(item.kind === draft.kind && item.id === draft.id)), draft];
  localStorage.setItem(key, JSON.stringify(next));
  window.dispatchEvent(new Event("workbench-drafts-changed"));
  return next;
}

export function deleteDraft(kind: DraftKind, id: string): WorkspaceDraft[] {
  const next = loadDrafts().filter((item) => !(item.kind === kind && item.id === id));
  localStorage.setItem(key, JSON.stringify(next));
  window.dispatchEvent(new Event("workbench-drafts-changed"));
  return next;
}

export function newEnvironmentDraft(source?: EnvironmentDto): EnvironmentDraft {
  const id = `environment-draft-${Date.now()}`;
  return {
    kind: "environment", id,
    name: source ? `${source.environment_asset_id} 副本` : "未命名声学环境",
    description: source?.provenance.source_description ?? "",
    maximumDepthMeters: source?.axes.receiver_depth.maximum ?? 100,
    maximumRangeMeters: source?.axes.horizontal_range.maximum ?? 5000,
    centerFrequencyHz: source?.axes.frequency.minimum ?? 25000,
    soundSpeedProfile: [{ depthMeters: 0, speedMetersPerSecond: 1500 }, { depthMeters: 100, speedMetersPerSecond: 1490 }],
    bathymetry: [{ rangeMeters: 0, depthMeters: 100 }, { rangeMeters: source?.axes.horizontal_range.maximum ?? 5000, depthMeters: 100 }],
    constructionMode: "BellhopOffline", updatedAt: now(),
  };
}

export function scenarioDraftFrom(source: ScenarioDto): ScenarioDraft {
  return {
    kind: "scenario", id: `scenario-draft-${Date.now()}`, name: `${source.name} 副本`,
    environmentAssetId: source.environment.environment_asset_id,
    fusionCenterNodeId: source.fusion_center_node_id,
    nodes: structuredClone(source.nodes), updatedAt: now(),
  };
}

export function experimentDraftFrom(source: ExperimentDto): ExperimentDraft {
  return {
    kind: "experiment", id: `experiment-draft-${Date.now()}`, name: `${source.name} 派生实验`,
    sourceExperimentId: source.experiment_id, scenarioId: source.scenario.scenario_id,
    routingMode: source.routing.mode, macMode: source.mac.mode,
    bitRateBitsPerSecond: source.phy.bit_rate_bits_per_second,
    centerFrequencyHz: source.phy.center_frequency_hz,
    occupiedBandwidthHz: source.phy.occupied_bandwidth_hz,
    sourceLevelDb: source.phy.source_level_db_re_1upa_at_1m,
    guardIntervalNs: source.mac.guard_interval_ns,
    simulationCycleCount: source.simulation_cycle_count,
    deterministicSeed: source.deterministic_seed,
    networkUpdateIntervalCycles: source.network_update_interval_cycles,
    updatedAt: now(),
  };
}

export function validateDraft(draft: WorkspaceDraft): string[] {
  const issues: string[] = [];
  if (!draft.name.trim()) issues.push("名称不能为空");
  if (draft.kind === "environment") {
    if (!(draft.maximumDepthMeters > 0)) issues.push("最大水深必须大于 0");
    if (!(draft.maximumRangeMeters > 0)) issues.push("最大距离必须大于 0");
    if (!(draft.centerFrequencyHz > 0)) issues.push("工作频率必须大于 0");
    if (draft.soundSpeedProfile.length < 2) issues.push("声速剖面至少需要两个采样点");
  } else if (draft.kind === "scenario") {
    if (!draft.nodes.length) issues.push("场景至少需要一个节点");
    if (!draft.nodes.some((node) => node.node_id === draft.fusionCenterNodeId)) issues.push("融合中心必须是场景内节点");
  } else {
    if (!/^[1-9][0-9]*$/.test(draft.bitRateBitsPerSecond)) issues.push("通信速率必须是正整数");
    if (!/^[1-9][0-9]*$/.test(draft.simulationCycleCount)) issues.push("仿真周期数必须是正整数");
    if (!/^(0|[1-9][0-9]*)$/.test(draft.deterministicSeed)) issues.push("随机种子必须是非负整数");
  }
  return issues;
}

function isDraft(value: unknown): value is WorkspaceDraft {
  if (!value || typeof value !== "object") return false;
  const record = value as Record<string, unknown>;
  return ["environment", "scenario", "experiment"].includes(String(record.kind)) &&
    typeof record.id === "string" && typeof record.name === "string";
}
