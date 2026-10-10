import { describe, expect, it } from "vitest";
import { algorithmDefaults, bindFlowIdentities, editableAlgorithm, isAlgorithmCatalog, type AlgorithmCatalog } from "../domain/algorithms";
import { algorithmCatalog } from "./algorithmFixtures";
describe("v2 generic algorithm adapter", () => {
  it("retains stable IDs across reorder/edit and only assigns IDs to new flows", () => {
    const flows = bindFlowIdentities([
      { source_node_id: "0", destination_node_id: "99", packets_per_cycle: 1 },
      { source_node_id: "0", destination_node_id: "99", packets_per_cycle: 1 }], true);
    expect(flows.map(flow => flow.flow_id)).toEqual(["0", "1"]);
    const reordered = bindFlowIdentities([flows[1], { ...flows[0], packets_per_cycle: 2 }], true);
    expect(reordered.map(flow => flow.flow_id)).toEqual(["1", "0"]);
    const added = bindFlowIdentities([...reordered, { source_node_id: "10", destination_node_id: "99", packets_per_cycle: 1 }], true);
    expect(added.map(flow => flow.flow_id)).toEqual(["1", "0", "2"]);
    expect(bindFlowIdentities(added, false).every(flow => !("flow_id" in flow))).toBe(true);
  });
  it("uses XML defaults and refuses unsupported v2 knowledge", () => {
    const catalog: AlgorithmCatalog = structuredClone(algorithmCatalog);
    catalog.algorithms[0] = { ...catalog.algorithms[0], interface: "planning-mac-v2", knowledge: "global-simulation",
      output: "serial-flow-schedule-v1", inputs: ["node-state-v1", "flow-demand-v1", "routing-view-v1"].map(name => ({ name, required: true })) };
    expect(isAlgorithmCatalog(catalog)).toBe(true);
    expect(algorithmDefaults(catalog.algorithms[0]).interface).toBe("planning-mac-v2");
    expect(isAlgorithmCatalog({ ...catalog, algorithms: [{ ...catalog.algorithms[0], knowledge: "future" }] })).toBe(false);
    expect(isAlgorithmCatalog({ ...catalog, algorithms: [{ ...catalog.algorithms[0], inputs: [] }] })).toBe(false);
  });
  it("does not carry frozen worker identity into an editable derived experiment", () => {
    const original = { ...algorithmDefaults(algorithmCatalog.algorithms[0]), worker_sha256: "frozen-worker" };
    const editable = editableAlgorithm(original);
    expect(editable).not.toHaveProperty("worker_sha256");
    expect(editable.parameters).toEqual(original.parameters);
    expect(editable.parameters).not.toBe(original.parameters);
  });
});
