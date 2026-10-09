import { describe, expect, it } from "vitest";
import { planningConfigurationIssues, planningNanosecondsInput, planningSecondsInput } from "../domain/planningConfiguration";
import { experimentDraftFrom } from "../domain/workspace";
import { definitionRunScene } from "../domain/runScene";
import { experiment, scenario } from "./fixtures";
import type { ExperimentDraft } from "../domain/workspace";
import type { RunDefinitionContextDto } from "../api/types";

const nodes = [0, 1, 2].map(id => ({ ...scenario.nodes[0], node_id: String(id), can_transmit: true, can_receive: true }));
const value: ExperimentDraft = { ...experimentDraftFrom(experiment), executionMode: "PlanningV1", planningCycleDurationNs: "30000000000", routingMode: "Direct", macMode: "AdaptiveTdma", adaptiveSafetyMarginNs: "0", workload: "CommunicationOnly", flows: [{ source_node_id: "0", destination_node_id: "1", packets_per_cycle: 1 }, { source_node_id: "1", destination_node_id: "0", packets_per_cycle: 1 }] };

describe("planning business configuration", () => {
  it("preserves nanosecond precision when editing seconds beyond Number's safe integer range", () => {
    expect(planningSecondsInput("9007199254740993")).toBe("9007199.254740993");
    expect(planningNanosecondsInput("9007199.254740993")).toBe("9007199254740993");
    expect(planningNanosecondsInput("0.000000001")).toBe("1");
    expect(planningNanosecondsInput("0")).toBe("0");
    expect(planningNanosecondsInput("1e3")).toBe("");
    expect(planningNanosecondsInput("-1")).toBe("");
  });
  it("supports NodeId zero, reverse flows and multiple destinations without hardware roles", () => {
    expect(planningConfigurationIssues({ ...value, flows: [...value.flows!, { source_node_id: "0", destination_node_id: "2", packets_per_cycle: 1000 }] }, nodes)).toEqual([]);
  });
  it.each([0, -1, 1.5, 1001, Number.NaN])("rejects invalid packets_per_cycle %s", packets_per_cycle => {
    expect(planningConfigurationIssues({ ...value, flows: [{ ...value.flows![0], packets_per_cycle }] }, nodes).join()).toContain("1～1000");
  });
  it("rejects missing, disabled, noncanonical, self and duplicated endpoints", () => {
    expect(planningConfigurationIssues({ ...value, flows: [{ source_node_id: "00", destination_node_id: "9", packets_per_cycle: 1 }] }, nodes)).toHaveLength(2);
    expect(planningConfigurationIssues(value, nodes.map(node => ({ ...node, can_transmit: false, can_receive: false })))).toHaveLength(4);
    expect(planningConfigurationIssues({ ...value, flows: [value.flows![0], value.flows![0]] }, nodes).join()).toContain("重复");
    expect(planningConfigurationIssues({ ...value, flows: [{ source_node_id: "0", destination_node_id: "0", packets_per_cycle: 1 }] }, nodes).join()).toContain("必须不同");
  });
  it("validates the application receiver independently of traffic and supports zero", () => {
    expect(planningConfigurationIssues({ ...value, workload: "BearingFusion", fusionReceiverNodeId: "0" }, nodes)).toEqual([]);
    expect(planningConfigurationIssues({ ...value, workload: "BearingFusion", fusionReceiverNodeId: null }, nodes).join()).toContain("接收节点");
  });
  it("does not retrofit planning requirements onto historical drafts", () => {
    expect(planningConfigurationIssues({ ...value, executionMode: undefined, flows: [] }, nodes)).toEqual([]);
    expect(experimentDraftFrom(experiment).executionMode).toBeUndefined();
  });
  it("rejects a legacy routing mode in PlanningV1 and never downgrades an unknown execution mode", () => {
    expect(planningConfigurationIssues({ ...value, routingMode: "DirectToFusionCenter" }, nodes).join()).toContain("重新选择");
    expect(planningConfigurationIssues({ ...value, executionMode: "UnknownMode" }, nodes).join()).toContain("不能按历史模式运行");
  });
  it("uses the frozen application receiver and never infers a protocol role from the scenario centre", () => {
    const context: RunDefinitionContextDto = { run_id: "planning", source: "AuthoredDefinition", definition_sha256: "sha", scenario, execution: {}, limitations: [], experiment: { ...experiment, execution_mode: "PlanningV1", fusion_receiver_node_id: "2", fusion: { ...experiment.fusion, workload: "BearingFusion" } } };
    expect(definitionRunScene(context).fusion_center_node_id).toBe("2");
    expect(definitionRunScene({ ...context, experiment: { ...context.experiment, fusion: { ...experiment.fusion, workload: "CommunicationOnly" } } }).fusion_center_node_id).toBeNull();
    expect(definitionRunScene({ ...context, experiment }).fusion_center_node_id).toBe(scenario.fusion_center_node_id);
  });
});
