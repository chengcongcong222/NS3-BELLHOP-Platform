import type { TrajectoryAssetDto } from "../api/types";
import type { ScenarioDraft } from "./workspace";

/** Current TDMA planner assigns one slot per non-centre node. Guard is
 * inside the slot, not added again. This is a preflight, not a new clock. */
export function trajectoryRunCoverage(scene: ScenarioDraft, assets: readonly TrajectoryAssetDto[],
  cycles: string, slotDurationNs: string, mac: string): { endNs: bigint | null; issues: string[] } {
  if (mac !== "Tdma") return { endNs: null, issues: ["当前时间覆盖检查仅支持已开放的 TDMA。"] };
  if (!/^[1-9]\d*$/.test(cycles) || !/^[1-9]\d*$/.test(slotDurationNs)) return { endNs: null, issues: ["通信周期数与时隙长度必须为正整数。"] };
  const endNs = BigInt(cycles) * BigInt(slotDurationNs) * BigInt(Math.max(0, scene.nodes.length - 1));
  if (endNs > 9223372036854775807n) return { endNs: null, issues: ["总仿真时长超出运行时间范围。"] };
  const issues: string[] = [];
  for (const node of scene.nodes) {
    const binding = scene.trajectoryBindings?.find(item => item.node_id === node.node_id);
    if (!binding) { issues.push(`N${node.node_id} 尚未绑定轨迹。`); continue; }
    const asset = assets.find(item => item.asset_id === binding.asset_id && item.version === binding.version);
    const track = asset?.tracks.find(item => item.track_id === binding.track_id);
    if (!track?.points.length) { issues.push(`N${node.node_id} 绑定的轨迹版本或轨道不存在。`); continue; }
    const start = track.points[0].time_s, end = track.points.at(-1)!.time_s;
    const scaledEnd = end * 1e9;
    if (!Number.isFinite(start) || !Number.isFinite(scaledEnd) || scaledEnd < 0 || scaledEnd >= 2 ** 63 || start !== 0 || BigInt(Math.round(scaledEnd)) < endNs) {
      issues.push(`N${node.node_id} 轨迹覆盖 ${start}～${end} s，本次需 0～${Number(endNs) / 1e9} s。请延长并另存轨迹，或减少通信周期数。`);
    }
  }
  return { endNs, issues };
}
