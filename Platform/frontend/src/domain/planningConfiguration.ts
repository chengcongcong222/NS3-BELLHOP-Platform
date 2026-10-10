import type { ScenarioDto } from "../api/types";
import type { ExperimentDraft } from "./workspace";

/** Preserve integer nanoseconds across the seconds-based editing fields. */
export function planningSecondsInput(nanoseconds: string | undefined): string {
  if (!/^(0|[1-9]\d*)$/.test(nanoseconds ?? "")) return "";
  const value = BigInt(nanoseconds!);
  const fractional = String(value % 1000000000n).padStart(9, "0").replace(/0+$/, "");
  return String(value / 1000000000n) + (fractional ? `.${fractional}` : "");
}

export function planningNanosecondsInput(seconds: string): string {
  const match = /^(\d+)(?:\.(\d{0,9}))?$/.exec(seconds);
  return match ? String(BigInt(match[1]) * 1000000000n + BigInt((match[2] ?? "").padEnd(9, "0"))) : "";
}

export function planningConfigurationIssues(value: ExperimentDraft, nodes: ScenarioDto["nodes"]): string[] {
  if (value.executionMode !== undefined && value.executionMode !== "PlanningV1") return ["当前草稿的执行模式不受支持，不能按历史模式运行。"];
  if (value.executionMode !== "PlanningV1") return [];
  const issues: string[] = [];
  if (!["Direct", "ShortestPath"].includes(value.routingMode)) issues.push("规划实验仅支持通用直达或最短路径路由，请重新选择。");
  if (!["Tdma", "AdaptiveTdma", "RegisteredMac"].includes(value.macMode)) issues.push("规划实验的 MAC 尚未接入运行。");
  if (value.macMode === "RegisteredMac" && !value.macAlgorithm) issues.push("请选择已安装算法。");
  if (!/^[1-9]\d*$/.test(value.planningCycleDurationNs ?? "") || BigInt(value.planningCycleDurationNs!) > 9223372036854775807n) issues.push("业务周期必须是有效的正整数纳秒时长。");
  if (value.macMode === "AdaptiveTdma" && (!/^(0|[1-9]\d*)$/.test(value.adaptiveSafetyMarginNs ?? "") || BigInt(value.adaptiveSafetyMarginNs!) > 9223372036854775807n)) issues.push("自适应 TDMA 安全余量必须是有效的非负整数纳秒时长。");
  const nodeId = (id: string) => /^(0|[1-9]\d*)$/.test(id) && BigInt(id) <= 18446744073709551615n;
  if (value.workload === "BearingFusion") {
    if (!nodes.some(node => node.node_id === value.fusionReceiverNodeId && node.can_receive)) issues.push("请选择场景内具备接收能力的融合应用接收节点。");
    if (!nodes.some(node => node.node_id !== value.fusionReceiverNodeId && node.can_transmit)) issues.push("融合业务至少需要一个可发送的观测节点。");
  } else {
    const flows = value.flows ?? [];
    if (!flows.length) issues.push("通信性能实验至少需要一条显式通信流。");
    const pairs = new Set<string>();
    flows.forEach((flow, index) => {
      const prefix = `通信流 ${index + 1}`;
      if (!nodeId(flow.source_node_id) || !nodes.some(node => node.node_id === flow.source_node_id && node.can_transmit)) issues.push(`${prefix}的源节点必须存在且可发送。`);
      if (!nodeId(flow.destination_node_id) || !nodes.some(node => node.node_id === flow.destination_node_id && node.can_receive)) issues.push(`${prefix}的目的节点必须存在且可接收。`);
      if (flow.source_node_id === flow.destination_node_id) issues.push(`${prefix}的源节点与目的节点必须不同。`);
      if (!Number.isInteger(flow.packets_per_cycle) || flow.packets_per_cycle < 1 || flow.packets_per_cycle > 1000) issues.push(`${prefix}每周期包数必须是 1～1000 的整数。`);
      const key = `${flow.source_node_id}:${flow.destination_node_id}`;
      if (pairs.has(key)) issues.push(`${prefix}与已有通信流方向重复，请调整每周期包数。`);
      pairs.add(key);
    });
  }
  return issues;
}
