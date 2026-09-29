import type { EnvironmentDto, SceneSpatialDomain } from "../api/types";
import { environmentSampleProfile } from "./SceneDepthProfile";

export function SceneDomainEditor({ value, onChange, environment, inputs, supported }: {
  value?: SceneSpatialDomain | null; onChange: (domain: SceneSpatialDomain | null) => void;
  environment?: EnvironmentDto; inputs?: unknown; supported: boolean;
}) {
  const profile = environmentSampleProfile(inputs);
  const enable = () => { if (profile && environment) onChange({ model: "ProfileExtrusion", x_min_m: profile.points[0].range_meters,
    x_max_m: Math.min(profile.points.at(-1)!.range_meters, environment.axes.horizontal_range.maximum),
    y_min_m: -100, y_max_m: 100, surface_clearance_m: 1, bottom_clearance_m: 5 }); };
  return <details className="scene-domain-editor" open={!!value}><summary>场景范围与安全距离{value ? " · 已限定" : " · 尚未关联地形"}</summary>
    <p>声场计算距离是两节点之间的距离，不是地图尺寸。启用后，场景 X 对应原地形测线距离；Y 方向采用同一地形剖面的简化模型，不代表已取得三维海底。</p>
    <label><input type="checkbox" disabled={!supported || !profile} checked={!!value} onChange={e => e.target.checked ? enable() : onChange(null)} />采用上述二维剖面假设，限定本实验范围</label>
    {!supported && <p>当前后端尚不支持范围校验，请使用新版服务。</p>}
    {!profile && <p>该环境缺少已保存的地形采样，不能推造海底边界。</p>}
    {value && <><div className="domain-fields">{([
      ["x_min_m", "X 起点（m）"], ["x_max_m", "X 终点（m）"], ["y_min_m", "Y 下界（m）"], ["y_max_m", "Y 上界（m）"],
      ["surface_clearance_m", "距水面至少（m）"], ["bottom_clearance_m", "离海底至少（m）"],
    ] as const).map(([key, label]) => <label key={key}>{label}<input type="number" value={value[key]} onChange={e => onChange({ ...value, [key]: Number(e.target.value) })} /></label>)}</div>
      <p>运行前检查完整轨迹，不只检查起点。越界会拒绝启动，不会把节点拉回边界或擅自改轨迹。Y 初始建议为 ±100 m，可按实验修改。</p></>}
  </details>;
}
