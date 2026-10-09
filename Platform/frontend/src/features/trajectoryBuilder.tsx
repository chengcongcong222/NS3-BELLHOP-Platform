import { useMemo, useState } from "react";
import { useMutation, useQuery, useQueryClient } from "@tanstack/react-query";
import { Link } from "react-router-dom";
import { apiClient } from "../api/client";
import type { TrajectoryAssetDto, TrajectoryPointDto } from "../api/types";
import { ErrorState } from "../components/common";
import "./trajectoryBuilder.css";

type Kind = "Fixed" | "ConstantVelocity" | "Waypoints" | "Uuv5Dof" | "Csv";
type Triple = [number, number, number];
const kinds: Array<[Kind, string]> = [["Fixed", "静止"], ["ConstantVelocity", "匀速直线"], ["Waypoints", "航路点"], ["Uuv5Dof", "UUV 5-DOF（专家）"], ["Csv", "CSV 导入"]];
const number = (value: number) => Number.isFinite(value) ? value.toLocaleString("zh-CN", { maximumFractionDigits: 2 }) : "—";
const sourceName = (source: string) => source.includes("spatial-translation") ? "起点平移派生" : source.includes("derived") ? "派生轨迹" : source.includes("uuv") ? "水下航行器动力学生成" : source.includes("waypoint") ? "航路点生成" : source.includes("constant-velocity") ? "直线生成" : source.includes("fixed") ? "静止生成" : source.includes("csv") ? "文件导入" : "已保存轨迹";

/** Display-only interpolation of persisted points. Never advances simulation time. */
export function trajectoryPreviewPosition(points: TrajectoryPointDto[], time: number): TrajectoryPointDto | null {
  if (!points.length || time < points[0].time_s || time > points[points.length - 1].time_s) return null;
  let lo = 0, hi = points.length - 1;
  while (lo < hi) { const mid = Math.floor((lo + hi) / 2); if (points[mid].time_s < time) lo = mid + 1; else hi = mid; }
  const right = points[lo];
  if (right.time_s === time || lo === 0) return right;
  const left = points[lo - 1], ratio = (time - left.time_s) / (right.time_s - left.time_s);
  return { time_s: time, x_m: left.x_m + ratio * (right.x_m - left.x_m), y_m: left.y_m + ratio * (right.y_m - left.y_m), z_m: left.z_m + ratio * (right.z_m - left.z_m) };
}

function Coordinates({ values, onChange, labels, prefix }: { values: Triple; onChange: (values: Triple) => void; labels: TripleLabels; prefix: string }) {
  return <div className="trj-three-fields">{labels.map((label, index) => <label key={label}>{label}<input aria-label={`${prefix}${label}`} type="number" step="any" value={Number.isNaN(values[index]) ? "" : values[index]} onChange={event => onChange(values.map((v, i) => i === index ? event.target.valueAsNumber : v) as Triple)} /></label>)}</div>;
}
type TripleLabels = [string, string, string];

