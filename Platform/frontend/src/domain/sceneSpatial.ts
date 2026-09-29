import type { SceneSpatialDomain } from "../api/types";
export function isSceneDomain(value: unknown): value is SceneSpatialDomain {
  if (!value || typeof value !== "object") return false;
  const d = value as SceneSpatialDomain;
  return d.model === "ProfileExtrusion" && [d.x_min_m, d.x_max_m, d.y_min_m, d.y_max_m, d.surface_clearance_m, d.bottom_clearance_m].every(Number.isFinite)
    && d.x_max_m > d.x_min_m && d.y_max_m > d.y_min_m && d.surface_clearance_m >= 0 && d.bottom_clearance_m >= 0;
}
export function insideSceneXY(d: SceneSpatialDomain, x: number, y: number) {
  return x >= d.x_min_m && x <= d.x_max_m && y >= d.y_min_m && y <= d.y_max_m;
}
