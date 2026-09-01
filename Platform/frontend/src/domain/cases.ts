import type { EnvironmentDto, ExperimentDto, ScenarioDto } from "../api/types";

export interface SimulationCase {
  id: string;
  title: string;
  summary: string;
  tags: readonly string[];
  environment: EnvironmentDto;
  scenario: ScenarioDto;
  experiment: ExperimentDto;
  classification: "验收基准" | "扩展示例" | "科研配置";
}

const copy: Record<string, Pick<SimulationCase, "title" | "summary" | "tags" | "classification">> = {
  Acceptance4Node: {
    title: "浅水四节点协同通信与融合",
    summary: "在参考浅水声学环境中验证移动节点通信、融合中心接收与方位特征融合。",
    tags: ["浅水", "4 节点", "协同融合", "验收基准"],
    classification: "验收基准",
  },
  Extended6Node: {
    title: "六节点协同通信扩展示例",
    summary: "在同一参考环境中扩展节点规模，用于观察更复杂的共享信道通信与融合过程。",
    tags: ["浅水", "6 节点", "规模扩展"],
    classification: "扩展示例",
  },
};

export function composeCases(
  environments: readonly EnvironmentDto[],
  scenarios: readonly ScenarioDto[],
  experiments: readonly ExperimentDto[],
): SimulationCase[] {
  return experiments.flatMap((experiment) => {
    const scenario = scenarios.find((candidate) =>
      candidate.scenario_id === experiment.scenario.scenario_id &&
      candidate.version === experiment.scenario.version);
    if (!scenario) return [];
    const environment = environments.find((candidate) =>
      candidate.environment_asset_id === scenario.environment.environment_asset_id &&
      candidate.asset_format_version === scenario.environment.asset_format_version);
    if (!environment) return [];
    const product = copy[experiment.fusion.acceptance_profile] ?? {
      title: experiment.name,
      summary: `使用 ${scenario.name} 的可运行科研配置。`,
      tags: [scenario.name, experiment.routing.mode, experiment.mac.mode],
      classification: "科研配置" as const,
    };
    return [{
      id: `${experiment.experiment_id}-v${experiment.version}`,
      environment,
      scenario,
      experiment,
      ...product,
    }];
  });
}