function TrajectoryCharts({ points, time }: { points: TrajectoryPointDto[]; time: number }) {
  const geometry = useMemo(() => {
    if (!points.length) return null;
    let minX = Infinity, maxX = -Infinity, minY = Infinity, maxY = -Infinity, minDepth = Infinity, maxDepth = -Infinity;
    for (const p of points) { minX = Math.min(minX, p.x_m); maxX = Math.max(maxX, p.x_m); minY = Math.min(minY, p.y_m); maxY = Math.max(maxY, p.y_m); minDepth = Math.min(minDepth, -p.z_m); maxDepth = Math.max(maxDepth, -p.z_m); }
    // Fixed viewBox uses one scale for both axes: a metre is never stretched.
    const scale = Math.min(540 / Math.max(maxX - minX, 10), 238 / Math.max(maxY - minY, 10));
    const centerX = (minX + maxX) / 2, centerY = (minY + maxY) / 2;
    const x = (v: number) => 350 + (v - centerX) * scale, y = (v: number) => 147 - (v - centerY) * scale;
    const depthPadding = Math.max((maxDepth - minDepth) * .12, 1);
    const top = minDepth - depthPadding, bottom = maxDepth + depthPadding;
    const start = points[0].time_s, end = points[points.length - 1].time_s;
    const tx = (v: number) => 66 + 568 * (end === start ? .5 : (v - start) / (end - start));
    const dy = (v: number) => 28 + 110 * (v - top) / (bottom - top);
    return { x, y, tx, dy, centerX, centerY, scale, start, end, top, bottom, plan: points.map(p => `${x(p.x_m)},${y(p.y_m)}`).join(" "), depth: points.map(p => `${tx(p.time_s)},${dy(-p.z_m)}`).join(" ") };
  }, [points]);
  const current = trajectoryPreviewPosition(points, time);
  if (!geometry) return null;
  const g = geometry;
  return <>
    <figure className="trj-plan"><figcaption><strong>平面轨迹</strong><span>X / Y 等比例 · 米</span></figcaption><svg viewBox="0 0 700 320" role="img" aria-label="XY 平面轨迹">
      {[0, 1, 2, 3, 4].map(i => { const x = 66 + i * 142, y = 28 + i * 59.5; return <g key={i}><path className="trj-grid" d={`M${x} 28V266 M66 ${y}H634`} /><text x={x} y="288" textAnchor="middle">{number(g.centerX + (x - 350) / g.scale)}</text><text x="57" y={y + 4} textAnchor="end">{number(g.centerY - (y - 147) / g.scale)}</text></g>; })}
      <path className="trj-axis" d="M66 28V266H634" /><text x="350" y="313" textAnchor="middle">X（m）</text><text x="14" y="145" transform="rotate(-90 14 145)" textAnchor="middle">Y（m）</text>
      <polyline className="trj-path" points={g.plan} /><circle className="trj-start" cx={g.x(points[0].x_m)} cy={g.y(points[0].y_m)} r="4" />
      {current && <circle className="trj-current" cx={g.x(current.x_m)} cy={g.y(current.y_m)} r="6" />}
    </svg></figure>
    <figure className="trj-depth"><figcaption><strong>深度随时间变化</strong><span>水下为正 · 米</span></figcaption><svg viewBox="0 0 700 186" role="img" aria-label="深度与时间曲线">
      {[0, 1, 2, 3, 4].map(i => <g key={i}><path className="trj-grid" d={`M${66 + i * 142} 28V138`} /><text x={66 + i * 142} y="159" textAnchor="middle">{number(g.start + (g.end - g.start) * i / 4)}</text></g>)}
      {[0, 1, 2].map(i => <g key={i}><path className="trj-grid" d={`M66 ${28 + i * 55}H634`} /><text x="57" y={32 + i * 55} textAnchor="end">{number(g.top + (g.bottom - g.top) * i / 2)}</text></g>)}
      <path className="trj-axis" d="M66 28V138H634" /><polyline className="trj-depth-path" points={g.depth} />
      {current && <circle className="trj-current" cx={g.tx(current.time_s)} cy={g.dy(-current.z_m)} r="5" />}<text x="350" y="182" textAnchor="middle">时间（s）</text>
    </svg></figure>
  </>;
}

