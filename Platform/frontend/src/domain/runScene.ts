import type { PositionSample, ResultDto, ScenarioDto } from "../api/types";

export type RunScene = Pick<ScenarioDto, "name"> & {
  nodes: Array<Pick<ScenarioDto["nodes"][number], "node_id" | "initial_position">>;
  fusion_center_node_id: string | null;
  observationsOnly?: boolean;
  spatial_domain?: ScenarioDto["spatial_domain"];
};

/** Older running servers have no definition endpoint. Observations permit a
 * limited replay, never reconstruction of capabilities, bindings or settings. */
export function observedRunScene(samples: readonly PositionSample[], result?: ResultDto): RunScene | null {
  const first = new Map<string, PositionSample>();
  for (const sample of samples) {
    const previous = first.get(sample.node_id);
    if (!previous || BigInt(sample.at_ns) < BigInt(previous.at_ns)) first.set(sample.node_id, sample);
  }
  if (!first.size) return null;
  return {
    name: "历史运行观测",
    observationsOnly: true,
    nodes: [...first.values()].sort((a, b) => BigInt(a.node_id) < BigInt(b.node_id) ? -1 : 1)
      .map(sample => ({ node_id: sample.node_id, initial_position: sample })),
    fusion_center_node_id: result?.nodes.find(node => node.is_fusion_center)?.node_id ?? null,
  };
}

export function mergePositionSamples(recorded: readonly PositionSample[], streamed: readonly PositionSample[]): PositionSample[] {
  const merged = new Map<string, PositionSample>();
  for (const sample of [...recorded, ...streamed]) merged.set(`${sample.node_id}:${sample.at_ns}`, sample);
  return [...merged.values()].sort((a, b) => BigInt(a.at_ns) < BigInt(b.at_ns) ? -1 : BigInt(a.at_ns) > BigInt(b.at_ns) ? 1 : 0);
}
