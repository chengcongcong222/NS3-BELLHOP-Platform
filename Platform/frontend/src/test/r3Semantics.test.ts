import { describe, expect, it } from "vitest";
import { environment } from "./fixtures";
import { newEnvironmentDraft, validateDraft } from "../domain/workspace";
import { createWorldViewTransform } from "../domain/worldViewTransform";
import { projectionCounts } from "../domain/runAnalytics";
import { productLabel } from "../productLanguage";

describe("R3 product truth contracts", () => {
  it("derives an environment boundary without pretending to clone its field content", () => {
    const draft = newEnvironmentDraft(environment);
    expect(draft.soundSpeedProfile).toEqual([]);
    expect(draft.bathymetry).toEqual([]);
    expect(draft.provenance).toMatchObject({ kind: "derived", sourceId: environment.environment_asset_id });
    expect(draft.provenance.kind === "derived" && draft.provenance.inherited).toContain("范围");
    expect(draft.provenance.kind === "derived" && draft.provenance.omitted).toEqual(expect.arrayContaining(["SSP", "Bathymetry", "传播场内容"]));
    expect(validateDraft(draft)).toContain("SSP 至少需要两个有效且范围内的采样点");
  });

  it("uses one uniform world transform and extends a formal range only when nodes require it", () => {
    const formal = createWorldViewTransform([{ x_meters: 10, y_meters: 20 }], 1000);
    const expanded = createWorldViewTransform([{ x_meters: 900, y_meters: 0 }], 1000);
    expect(formal.derivedFromNodes).toBe(false);
    expect(formal.extent).toBe(500);
    expect(expanded.extent).toBeGreaterThan(formal.extent);
    const origin = formal.point({ x_meters: 0, y_meters: 0 });
    const x = formal.point({ x_meters: 100, y_meters: 0 });
    const y = formal.point({ x_meters: 0, y_meters: 100 });
    expect(Math.abs(x.x - origin.x)).toBeCloseTo(Math.abs(y.y - origin.y));
  });

  it("keeps Reception as processing evidence and classifies each disposition", () => {
    const reception = (disposition: "LocalDelivery" | "Overheard" | "NotDecoded" | "RelayEnqueue") => ({ run_id: "r", sequence: disposition, trace: { occurred_at_ns: "1", kind: "Reception" as const, payload: { reception_id: "1", transmission_id: "1", packet_id: "1", receiver_node_id: "2", disposition, quality: null } } });
    const counts = projectionCounts([reception("LocalDelivery"), reception("Overheard"), reception("NotDecoded"), reception("RelayEnqueue")]);
    expect(counts).toMatchObject({ receptions: 4, localDeliveries: 1, overheard: 1, notDecoded: 1, relayEnqueue: 1 });
  });

  it("presents lifecycle text in Chinese while preserving raw enum elsewhere", () => {
    expect(productLabel("Running")).toBe("运行中");
    expect(productLabel("Completed")).toBe("已完成");
    expect(productLabel("NotFullyEvaluated")).toBe("尚未完整评估");
  });
});