export function TrajectoryAuthoringPage({ embedded = false, onChoose, initialPosition }: {
  embedded?: boolean;
  onChoose?: (asset: TrajectoryAssetDto, trackId: string) => void;
  initialPosition?: { x_meters: number; y_meters: number; z_meters: number };
} = {}) {
  const client = useQueryClient();
  const catalog = useQuery({ queryKey: ["trajectories"], queryFn: apiClient.listTrajectories });
  const [asset, setAsset] = useState<TrajectoryAssetDto | null>(null), [selectedTrack, setSelectedTrack] = useState("");
  const [filter, setFilter] = useState(""), [kind, setKind] = useState<Kind>("ConstantVelocity"), [name, setName] = useState("新建直线轨迹");
  const [duration, setDuration] = useState(60), [dt, setDt] = useState(1), [time, setTime] = useState<number | null>(null);
  const [trackId, setTrackId] = useState("N1"), [initial, setInitial] = useState<Triple>(initialPosition ? [initialPosition.x_meters, initialPosition.y_meters, -initialPosition.z_meters] : [0, 0, 20]), [velocity, setVelocity] = useState<Triple>([1, 0, 0]);
  const [waypoints, setWaypoints] = useState<Triple[]>([[30, 20, 30], [60, 0, 20]]), [controller, setController] = useState("WaypointPid"), [speed, setSpeed] = useState(1);
  const [direct, setDirect] = useState([100, 0, 0, 0, 8]), [csv, setCsv] = useState(""), [fileError, setFileError] = useState<string | null>(null);
  const shown = asset ?? (embedded ? null : catalog.data?.[0] ?? null);
  const track = shown?.tracks.find(t => t.track_id === selectedTrack) ?? shown?.tracks[0];
  const start = track?.points[0]?.time_s ?? 0, end = track?.points.at(-1)?.time_s ?? 0;
  const previewTime = time ?? start;
  const position = track ? trajectoryPreviewPosition(track.points, previewTime) : null;
  const invalid = !name.trim() ? "请填写便于辨认的轨迹名称。" : kind === "Csv" ? !csv.trim() ? "请选择 CSV 文件或粘贴轨迹数据。" : null
    : !/^[A-Za-z0-9_.-]{1,128}$/.test(trackId) ? "路径编号请使用字母、数字、短横线或下划线。"
    : !Number.isFinite(duration) || duration <= 0 || duration > 86400 ? "总时长须大于 0 且不超过 86400 秒。"
    : !Number.isFinite(dt) || dt <= 0 || dt > 60 ? "采样间隔须大于 0 且不超过 60 秒。"
    : Math.abs(Math.round(duration / dt) * dt - duration) > 1e-9 ? "总时长必须是采样间隔的整数倍，请调整其中一项。"
    : !initial.every(Number.isFinite) ? "初始位置必须是有效数字。"
    : kind === "ConstantVelocity" && !velocity.every(Number.isFinite) ? "速度必须是有效数字。"
    : (kind === "Waypoints" || (kind === "Uuv5Dof" && controller === "WaypointPid")) && (!waypoints.length || !waypoints.every(p => p.every(Number.isFinite))) ? "至少填写一个坐标完整的目标航路点。"
    : kind === "Uuv5Dof" && controller === "WaypointPid" && (!Number.isFinite(speed) || speed < 0 || speed > 20) ? "目标航速须在 0–20 m/s 之间。"
    : kind === "Uuv5Dof" && controller === "Direct" && !direct.every(Number.isFinite) ? "控制力与力矩必须是有效数字。" : null;
  const create = useMutation({
    mutationFn: () => {
      if (invalid) throw new Error(invalid);
      const identity = { asset_id: `trajectory-${crypto.randomUUID()}`, version: 1, name: name.trim() };
      return kind === "Csv" ? apiClient.importTrajectoryCsv({ ...identity, content: csv }) : apiClient.generateTrajectory({
        ...identity, track_id: trackId, kind, duration_s: duration, dt_s: dt,
        initial_x_m: initial[0], initial_y_m: initial[1], initial_z_m: -initial[2],
        ...(kind === "ConstantVelocity" ? { vx_mps: velocity[0], vy_mps: velocity[1], vz_mps: -velocity[2] } : {}),
        ...(kind === "Waypoints" || (kind === "Uuv5Dof" && controller === "WaypointPid") ? { waypoints: waypoints.map(([x, y, depth]) => [x, y, -depth]) } : {}),
        ...(kind === "Uuv5Dof" ? { controller, ...(controller === "WaypointPid" ? { target_speed_mps: speed } : { tau_u_n: direct[0], tau_v_n: direct[1], tau_w_n: direct[2], tau_q_nm: direct[3], tau_r_nm: direct[4] }) } : {}),
      });
    },
    onSuccess: value => { setAsset(value); setSelectedTrack(value.tracks[0].track_id); setTime(null); void client.invalidateQueries({ queryKey: ["trajectories"] }); },
  });
  const selectAsset = (value: TrajectoryAssetDto) => { setAsset(value); setSelectedTrack(value.tracks[0].track_id); setTime(null); };
  const selectKind = (value: Kind) => { setKind(value); create.reset(); if (value === "Uuv5Dof") setDt(.1); };
  const catalogItems = (catalog.data ?? []).filter(item => `${item.name} ${item.asset_id}`.toLowerCase().includes(filter.toLowerCase()));
  return <section className="trajectory-builder" aria-label="轨迹建设工作台">
    <header className="trj-header"><div><h1>{embedded ? "新增或选择轨迹" : "轨迹资源"}</h1><p>{embedded ? "生成后预览，再应用到当前节点；原有轨迹与实验不会被覆盖。" : "轨迹在实验工作台的节点面板内创建与绑定。"}</p></div>{!embedded && <Link className="trj-link" to="/workspace/experiment">进入实验工作台 →</Link>}</header>
    <div className="trj-layout">
      <aside className="trj-catalog" aria-label="已保存轨迹目录"><div className="trj-panel-title"><h2>已保存轨迹</h2><span>{catalog.data?.length ?? 0} 条</span></div><input type="search" aria-label="搜索轨迹" placeholder="搜索名称" value={filter} onChange={e => setFilter(e.target.value)} />
        <div className="trj-catalog-list">{catalog.isPending && <p>正在加载轨迹…</p>}{catalog.error && <ErrorState error={catalog.error} />}{!catalog.isPending && !catalog.error && !catalogItems.length && <p className="trj-hint">{filter ? "没有匹配的轨迹。" : "尚无轨迹。填写右侧参数并生成保存后，可在这里查看。"}</p>}
          {catalogItems.map(item => { const first = item.tracks[0]?.points[0], last = item.tracks[0]?.points.at(-1); return <button type="button" key={`${item.asset_id}:${item.version}`} className={shown?.asset_id === item.asset_id && shown?.version === item.version ? "selected" : ""} onClick={() => selectAsset(item)}><strong>{item.name}</strong><span>{sourceName(item.source)} · v{item.version}</span><small>{item.tracks.length} 条路径 · {number(first?.time_s ?? 0)}–{number(last?.time_s ?? 0)} s</small></button>; })}
        </div><p className="trj-catalog-foot">保存后不会被编辑覆盖。调整后生成新的轨迹，原实验仍引用原版本。</p>
      </aside>
      <section className="trj-view" aria-label="已保存轨迹预览"><div className="trj-panel-title"><h2>{shown?.name ?? "轨迹预览"}</h2>{shown && <span>已保存 · 只读</span>}</div>
        {track && shown ? <><div className="trj-view-toolbar"><label>路径<select aria-label="轨迹节点" value={track.track_id} onChange={e => { setSelectedTrack(e.target.value); setTime(null); }}>{shown.tracks.map(item => <option key={item.track_id}>{item.track_id}</option>)}</select></label><span>{track.points.length} 个记录点 · 有效时间 {number(start)}–{number(end)} s</span></div>
          <div className="trj-charts"><TrajectoryCharts points={track.points} time={previewTime} /></div>
          <div className="trj-scrubber"><label htmlFor="trj-time">位置预览</label><input id="trj-time" aria-label="轨迹时间" type="range" min={start} max={end} step="any" value={previewTime} onChange={e => setTime(Number(e.target.value))} /><output>t = {number(previewTime)} s</output></div>
          <dl className="trj-readout"><div><dt>X</dt><dd>{position ? number(position.x_m) : "—"} m</dd></div><div><dt>Y</dt><dd>{position ? number(position.y_m) : "—"} m</dd></div><div><dt>深度</dt><dd>{position ? number(-position.z_m) : "—"} m</dd></div></dl>
          <p className="trj-hint trj-view-note">滑块仅预览路径，不改变仿真。应用后可在场景中设置节点起点。</p>
          {onChoose && <button type="button" className="trj-primary" onClick={() => onChoose(shown, track.track_id)}>应用此轨迹到节点</button>}
          <details className="trj-resource-details"><summary>资源标识与来源</summary><dl><dt>资源编号 / 版本</dt><dd>{shown.asset_id} / {shown.version}</dd><dt>坐标</dt><dd>局部直角坐标，单位米；存储的 Z 轴向上为正。</dd><dt>来源</dt><dd>{sourceName(shown.source)}</dd><dt>校验值</dt><dd>{shown.checksum_sha256}</dd></dl>{shown.source_notice && <p>{shown.source_notice}</p>}</details>
        </> : <div className="trj-empty"><strong>{embedded ? "选择或生成要绑定的轨迹" : "这里显示真实生成的轨迹"}</strong><p>选择左侧已有轨迹，或填写右侧参数后点击“生成并保存”。</p>{!embedded && <p>未生成前不绘制示意运动，也不会用示例数据替代。</p>}</div>}
      </section>
      <form className="trj-controls" onSubmit={event => { event.preventDefault(); create.mutate(); }}>
        <div className="trj-panel-title"><h2>生成 / 导入</h2><span>保存为新轨迹</span></div>
        <fieldset disabled={create.isPending}><section className="trj-control-group"><label>名称<input maxLength={160} value={name} onChange={e => setName(e.target.value)} /></label><label>生成方式<select value={kind} onChange={e => selectKind(e.target.value as Kind)}>{kinds.map(([value, title]) => <option key={value} value={value}>{title}</option>)}</select></label></section>
          {kind === "Csv" ? <section className="trj-control-group"><h3>外部位置记录</h3><label>选择 CSV 文件<input type="file" accept=".csv,text/csv" onChange={async e => { const file = e.target.files?.[0]; if (!file) return; if (file.size > 16 * 1024 * 1024) { setFileError("文件不得超过 16 MiB。"); return; } try { setCsv(await file.text()); setFileError(null); } catch { setFileError("无法读取文件，请重新选择。"); } }} /></label><label>CSV 数据<textarea rows={9} value={csv} placeholder="track_id,time_s,x_m,y_m,z_m" onChange={e => setCsv(e.target.value)} /></label><p className="trj-hint">列为路径编号、时间（秒）、X/Y/Z（米）。Z 向上为正，水下 20 m 写作 -20；每条路径时间严格递增，所有路径须覆盖同一时段。</p>{fileError && <p role="alert" className="trj-validation">{fileError}</p>}</section> : <>
            <section className="trj-control-group"><h3>起点与有效时间</h3><Coordinates prefix="初始" labels={["X（m）", "Y（m）", "深度（m）"]} values={initial} onChange={setInitial} /><p className="trj-hint">这是 t = 0 时的位置。水下深度填正数；存储时转换为负的 Z 坐标。</p><div className="trj-two-fields"><label>总时长（s）<input type="number" min="0" max="86400" step="any" value={Number.isNaN(duration) ? "" : duration} onChange={e => setDuration(e.target.valueAsNumber)} /></label><label>{kind === "Uuv5Dof" ? "积分 / 采样间隔（s）" : "采样间隔（s）"}<input type="number" min="0" max="60" step="any" value={Number.isNaN(dt) ? "" : dt} onChange={e => setDt(e.target.valueAsNumber)} /></label></div><p className="trj-hint">每隔 {number(dt)} s 保存一个位置，覆盖 0–{number(duration)} s。{kind === "Uuv5Dof" ? "此间隔同时用于离线 RK4 积分。" : "间隔只决定轨迹记录密度。"}它不是仿真频率，不需要对齐 MAC 帧；仿真在任意事件时刻查询并插值。</p></section>
            {kind === "Fixed" && <section className="trj-control-group"><p>整个有效时段保持初始位置不变。静止轨迹同样有有效期，不会无限延长。</p></section>}
            {kind === "ConstantVelocity" && <section className="trj-control-group"><h3>直线速度</h3><Coordinates prefix="速度" labels={["X（m/s）", "Y（m/s）", "下潜（m/s）"]} values={velocity} onChange={setVelocity} /><p className="trj-hint">下潜速度为正表示变深，为负表示上浮；填 0 保持深度。这里只离线生成轨迹。</p></section>}
            {kind === "Uuv5Dof" && <section className="trj-control-group"><h3>水下航行器 · 专家设置</h3><label>控制方式<select value={controller} onChange={e => setController(e.target.value)}><option value="WaypointPid">航路点跟踪（航向 / 深度 / 航速闭环）</option><option value="Direct">直接设置控制力与力矩</option></select></label>{controller === "WaypointPid" ? <label>目标航速（m/s）<input type="number" step="any" min="0" max="20" value={speed} onChange={e => setSpeed(e.target.valueAsNumber)} /></label> : <div className="trj-two-fields">{["前向力（N）", "侧向力（N）", "下向力（N）", "俯仰力矩（N·m）", "偏航力矩（N·m）"].map((label, i) => <label key={label}>{label}<input type="number" step="any" value={direct[i]} onChange={e => setDirect(direct.map((v, index) => index === i ? e.target.valueAsNumber : v))} /></label>)}</div>}<p className="trj-hint">经典结构的简化五自由度演示模型，由 C++ 统一计算；不是论文模型完整复现，也不代表任意真实 UUV。小步长通常更利于数值稳定，但应根据实际结果检查。</p></section>}
            {(kind === "Waypoints" || (kind === "Uuv5Dof" && controller === "WaypointPid")) && <section className="trj-control-group"><div className="trj-group-heading"><h3>目标航路点</h3><button type="button" onClick={() => setWaypoints([...waypoints, [...(waypoints.at(-1) ?? initial)] as Triple])}>添加</button></div><div className="trj-waypoints">{waypoints.map((point, index) => <div className="trj-waypoint" key={index}><Coordinates prefix={`航路点${index + 1}`} labels={["X（m）", "Y（m）", "深度（m）"]} values={point} onChange={value => setWaypoints(waypoints.map((v, i) => index === i ? value : v))} /><button type="button" aria-label={`移除航路点${index + 1}`} onClick={() => setWaypoints(waypoints.filter((_, i) => i !== index))}>×</button></div>)}</div><p className="trj-hint">{kind === "Waypoints" ? "从初始位置依次经过以上位置，按各段距离分配总时长。拐弯处需较密采样才能清楚保留路径。" : "目标点供离线控制器跟踪，生成的实际轨迹不保证在指定时刻精确经过每个目标点。"}</p></section>}
            <details className="trj-control-group"><summary>路径编号</summary><label>编号<input value={trackId} onChange={e => setTrackId(e.target.value)} /></label><p className="trj-hint">用于在一个资源内区分路径，名称不要求与实验节点编号相同。</p></details>
          </>}
        </fieldset>
        <div className="trj-save"><p className="trj-hint">有效期须覆盖最后一次接收。越界明确报错，不自动续航或停在终点。</p>{invalid && <p className="trj-validation" role="status">{invalid}</p>}<button className="trj-primary" type="submit" disabled={Boolean(invalid) || create.isPending}>{create.isPending ? "正在生成并保存…" : kind === "Csv" ? "导入并保存" : "生成并保存"}</button>{create.isSuccess && <p className="trj-success" role="status">已保存“{create.data.name}”。{embedded ? "尚未绑定到节点：请确认预览后点击“应用此轨迹到节点”，然后在工作台保存实验。直接关闭不会修改节点绑定。" : "可前往实验工作台绑定。"}</p>}{create.error && <ErrorState error={create.error} />}</div>
      </form>
    </div>
  </section>;
}
