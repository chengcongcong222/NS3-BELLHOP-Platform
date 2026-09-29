import { describe, expect, it } from "vitest";
import type { TrajectoryAssetDto } from "../api/types";
import type { ScenarioDraft } from "../domain/workspace";
import { trajectoryRunCoverage } from "../domain/trajectoryCoverage";

function fixture(times: number[] = [0, 6], nodeCount = 2): {
  scene: ScenarioDraft; assets: TrajectoryAssetDto[];
} {
  const ids = Array.from({ length: nodeCount }, (_, index) => String(index));
  return {
    scene: {
      kind: "scenario", id: "coverage-test", name: "Coverage test", environmentAssetId: "test-water",
      fusionCenterNodeId: "0", provenance: { kind: "new", label: "新建" }, updatedAt: "test",
      nodes: ids.map(node_id => ({ node_id, can_transmit: node_id !== "0", can_receive: true,
        duplex_mode: "HalfDuplex", initial_position: { x_meters: 0, y_meters: 0, z_meters: -10 },
        initial_velocity: { x_meters_per_second: 0, y_meters_per_second: 0, z_meters_per_second: 0 } })),
      trajectoryBindings: ids.map(node_id => ({ node_id, asset_id: "shared-track", version: 2,
        track_id: `N${node_id}` })),
    },
    assets: [{ asset_id: "shared-track", version: 2, name: "Explicit unit-test fixture",
      coordinate_system: "LocalCartesianMetersZUp", source: "test-fixture", source_notice: null,
      checksum_sha256: "test-only", tracks: ids.map(id => ({ track_id: `N${id}`,
        points: times.map(time_s => ({ time_s, x_m: 0, y_m: 0, z_m: -10 })) })) }],
  };
}

describe("trajectory Run coverage preflight", () => {
  it("accepts the exact closed endpoint and rejects a trajectory one nanosecond short", () => {
    const exact = fixture();
    expect(trajectoryRunCoverage(exact.scene, exact.assets, "1", "6000000000", "Tdma"))
      .toEqual({ endNs: 6000000000n, issues: [] });
    const short = fixture([0, 5.999999999]);
    expect(trajectoryRunCoverage(short.scene, short.assets, "1", "6000000000", "Tdma").issues).toHaveLength(2);
  });

  it("uses one slot per non-centre node and does not add guard time again", () => {
    const { scene, assets } = fixture([0, 36], 4);
    const before = JSON.stringify({ scene, assets });
    expect(trajectoryRunCoverage(scene, assets, "2", "6000000000", "Tdma"))
      .toEqual({ endNs: 36000000000n, issues: [] });
    expect(JSON.stringify({ scene, assets })).toBe(before);
  });

  it("does not extrapolate a single-point asset across a positive-duration Run", () => {
    const { scene, assets } = fixture([0]);
    expect(trajectoryRunCoverage(scene, assets, "1", "6000000000", "Tdma").issues).toHaveLength(2);
  });

  it("accepts a stationary trajectory with explicit temporal coverage", () => {
    const { scene, assets } = fixture([0, 100]);
    expect(trajectoryRunCoverage(scene, assets, "1", "6000000000", "Tdma").issues).toEqual([]);
  });

  it("rejects a nonzero start rather than implicitly resetting its time origin", () => {
    const { scene, assets } = fixture([5, 100]);
    const result = trajectoryRunCoverage(scene, assets, "1", "6000000000", "Tdma");
    expect(result.issues).toHaveLength(2);
    expect(result.issues[0]).toContain("5～100");
  });

  it("requires the exact asset version and track for every node including NodeId zero", () => {
    const { scene, assets } = fixture();
    scene.trajectoryBindings![0].version = 1;
    scene.trajectoryBindings![1].track_id = "missing";
    const issues = trajectoryRunCoverage(scene, assets, "1", "6000000000", "Tdma").issues;
    expect(issues).toHaveLength(2);
    expect(issues[0]).toContain("N0");
    expect(issues[1]).toContain("N1");
  });

  it("does not substitute any available asset when a binding or asset is missing", () => {
    const { scene, assets } = fixture();
    scene.trajectoryBindings = [scene.trajectoryBindings![1]];
    scene.trajectoryBindings[0].asset_id = "missing";
    expect(trajectoryRunCoverage(scene, assets, "1", "6000000000", "Tdma").issues).toHaveLength(2);
  });

  it("does not confuse sample spacing or sample count with a MAC frame", () => {
    const sparse = fixture([0, 6]);
    const irregular = fixture([0, .37, 1.21, 4.5, 6]);
    expect(trajectoryRunCoverage(sparse.scene, sparse.assets, "1", "6000000000", "Tdma"))
      .toEqual(trajectoryRunCoverage(irregular.scene, irregular.assets, "1", "6000000000", "Tdma"));
  });

  it("uses exact BigInt multiplication beyond the JavaScript safe integer range", () => {
    const { scene, assets } = fixture([0, 10_000_000]);
    expect(trajectoryRunCoverage(scene, assets, "9007199254740993", "1", "Tdma"))
      .toEqual({ endNs: 9007199254740993n, issues: [] });
  });

  it("rejects a computed duration outside signed int64 nanoseconds", () => {
    const { scene, assets } = fixture();
    const result = trajectoryRunCoverage(scene, assets, "9223372036854775808", "1", "Tdma");
    expect(result.endNs).toBeNull();
    expect(result.issues).not.toHaveLength(0);
  });

  it.each(["", "0", "-1", "1.5", "01", "1e3", " 2", "NaN", "Infinity"])
  ("rejects a noncanonical positive integer %j", value => {
    const { scene, assets } = fixture();
    expect(trajectoryRunCoverage(scene, assets, value, "1", "Tdma").endNs).toBeNull();
    expect(trajectoryRunCoverage(scene, assets, "1", value, "Tdma").endNs).toBeNull();
  });

  it.each([Number.NaN, Number.POSITIVE_INFINITY, Number.MAX_VALUE])
  ("reports invalid trajectory time %j without throwing during BigInt conversion", end => {
    const { scene, assets } = fixture([0, end]);
    let issues: string[] = [];
    expect(() => { issues = trajectoryRunCoverage(scene, assets, "1", "6000000000", "Tdma").issues; }).not.toThrow();
    expect(issues).toHaveLength(2);
  });

  it("does not reuse the TDMA formula for an unsupported MAC", () => {
    const { scene, assets } = fixture();
    expect(trajectoryRunCoverage(scene, assets, "1", "6000000000", "Aloha").endNs).toBeNull();
  });
});
