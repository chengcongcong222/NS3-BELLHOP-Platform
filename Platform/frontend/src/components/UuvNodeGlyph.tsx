import "./sceneDepthProfile.css";

export type UuvNodeTone = "standard" | "selected" | "peer" | "fusion" | "tx" | "arrival" | "success" | "failure";

/** A schematic node marker, not a physical hull or heading measurement. */
export function UuvNodeGlyph({ tone = "standard", yawRadians, scale = 1 }: {
  tone?: UuvNodeTone;
  /** Only pass a heading supplied by the recorded trajectory, never inferred velocity. */
  yawRadians?: number;
  scale?: number;
}) {
  const knownHeading = yawRadians !== undefined && Number.isFinite(yawRadians);
  const rotation = knownHeading ? -yawRadians * 180 / Math.PI : 0;
  return <g className={`uuv-node-glyph tone-${tone}`} transform={`rotate(${rotation}) scale(${scale})`}
    data-heading-source={knownHeading ? "trajectory" : "schematic"}>
    <title>{knownHeading ? "水下节点 · 轨迹记录的航向" : "水下节点示意符号 · 未记录航向，朝向不代表姿态"}</title>
    <path className="uuv-fins" d="M-10 -5 L-18 -12 L-17 -4 M-10 5 L-18 12 L-17 4 M1 -5 L-1 -11 L7 -5 M1 5 L-1 11 L7 5" />
    <path className="uuv-hull" d="M-19 -4 Q-4 -8 8 -6 Q17 -5 22 0 Q17 5 8 6 Q-4 8 -19 4 Z" />
    <path className="uuv-keel" d="M-17 0 H14 M-20 -7 V7" />
    <circle className="uuv-port" cx="8" cy="0" r="2.1" />
  </g>;
}
