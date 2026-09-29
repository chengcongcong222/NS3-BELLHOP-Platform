export interface WorldPoint { x_meters: number; y_meters: number }

export function createBoundedSceneView(bounds: { x_min_m: number; x_max_m: number; y_min_m: number; y_max_m: number }, viewport: { width: number; height: number; padding: number; paddingY?: number }) {
  const width = viewport.width - 2 * viewport.padding, height = viewport.height - 2 * (viewport.paddingY ?? viewport.padding);
  const scale = Math.min(width / (bounds.x_max_m - bounds.x_min_m), height / (bounds.y_max_m - bounds.y_min_m));
  const cx = (bounds.x_max_m + bounds.x_min_m) / 2, cy = (bounds.y_max_m + bounds.y_min_m) / 2;
  return {
    xDomain: [cx - width / (2 * scale), cx + width / (2 * scale)] as const,
    yDomain: [cy - height / (2 * scale), cy + height / (2 * scale)] as const,
    point: (p: WorldPoint) => ({ x: viewport.width / 2 + (p.x_meters - cx) * scale, y: viewport.height / 2 - (p.y_meters - cy) * scale }),
    world: (x: number, y: number) => ({ x_meters: cx + (x - viewport.width / 2) / scale, y_meters: cy - (y - viewport.height / 2) / scale }),
  };
}

/** Fit recorded coordinates to a rectangular display while preserving equal XY scale.
 * This is a viewport, not an environmental boundary or a position prediction. */
export function createRecordedSceneView(points: readonly WorldPoint[], viewport: { width: number; height: number; padding: number; paddingY?: number }) {
  const valid = points.filter(p => Number.isFinite(p.x_meters) && Number.isFinite(p.y_meters));
  const xs = valid.length ? valid.map(p => p.x_meters) : [0], ys = valid.length ? valid.map(p => p.y_meters) : [0];
  const left = Math.min(...xs), right = Math.max(...xs), bottom = Math.min(...ys), top = Math.max(...ys);
  const width = viewport.width - 2 * viewport.padding, height = viewport.height - 2 * (viewport.paddingY ?? viewport.padding);
  const scale = Math.min(width / (Math.max(10, right - left) * 1.2), height / (Math.max(10, top - bottom) * 1.2));
  const centerX = (left + right) / 2, centerY = (bottom + top) / 2;
  const xDomain = [centerX - width / (2 * scale), centerX + width / (2 * scale)] as const;
  const yDomain = [centerY - height / (2 * scale), centerY + height / (2 * scale)] as const;
  return { xDomain, yDomain, point: (p: WorldPoint) => ({
    x: viewport.width / 2 + (p.x_meters - centerX) * scale,
    y: viewport.height / 2 - (p.y_meters - centerY) * scale,
  }) };
}
export interface WorldViewTransform { readonly extent: number; readonly derivedFromNodes: boolean; point(point: WorldPoint): { x: number; y: number } }
export function createWorldViewTransform(points: readonly WorldPoint[], maximumRangeMeters: number | undefined, viewport = { width: 640, height: 390, padding: 30 }): WorldViewTransform { const maximumCoordinate = Math.max(1, ...points.flatMap((point) => [Math.abs(point.x_meters), Math.abs(point.y_meters)])); const formalExtent = maximumRangeMeters && maximumRangeMeters > 0 ? maximumRangeMeters / 2 : 0; const extent = Math.max(formalExtent, maximumCoordinate * 1.1, 1); const scale = Math.min((viewport.width - viewport.padding * 2) / (extent * 2), (viewport.height - viewport.padding * 2) / (extent * 2)); return { extent, derivedFromNodes: formalExtent === 0, point: (point) => ({ x: viewport.width / 2 + point.x_meters * scale, y: viewport.height / 2 - point.y_meters * scale }) }; }
