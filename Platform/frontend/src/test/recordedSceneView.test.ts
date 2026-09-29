import { describe, expect, it } from "vitest";
import { createRecordedSceneView } from "../domain/worldViewTransform";

describe("recorded scene viewport", () => {
  it("fits an offset rectangular scene without changing coordinates or equal XY scale", () => {
    const points = [{ x_meters: 1000, y_meters: 100 }, { x_meters: 3000, y_meters: 200 }];
    const view = createRecordedSceneView(points, { width: 980, height: 320, padding: 45 });
    expect(view.point(points[0]).x).toBeGreaterThan(45);
    expect(view.point(points[1]).x).toBeLessThan(935);
    const origin = view.point(points[0]);
    expect(view.point({ x_meters: 1010, y_meters: 100 }).x - origin.x)
      .toBeCloseTo(origin.y - view.point({ x_meters: 1000, y_meters: 110 }).y);
    expect(view.point({ x_meters: view.xDomain[0], y_meters: 100 }).x).toBeCloseTo(45);
    expect(points[0]).toEqual({ x_meters: 1000, y_meters: 100 });
  });
  it("has finite bounds for an empty or stationary scene", () => {
    for (const points of [[], [{ x_meters: 10000, y_meters: -200 }]]) {
      const view = createRecordedSceneView(points, { width: 980, height: 320, padding: 45 });
      expect([...view.xDomain, ...view.yDomain].every(Number.isFinite)).toBe(true);
      expect(view.xDomain[1]).toBeGreaterThan(view.xDomain[0]);
    }
  });
});
