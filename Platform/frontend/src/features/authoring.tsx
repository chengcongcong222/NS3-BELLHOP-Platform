import { useEffect, useMemo, useState } from "react";
import { useMutation, useQuery, useQueryClient } from "@tanstack/react-query";
import { Link, useNavigate, useSearchParams } from "react-router-dom";
import { apiClient } from "../api/client";
import { queries } from "../api/queries";
import type {
  AuthoringDraftDto,
  DraftKind,
  EnvironmentBuildDto,
  ScenarioDto,
  TrajectoryAssetDto,
} from "../api/types";

export function TrajectoryAuthoringPage() {
  const client=useQueryClient();
  const catalog=useQuery({queryKey:["trajectories"],queryFn:apiClient.listTrajectories});
  const [asset,setAsset]=useState<TrajectoryAssetDto|null>(null);
  const [kind,setKind]=useState("ConstantVelocity"); const [name,setName]=useState("新建轨迹");
  const [duration,setDuration]=useState(60); const [dt,setDt]=useState(1); const [time,setTime]=useState(0);
  const [trackId,setTrackId]=useState("N1"); const [velocity,setVelocity]=useState("1,0.2,0");
  const [waypoints,setWaypoints]=useState("30,20,-30\n60,0,-20"); const [controller,setController]=useState("WaypointPid");
  const [direct,setDirect]=useState("100,0,0,0,8"); const [selectedTrack,setSelectedTrack]=useState("");
  const [csv,setCsv]=useState("track_id,time_s,x_m,y_m,z_m\nN1,0,0,0,-20\nN1,60,60,0,-20\n");
  const create=useMutation({mutationFn:()=>{const v=velocity.split(",").map(Number),tau=direct.split(",").map(Number),wps=waypoints.split("\n").filter(Boolean).map(row=>row.split(",").map(Number));return kind==="Csv"?apiClient.importTrajectoryCsv({asset_id:`trajectory-${Date.now()}`,version:1,name,content:csv}):apiClient.generateTrajectory({asset_id:`trajectory-${Date.now()}`,version:1,name,track_id:trackId,kind,duration_s:duration,dt_s:dt,initial_x_m:0,initial_y_m:0,initial_z_m:-20,vx_mps:v[0],vy_mps:v[1],vz_mps:v[2],waypoints:wps,controller,target_speed_mps:1,tau_u_n:tau[0],tau_v_n:tau[1],tau_w_n:tau[2],tau_q_nm:tau[3],tau_r_nm:tau[4]});},onSuccess:(value)=>{setAsset(value);setSelectedTrack(value.tracks[0].track_id);setTime(0);client.invalidateQueries({queryKey:["trajectories"]});}});
  const shown=asset??catalog.data?.[0]??null; const track=shown?.tracks.find(x=>x.track_id===(selectedTrack||shown.tracks[0].track_id))??shown?.tracks[0];
  const nearest=track?.points.reduce((a,b)=>Math.abs(b.time_s-time)<Math.abs(a.time_s-time)?b:a,track.points[0]);
  const bounds=track?{minX:Math.min(...track.points.map(p=>p.x_m)),maxX:Math.max(...track.points.map(p=>p.x_m)),minY:Math.min(...track.points.map(p=>p.y_m)),maxY:Math.max(...track.points.map(p=>p.y_m))}:null;
  const map=(v:number,min:number,max:number,size:number)=>20+(size-40)*(max===min?0.5:(v-min)/(max-min));
  return <><PageHeader title="轨迹建设" detail="生成或导入离线轨迹资源；Runtime 只查询正式 Asset，不执行运动模型。"/><div className="trajectory-workspace"><aside className="trajectory-catalog"><div className="studio-pane-title"><span>轨迹资源</span><b>{catalog.data?.length??0}</b></div>{catalog.isLoading&&<LoadingState/>}{catalog.data?.map(x=><button className={shown?.asset_id===x.asset_id?"active":""} onClick={()=>{setAsset(x);setSelectedTrack(x.tracks[0].track_id);setTime(0)}} key={`${x.asset_id}:${x.version}`}><strong>{x.name}</strong><span>{x.asset_id} · v{x.version}</span><small>{x.tracks.length} tracks · {x.source}</small></button>)}</aside><section className="trajectory-canvas"><div className="studio-pane-title"><span>轨迹预览</span>{shown&&<select aria-label="轨迹节点" value={track?.track_id} onChange={e=>{setSelectedTrack(e.target.value);setTime(0)}}>{shown.tracks.map(x=><option key={x.track_id}>{x.track_id}</option>)}</select>}<b>{shown?.coordinate_system??"LocalCartesianMetersZUp"}</b></div>{track&&bounds?<><svg viewBox="0 0 720 390" aria-label="XY 平面轨迹"><path className="trajectory-grid" d="M20 20V370H700M20 195H700M360 20V370"/><polyline className="trajectory-line" points={track.points.map(p=>`${map(p.x_m,bounds.minX,bounds.maxX,720)},${390-map(p.y_m,bounds.minY,bounds.maxY,390)}`).join(" ")}/>{nearest&&<circle className="trajectory-current" cx={map(nearest.x_m,bounds.minX,bounds.maxX,720)} cy={390-map(nearest.y_m,bounds.minY,bounds.maxY,390)} r="7"/>}</svg><div className="depth-strip"><span>深度 / 时间</span><svg viewBox="0 0 720 110"><polyline className="trajectory-depth" points={track.points.map(p=>`${map(p.time_s,track.points[0].time_s,track.points.at(-1)!.time_s,720)},${map(p.z_m,Math.max(...track.points.map(q=>q.z_m)),Math.min(...track.points.map(q=>q.z_m)),110)}`).join(" ")}/></svg></div><input aria-label="轨迹时间" type="range" min={track.points[0].time_s} max={track.points.at(-1)!.time_s} step={dt} value={time} onChange={e=>setTime(Number(e.target.value))}/><div className="trajectory-readout"><b>t = {nearest?.time_s.toFixed(1)} s</b><span>x {nearest?.x_m.toFixed(2)} m</span><span>y {nearest?.y_m.toFixed(2)} m</span><span>z {nearest?.z_m.toFixed(2)} m</span></div></>:<EmptyState>生成或选择一条正式轨迹。</EmptyState>}</section><aside className="trajectory-controls"><div className="studio-pane-title"><span>生成 / 导入</span></div><label>名称<input value={name} onChange={e=>setName(e.target.value)}/></label><label>来源<select value={kind} onChange={e=>setKind(e.target.value)}><option value="Fixed">静止</option><option value="ConstantVelocity">匀速直线</option><option value="Waypoints">航路点</option><option value="Uuv5Dof">UUV 5-DOF</option><option value="Csv">CSV 导入</option></select></label>{kind==="Csv"?<label>CSV（s / m / z-up）<textarea rows={12} value={csv} onChange={e=>setCsv(e.target.value)}/></label>:<><label>Track ID<input value={trackId} onChange={e=>setTrackId(e.target.value)}/></label><label>时长（s）<input type="number" value={duration} onChange={e=>setDuration(Number(e.target.value))}/></label><label>固定步长（s）<input type="number" value={dt} onChange={e=>setDt(Number(e.target.value))}/></label>{kind==="ConstantVelocity"&&<label>速度 vx,vy,vz（m/s）<input value={velocity} onChange={e=>setVelocity(e.target.value)}/></label>}{(kind==="Waypoints"||kind==="Uuv5Dof")&&<label>航路点 x,y,z（每行一个）<textarea rows={4} value={waypoints} onChange={e=>setWaypoints(e.target.value)}/></label>}{kind==="Uuv5Dof"&&<><label>控制方式<select value={controller} onChange={e=>setController(e.target.value)}><option value="WaypointPid">航向/深度/速度 PID</option><option value="Direct">直接 τ 输入</option></select></label>{controller==="Direct"&&<label>τu,τv,τw,τq,τr<input value={direct} onChange={e=>setDirect(e.target.value)}/></label>}<div className="data-boundary"><strong>Demo UUV 参数</strong><p>五自由度 Plant 与 Controller 分离，固定步长 RK4。</p><p>演示参数，不代表任意真实 UUV；NED 在输出边界转换为平台 z-up。</p></div></>}</>}<button className="primary-button" disabled={create.isPending} onClick={()=>create.mutate()}>{create.isPending?"生成中…":"生成并保存正式 Asset"}</button>{create.error&&<ErrorState error={create.error}/>}</aside></div></>;
}
import { DraftEnvironmentProfile } from "../components/EnvironmentProfile";
import { ScenarioDesigner } from "../components/ScenarioDesigner";
import {
  EmptyState,
  ErrorState,
  Field,
  LoadingState,
  PageHeader,
  Status,
} from "../components/common";
import type {
  EnvironmentDraft,
  ExperimentDraft,
  ScenarioDraft,
} from "../domain/workspace";

const now = () => new Date().toISOString();
const environmentInitial: EnvironmentDraft = {
  kind: "environment",
  id: "",
  name: "未命名水声环境",
  description: "",
  provenance: { kind: "new", label: "新建" },
  maximumDepthMeters: 200,
  maximumRangeMeters: 10000,
  centerFrequencyHz: 25000,
  soundSpeedProfile: [],
  bathymetry: [],
  constructionMode: "BellhopOffline",
  updatedAt: now(),
};
interface EnvironmentBuildParameters {
  bottomSoundSpeed: number;
  bottomDensity: number;
  bottomAttenuation: number;
  beamCount: number;
  minimumAngle: number;
  maximumAngle: number;
}
const defaultBuildParameters: EnvironmentBuildParameters = {
  bottomSoundSpeed: 1700, bottomDensity: 1.8,
  bottomAttenuation: 0.5, beamCount: 501,
  minimumAngle: -30, maximumAngle: 30,
};
const toEnvironmentPayload = (value: EnvironmentDraft, parameters = defaultBuildParameters) => ({
  description: value.description,
  maximum_depth_meters: value.maximumDepthMeters,
  maximum_range_meters: value.maximumRangeMeters,
  center_frequency_hz: value.centerFrequencyHz,
  sound_speed_profile: value.soundSpeedProfile.map((point) => ({
    depth_meters: point.depthMeters,
    speed_meters_per_second: point.speedMetersPerSecond,
  })),
  bathymetry: value.bathymetry.map((point) => ({
    range_meters: point.rangeMeters,
    depth_meters: point.depthMeters,
  })),
  construction_mode: "BellhopOffline",
  build_source: "reference-bellhop-arrivals",
  bottom: {
    sound_speed_meters_per_second: parameters.bottomSoundSpeed,
    density_grams_per_cubic_centimeter: parameters.bottomDensity,
    attenuation_db_per_wavelength: parameters.bottomAttenuation,
  },
  bellhop: {
    beam_count: parameters.beamCount,
    minimum_launch_angle_degrees: parameters.minimumAngle,
    maximum_launch_angle_degrees: parameters.maximumAngle,
    output: "Arrivals",
  },
});
const fromEnvironmentDraft = (draft: AuthoringDraftDto): EnvironmentDraft => {
  const p = draft.payload as any;
  return {
    ...environmentInitial,
    id: draft.draft_id,
    name: draft.name,
    description: String(p.description ?? ""),
    maximumDepthMeters: Number(p.maximum_depth_meters ?? 200),
    maximumRangeMeters: Number(p.maximum_range_meters ?? 10000),
    centerFrequencyHz: Number(p.center_frequency_hz ?? 25000),
    soundSpeedProfile: Array.isArray(p.sound_speed_profile)
      ? p.sound_speed_profile.map((x: any) => ({
          depthMeters: Number(x.depth_meters),
          speedMetersPerSecond: Number(x.speed_meters_per_second),
        }))
      : [],
    bathymetry: Array.isArray(p.bathymetry)
      ? p.bathymetry.map((x: any) => ({
          rangeMeters: Number(x.range_meters),
          depthMeters: Number(x.depth_meters),
        }))
      : [],
    updatedAt: draft.updated_at,
  };
};

function StepRail({ current }: { current: number }) {
  return (
    <ol className="build-step-rail">
      {[
        "基本信息",
        "水文来源",
        "地形来源",
        "数据预览",
        "Bellhop 配置",
        "构建与发布",
      ].map((label, index) => (
        <li className={index + 1 <= current ? "active" : ""} key={label}>
          <b>{index + 1}</b>
          <span>{label}</span>
        </li>
      ))}
    </ol>
  );
}

function SourceCard({
  selected,
  title,
  detail,
  onClick,
}: {
  selected: boolean;
  title: string;
  detail: string;
  onClick: () => void;
}) {
  return (
    <button
      type="button"
      className={`source-card ${selected ? "selected" : ""}`}
      onClick={onClick}
    >
      <strong>{title}</strong>
      <span>{detail}</span>
      {selected && <i>✓</i>}
    </button>
  );
}

export function EnvironmentAuthoringPage() {
  const [params] = useSearchParams();
  const drafts = useQuery(queries.drafts("environment"));
  const client = useQueryClient();
  const [value, setValue] = useState(environmentInitial);
  const [serverDraft, setServerDraft] = useState<AuthoringDraftDto | null>(
    null,
  );
  const [step, setStep] = useState(1);
  const [hydrologySource, setHydrologySource] = useState<
    "woa" | "file" | "manual"
  >("woa");
  const [terrainSource, setTerrainSource] = useState<
    "gebco" | "file" | "manual"
  >("gebco");
  const [build, setBuild] = useState<EnvironmentBuildDto | null>(null);
  const [notice, setNotice] = useState("");
  const [buildParameters, setBuildParameters] = useState(defaultBuildParameters);
  useEffect(() => {
    const selected = drafts.data?.find(
      (item) => item.draft_id === params.get("draft"),
    );
    if (selected && selected.draft_id !== serverDraft?.draft_id) {
      setServerDraft(selected);
      setValue(fromEnvironmentDraft(selected));
    }
  }, [drafts.data, params, serverDraft?.draft_id]);
  const save = useMutation({
    mutationFn: async () =>
      serverDraft
        ? apiClient.updateDraft(
            serverDraft,
            value.name,
            toEnvironmentPayload(value, buildParameters),
          )
        : apiClient.createDraft(
            "environment",
            value.name,
            toEnvironmentPayload(value, buildParameters),
          ),
    onSuccess: async (draft) => {
      setServerDraft(draft);
      setValue(fromEnvironmentDraft(draft));
      setNotice("草稿已保存到服务器工作区");
      await client.invalidateQueries({ queryKey: ["authoring-drafts"] });
    },
  });
  const woa = useMutation({
    mutationFn: () =>
      apiClient.acquireWoa({
        latitude_deg: 18,
        longitude_deg: 150,
        month: 4,
        maximum_depth_meters: value.maximumDepthMeters,
        use_reference_cache: true,
      }),
    onSuccess: (result) => {
      const points = result.sound_speed_profile as Array<any>;
      setValue((current) => ({
        ...current,
        soundSpeedProfile: points.map((x) => ({
          depthMeters: Number(x.depth_meters),
          speedMetersPerSecond: Number(x.speed_meters_per_second),
        })),
      }));
      setNotice(`已从 ${result.provider} 获取 ${points.length} 个水文采样点`);
      setStep(3);
    },
  });
  const terrain = useMutation({
    mutationFn: () =>
      apiClient.acquireTerrain({
        latitude_deg: 18,
        longitude_deg: 150,
        bearing_deg: 90,
        maximum_range_meters: value.maximumRangeMeters,
        sample_count: 41,
        use_reference_cache: true,
      }),
    onSuccess: (result) => {
      const points = result.bathymetry as Array<any>;
      setValue((current) => ({
        ...current,
        bathymetry: points.map((x) => ({
          rangeMeters: Number(x.range_meters),
          depthMeters: Number(x.depth_meters),
        })),
      }));
      setNotice(`已从 ${result.provider} 获取 ${points.length} 个地形采样点`);
      setStep(4);
    },
  });
  const importFile = async (file: File, kind: "ssp" | "terrain") => {
    const result =
      kind === "ssp"
        ? await apiClient.importSsp(file.name, await file.text())
        : await apiClient.importTerrain(file.name, await file.text());
    if (kind === "ssp") {
      const points = result.sound_speed_profile as Array<any>;
      setValue({
        ...value,
        soundSpeedProfile: points.map((x) => ({
          depthMeters: Number(x.depth_meters),
          speedMetersPerSecond: Number(x.speed_meters_per_second),
        })),
      });
    } else {
      const points = result.bathymetry as Array<any>;
      setValue({
        ...value,
        bathymetry: points.map((x) => ({
          rangeMeters: Number(x.range_meters),
          depthMeters: Number(x.depth_meters),
        })),
      });
    }
    setNotice(`${file.name} 已解析并通过基础检查`);
  };
  const startBuild = useMutation({
    mutationFn: async () => {
      const saved = serverDraft
        ? await apiClient.updateDraft(
            serverDraft,
            value.name,
            toEnvironmentPayload(value, buildParameters),
          )
        : await apiClient.createDraft(
            "environment",
            value.name,
            toEnvironmentPayload(value, buildParameters),
          );
      setServerDraft(saved);
      return apiClient.createEnvironmentBuild(saved.draft_id);
    },
    onSuccess: (result) => {
      setBuild(result);
      setStep(6);
    },
  });
  const publish = useMutation({
    mutationFn: () => apiClient.publishEnvironmentBuild(build!.build_id),
    onSuccess: () => setNotice("构建产物已在 Authoring Workspace 中不可变固定；当前不会进入 Runtime catalog"),
  });
  if (drafts.isPending) return <LoadingState />;
  if (drafts.error) return <ErrorState error={drafts.error} />;
  const inputReady =
    value.soundSpeedProfile.length >= 2 && value.bathymetry.length >= 2;
  return (
    <div className="environment-builder classic-environment-page">
      <section className="environment-classic-heading">
        <div><p className="eyebrow">Bellhop 环境管理</p><h1>环境建设</h1><span>在同一工作区准备 SSP、海底地形与传播参数，再由正式 Build Job 固定产物。</span></div>
        <div className="environment-classic-state"><b>{drafts.data?.length ?? 0}</b><span>服务器草稿</span><small>{serverDraft ? `当前 revision ${serverDraft.revision}` : "当前未保存"}</small></div>
      </section>
      <section className="workspace-toolbar environment-classic-toolbar">
        <div>
          <small>AUTHORING PLANE · 环境建设</small>
          <h1>{value.name}</h1>
        </div>
        <label>
          环境名称
          <input
            value={value.name}
            onChange={(event) =>
              setValue({ ...value, name: event.target.value })
            }
          />
        </label>
        <button
          type="button"
          onClick={() => save.mutate()}
          disabled={save.isPending}
        >
          保存草稿
        </button>
      </section>
      <div className="builder-layout">
        <aside>
          <StepRail current={step} />
          <div className="plane-note">
            <strong>离线建设</strong>
            <span>
              Bellhop 只在环境 Build Job 中运行，不进入 Runtime 热路径。
            </span>
          </div>
        </aside>
        <main className="builder-main">
          <section className="builder-section">
            <div className="section-number">01</div>
            <div>
              <h2>定义海域与用途</h2>
              <p>先描述环境范围，再选择真实水文和地形来源。</p>
              <div className="form-grid">
                <label>
                  说明
                  <textarea
                    rows={2}
                    value={value.description}
                    onChange={(e) =>
                      setValue({ ...value, description: e.target.value })
                    }
                  />
                </label>
                <label>
                  最大水深（m）
                  <input
                    type="number"
                    value={value.maximumDepthMeters}
                    onChange={(e) =>
                      setValue({
                        ...value,
                        maximumDepthMeters: Number(e.target.value),
                      })
                    }
                  />
                </label>
                <label>
                  最大距离（m）
                  <input
                    type="number"
                    value={value.maximumRangeMeters}
                    onChange={(e) =>
                      setValue({
                        ...value,
                        maximumRangeMeters: Number(e.target.value),
                      })
                    }
                  />
                </label>
              </div>
            </div>
          </section>
          <section className="builder-section">
            <div className="section-number">02</div>
            <div>
              <h2>选择水文来源</h2>
              <div className="source-grid">
                <SourceCard
                  selected={hydrologySource === "woa"}
                  title="WOA 海洋气候数据"
                  detail="按经纬度与月份获取温盐并换算 SSP"
                  onClick={() => setHydrologySource("woa")}
                />
                <SourceCard
                  selected={hydrologySource === "file"}
                  title="导入水文文件"
                  detail="CSV / TXT / JSON，支持 SSP 与 CTD 转换后的数据"
                  onClick={() => setHydrologySource("file")}
                />
                <SourceCard
                  selected={hydrologySource === "manual"}
                  title="高级手工编辑"
                  detail="仅用于检查与专家修订"
                  onClick={() => setHydrologySource("manual")}
                />
              </div>
              {hydrologySource === "woa" && (
                <button
                  type="button"
                  onClick={() => woa.mutate()}
                  disabled={woa.isPending}
                >
                  {woa.isPending ? "正在获取…" : "获取 WOA 数据"}
                </button>
              )}
              {hydrologySource === "file" && (
                <label className="file-button">
                  选择水文文件
                  <input
                    type="file"
                    accept=".csv,.txt,.json"
                    onChange={(e) =>
                      e.target.files?.[0] &&
                      void importFile(e.target.files[0], "ssp")
                    }
                  />
                </label>
              )}
            </div>
          </section>
          <section className="builder-section">
            <div className="section-number">03</div>
            <div>
              <h2>选择海底地形来源</h2>
              <div className="source-grid">
                <SourceCard
                  selected={terrainSource === "gebco"}
                  title="GEBCO 全球地形"
                  detail="沿指定方位采样真实海底剖面"
                  onClick={() => setTerrainSource("gebco")}
                />
                <SourceCard
                  selected={terrainSource === "file"}
                  title="导入地形文件"
                  detail="CSV / TXT / JSON 地形剖面"
                  onClick={() => setTerrainSource("file")}
                />
                <SourceCard
                  selected={terrainSource === "manual"}
                  title="高级手工编辑"
                  detail="用于局部修订或受控测试"
                  onClick={() => setTerrainSource("manual")}
                />
              </div>
              {terrainSource === "gebco" && (
                <button
                  type="button"
                  onClick={() => terrain.mutate()}
                  disabled={terrain.isPending}
                >
                  {terrain.isPending ? "正在获取…" : "获取 GEBCO 地形"}
                </button>
              )}
              {terrainSource === "file" && (
                <label className="file-button">
                  选择地形文件
                  <input
                    type="file"
                    accept=".csv,.txt,.json"
                    onChange={(e) =>
                      e.target.files?.[0] &&
                      void importFile(e.target.files[0], "terrain")
                    }
                  />
                </label>
              )}
            </div>
          </section>
          <section className="builder-section preview-section">
            <div className="section-number">04</div>
            <div>
              <h2>数据预览与检查</h2>
              <div className="environment-preview">
                <DraftEnvironmentProfile draft={value} />
              </div>
              <div className="preview-facts">
                <span>
                  <b>{value.soundSpeedProfile.length}</b> SSP 点
                </span>
                <span>
                  <b>{value.bathymetry.length}</b> 地形点
                </span>
                <span>
                  <b>{value.maximumDepthMeters} m</b> 深度
                </span>
                <span>
                  <b>{value.maximumRangeMeters / 1000} km</b> 距离
                </span>
              </div>
              <details>
                <summary>高级数据表</summary>
                <div className="advanced-data">
                  <textarea
                    aria-label="SSP 高级编辑"
                    rows={8}
                    value={value.soundSpeedProfile
                      .map((x) => `${x.depthMeters}, ${x.speedMetersPerSecond}`)
                      .join("\n")}
                    readOnly
                  />
                  <textarea
                    aria-label="地形高级编辑"
                    rows={8}
                    value={value.bathymetry
                      .map((x) => `${x.rangeMeters}, ${x.depthMeters}`)
                      .join("\n")}
                    readOnly
                  />
                </div>
              </details>
            </div>
          </section>
          <section className="builder-section">
            <div className="section-number">05</div>
            <div>
              <h2>Bellhop 离线构建</h2>
              <div className="form-grid">
                <label>
                  工作频率（Hz）
                  <input
                    type="number"
                    value={value.centerFrequencyHz}
                    onChange={(e) =>
                      setValue({
                        ...value,
                        centerFrequencyHz: Number(e.target.value),
                      })
                    }
                  />
                </label>
                <label>
                  计算模式
                  <select>
                    <option>Arrivals · 多径到达</option>
                  </select>
                </label>
                <label>
                  采样质量
                  <select>
                    <option>正式构建</option>
                    <option>快速预览</option>
                  </select>
                </label>
                <label>海底声速（m/s）<input type="number" value={buildParameters.bottomSoundSpeed} onChange={(event) => setBuildParameters({...buildParameters, bottomSoundSpeed: Number(event.target.value)})} /></label>
                <label>海底密度（g/cm³）<input type="number" step="0.1" value={buildParameters.bottomDensity} onChange={(event) => setBuildParameters({...buildParameters, bottomDensity: Number(event.target.value)})} /></label>
                <label>海底衰减（dB/λ）<input type="number" step="0.1" value={buildParameters.bottomAttenuation} onChange={(event) => setBuildParameters({...buildParameters, bottomAttenuation: Number(event.target.value)})} /></label>
                <label>波束数<input type="number" min="1" value={buildParameters.beamCount} onChange={(event) => setBuildParameters({...buildParameters, beamCount: Number(event.target.value)})} /></label>
                <label>最小发射角（°）<input type="number" value={buildParameters.minimumAngle} onChange={(event) => setBuildParameters({...buildParameters, minimumAngle: Number(event.target.value)})} /></label>
                <label>最大发射角（°）<input type="number" value={buildParameters.maximumAngle} onChange={(event) => setBuildParameters({...buildParameters, maximumAngle: Number(event.target.value)})} /></label>
              </div>
              <p>这些底质与 Bellhop 参数随服务器草稿保存；当前参考构建使用已验证 arrivals，尚不冒充任意新环境的正式 Runtime asset。</p>
              <button
                type="button"
                disabled={!inputReady || startBuild.isPending}
                onClick={() => startBuild.mutate()}
              >
                {startBuild.isPending
                  ? "正在执行 Build Job…"
                  : "开始离线声场构建"}
              </button>
            </div>
          </section>
          {build && (
            <section className="build-console">
              <div className="panel-title">
                <div>
                  <small>ENVIRONMENT BUILD JOB</small>
                  <h2>{build.build_id}</h2>
                </div>
                <Status value={build.state} />
              </div>
              <div className="build-pipeline">
                {build.steps.map((item) => (
                  <div className={item.state.toLowerCase()} key={item.key}>
                    <i>
                      {item.state === "Completed"
                        ? "✓"
                        : item.state === "Failed"
                          ? "!"
                          : item.state === "Running"
                            ? "●"
                            : "○"}
                    </i>
                    <span>{item.label}</span>
                    <small>{item.state}</small>
                  </div>
                ))}
              </div>
              {build.failure && (
                <ErrorState error={new Error(build.failure.message)} />
              )}
              {build.state === "Completed" && (
                <div className="publish-ready">
                  <div>
                    <strong>参考环境构建已完成</strong>
                    <span>水文、地形和参考 Bellhop arrivals 已完成校验；尚未注册到 Runtime catalog。</span>
                  </div>
                  <button
                    type="button"
                    onClick={() => publish.mutate()}
                    disabled={publish.isPending}
                  >
                    固定工作区构建产物
                  </button>
                </div>
              )}
            </section>
          )}
          {(notice ||
            save.error ||
            woa.error ||
            terrain.error ||
            startBuild.error ||
            publish.error) && (
            <div className="authoring-notice">
              {notice}
              {(save.error ||
                woa.error ||
                terrain.error ||
                startBuild.error ||
                publish.error) && (
                <ErrorState
                  error={
                    save.error ||
                    woa.error ||
                    terrain.error ||
                    startBuild.error ||
                    publish.error
                  }
                />
              )}
            </div>
          )}
        </main>
      </div>
    </div>
  );
}

const scenarioInitial = (
  scenarios: ScenarioDto[],
  environmentId: string,
): ScenarioDraft =>
  scenarios[0]
    ? {
        kind: "scenario",
        id: "",
        name: `${scenarios[0].name} 派生场景`,
        environmentAssetId: scenarios[0].environment.environment_asset_id,
        fusionCenterNodeId: scenarios[0].fusion_center_node_id,
        provenance: { kind: "new", label: "新建" },
        nodes: structuredClone(scenarios[0].nodes),
        updatedAt: now(),
      }
    : {
        kind: "scenario",
        id: "",
        name: "未命名节点场景",
        environmentAssetId: environmentId,
        fusionCenterNodeId: "0",
        provenance: { kind: "new", label: "新建" },
        nodes: [
          {
            node_id: "0",
            can_transmit: true,
            can_receive: true,
            duplex_mode: "HalfDuplex",
            initial_position: { x_meters: 0, y_meters: 0, z_meters: -20 },
            initial_velocity: {
              x_meters_per_second: 0,
              y_meters_per_second: 0,
              z_meters_per_second: 0,
            },
          },
        ],
        updatedAt: now(),
      };

export function ScenarioAuthoringPage() {
  const environments = useQuery(queries.environments());
  const trajectories = useQuery(queries.trajectories());
  const scenarios = useQuery(queries.scenarios());
  const drafts = useQuery(queries.drafts("scenario"));
  const [params] = useSearchParams();
  const client = useQueryClient();
  const [value, setValue] = useState<ScenarioDraft | null>(null);
  const [server, setServer] = useState<AuthoringDraftDto | null>(null);
  useEffect(() => {
    if (!value && environments.data && scenarios.data)
      setValue(
        scenarioInitial(
          scenarios.data,
          environments.data[0]?.environment_asset_id ?? "",
        ),
      );
  }, [environments.data, scenarios.data, value]);
  useEffect(() => {
    const selected = drafts.data?.find(
      (x) => x.draft_id === params.get("draft"),
    );
    if (selected && selected.draft_id !== server?.draft_id) {
      const p = selected.payload as any;
      setServer(selected);
      setValue({
        kind: "scenario",
        id: selected.draft_id,
        name: selected.name,
        environmentAssetId: String(p.environment_asset_id ?? ""),
        fusionCenterNodeId: String(p.fusion_center_node_id ?? "0"),
        nodes: p.nodes ?? [],
        trajectoryBindings: p.trajectory_bindings ?? [],
        provenance: { kind: "new", label: "新建" },
        updatedAt: selected.updated_at,
      });
    }
  }, [drafts.data, params, server?.draft_id]);
  const save = useMutation({
    mutationFn: () => {
      const payload = {
        environment_asset_id: value!.environmentAssetId,
        fusion_center_node_id: value!.fusionCenterNodeId,
        nodes: value!.nodes,
        trajectory_bindings: value!.trajectoryBindings ?? [],
      };
      return server
        ? apiClient.updateDraft(server, value!.name, payload)
        : apiClient.createDraft("scenario", value!.name, payload);
    },
    onSuccess: async (draft) => {
      setServer(draft);
      await client.invalidateQueries({ queryKey: ["authoring-drafts"] });
    },
  });
  if (
    !value ||
    environments.isPending || trajectories.isPending ||
    scenarios.isPending ||
    drafts.isPending
  )
    return <LoadingState />;
  const error = environments.error || trajectories.error || scenarios.error || drafts.error;
  if (error) return <ErrorState error={error} />;
  const env = environments.data.find(
    (x) => x.environment_asset_id === value.environmentAssetId,
  );
  return (
    <div className="scenario-workbench-page classic-scenario-page">
      <section className="workspace-toolbar studio-topbar classic-scenario-toolbar">
        <div>
          <small>AUTHORING PLANE · SCENARIO STUDIO</small>
          <h1>{value.name}</h1>
        </div>
        <label>
          场景名称
          <input
            value={value.name}
            onChange={(e) => setValue({ ...value, name: e.target.value })}
          />
        </label>
        <label>
          环境
          <select
            value={value.environmentAssetId}
            onChange={(e) =>
              setValue({ ...value, environmentAssetId: e.target.value })
            }
          >
            {environments.data.map((x) => (
              <option key={x.environment_asset_id}>
                {x.environment_asset_id}
              </option>
            ))}
          </select>
        </label>
        <span className="trajectory-binding-state">轨迹绑定：正式 Asset / version / track</span>
        <button onClick={() => save.mutate()} disabled={save.isPending}>
          保存场景
        </button>
      </section>
      <ScenarioDesigner
        draft={value}
        onChange={setValue}
        maximumDepthMeters={env?.axes.receiver_depth.maximum}
        maximumRangeMeters={env?.axes.horizontal_range.maximum}
      />
      <section className="panel compact-form"><h2>节点轨迹绑定</h2>
        {value.nodes.map((node) => { const current=value.trajectoryBindings?.find(x=>x.node_id===node.node_id); return <label key={node.node_id}>N{node.node_id}
          <select value={current?`${current.asset_id}:${current.version}:${current.track_id}`:""} onChange={e=>{const [asset_id,version,track_id]=e.target.value.split(":");setValue({...value,trajectoryBindings:[...(value.trajectoryBindings??[]).filter(x=>x.node_id!==node.node_id),{node_id:node.node_id,asset_id,version:Number(version),track_id}]})}}>
            <option value="">选择正式轨迹</option>{trajectories.data?.flatMap(asset=>asset.tracks.map(track=><option key={`${asset.asset_id}:${asset.version}:${track.track_id}`} value={`${asset.asset_id}:${asset.version}:${track.track_id}`}>{asset.name} · v{asset.version} · {track.track_id}</option>))}
          </select></label>})}
      </section>
      <div className="draft-actions">
        <span>
          {server ? `服务器草稿 · revision ${server.revision}` : "尚未保存"}
        </span>
        <button onClick={() => save.mutate()}>保存到 Workspace</button>
      </div>
    </div>
  );
}

const experimentInitial: ExperimentDraft = {
  kind: "experiment",
  id: "",
  name: "未命名网络实验",
  sourceExperimentId: null,
  scenarioId: "",
  provenance: { kind: "new", label: "新建" },
  routingMode: "DirectToFusionCenter",
  macMode: "Tdma",
  bitRateBitsPerSecond: "60",
  centerFrequencyHz: 25000,
  occupiedBandwidthHz: 4000,
  sourceLevelDb: 190,
  guardIntervalNs: "2000000000",
  simulationCycleCount: "2",
  deterministicSeed: "1",
  networkUpdateIntervalCycles: "10",
  updatedAt: now(),
};
export function ExperimentAuthoringPage() {
  const scenarios = useQuery(queries.scenarios());
  const scenarioDrafts = useQuery(queries.drafts("scenario"));
  const client = useQueryClient();
  const navigate = useNavigate();
  const [value, setValue] = useState(experimentInitial);
  const [server, setServer] = useState<AuthoringDraftDto | null>(null);
  const [physicalLayer, setPhysicalLayer] = useState<"Channel1Abstract" | "Channel2Waveform">("Channel1Abstract");
  const [waveformModulation, setWaveformModulation] = useState<"Bpsk" | "Bfsk">("Bpsk");
  const [waveformNoise, setWaveformNoise] = useState<"None" | "Awgn">("None");
  const [waveformNoisePsd, setWaveformNoisePsd] = useState(0);
  const set = <K extends keyof ExperimentDraft>(
    key: K,
    val: ExperimentDraft[K],
  ) => setValue({ ...value, [key]: val });
  useEffect(() => {
    if (!value.scenarioId && scenarioDrafts.data?.[0])
      set("scenarioId", scenarioDrafts.data[0].draft_id);
  }, [scenarioDrafts.data]);
  const payload = {
    scenario_id: value.scenarioId,
    routing_mode: value.routingMode,
    mac_mode: value.macMode,
    bit_rate_bits_per_second: value.bitRateBitsPerSecond,
    center_frequency_hz: value.centerFrequencyHz,
    occupied_bandwidth_hz: value.occupiedBandwidthHz,
    source_level_db: value.sourceLevelDb,
    guard_interval_ns: value.guardIntervalNs,
    simulation_cycle_count: value.simulationCycleCount,
    deterministic_seed: value.deterministicSeed,
    network_update_interval_cycles: value.networkUpdateIntervalCycles,
    physical_layer: physicalLayer,
    waveform_modulation: waveformModulation,
    waveform_noise: waveformNoise,
    waveform_noise_psd_db_re_1upa2_per_hz: waveformNoisePsd,
  };
  const save = useMutation({
    mutationFn: () =>
      server
        ? apiClient.updateDraft(server, value.name, payload)
        : apiClient.createDraft("experiment", value.name, payload),
    onSuccess: async (draft) => {
      setServer(draft);
      await client.invalidateQueries({ queryKey: ["authoring-drafts"] });
    },
  });
  const run = useMutation({
    mutationFn: () => apiClient.createAuthoringRun(server!.draft_id),
    onSuccess: async (created) => {
      await client.invalidateQueries({ queryKey: ["runs"] });
      navigate(`/runs/${created.run_id}`);
    },
  });
  if (scenarios.isPending || scenarioDrafts.isPending) return <LoadingState />;
  if (scenarios.error || scenarioDrafts.error) return <ErrorState error={scenarios.error || scenarioDrafts.error} />;
  return (
    <>
      <div className="classic-experiment-heading"><div><p className="eyebrow">仿真工作台</p><h1>配置网络实验</h1></div><p>沿用成熟的分组配置工作流，只开放当前 Runtime 已接入的协议能力。</p></div>
      <div className="experiment-workspace classic-experiment-workspace">
        <aside className="step-rail">
          <a href="#basic">01 基础信息</a>
          <a href="#network">02 网络与协议</a>
          <a href="#phy">03 通信与 PHY</a>
          <a href="#execution">04 执行设置</a>
        </aside>
        <div>
          <section id="basic" className="panel form-panel">
            <h2>基础信息</h2>
            <label>
              实验名称
              <input
                value={value.name}
                onChange={(e) => set("name", e.target.value)}
              />
            </label>
            <label>
              场景版本
              <select
                value={value.scenarioId}
                onChange={(e) => set("scenarioId", e.target.value)}
              >
                {scenarioDrafts.data.map((x) => (
                  <option value={x.draft_id} key={x.draft_id}>
                    {x.name} · authored revision {x.revision}
                  </option>
                ))}
                {scenarios.data.map((x) => (
                  <option disabled
                    value={x.scenario_id}
                    key={`${x.scenario_id}:${x.version}`}
                  >
                    {x.name} · v{x.version}
                  </option>
                ))}
              </select>
            </label>
          </section>
          <section id="network" className="panel form-panel">
            <h2>网络与协议</h2>
            <div className="form-grid">
              <label>
                路由
                <select
                  disabled
                  value={value.routingMode}
                  onChange={(e) => set("routingMode", e.target.value)}
                >
                  <option value="DirectToFusionCenter">直接到融合中心</option>
                  <option disabled>AODV（运行内核暂未接入）</option>
                  <option disabled>OLSR（运行内核暂未接入）</option>
                </select>
              </label>
              <label>
                MAC
                <select
                  disabled
                  value={value.macMode}
                  onChange={(e) => set("macMode", e.target.value)}
                >
                  <option value="Tdma">TDMA</option>
                  <option disabled>ALOHA（运行内核暂未接入）</option>
                  <option disabled>CSMA（运行内核暂未接入）</option>
                </select>
              </label>
              <label>
                网络更新周期
                <input
                  disabled
                  value={value.networkUpdateIntervalCycles}
                  onChange={(e) =>
                    set("networkUpdateIntervalCycles", e.target.value)
                  }
                />
              </label>
            </div>
          </section>
          <section id="phy" className="panel form-panel">
            <h2>通信与物理层</h2>
            <div className="form-grid">
              <label>
                物理层实现
                <select aria-label="物理层实现" value={physicalLayer} onChange={(event) => setPhysicalLayer(event.target.value as "Channel1Abstract" | "Channel2Waveform")}>
                  <option value="Channel1Abstract">Channel1 · 快速抽象物理层</option>
                  <option value="Channel2Waveform">Channel2 · 波形级物理层</option>
                </select>
              </label>
              <label>
                通信速率（bit/s）
                <input
                  disabled
                  value={value.bitRateBitsPerSecond}
                  onChange={(e) => set("bitRateBitsPerSecond", e.target.value)}
                />
              </label>
              <label>
                中心频率 Hz
                <input
                  type="number"
                  disabled
                  value={value.centerFrequencyHz}
                  onChange={(e) =>
                    set("centerFrequencyHz", Number(e.target.value))
                  }
                />
              </label>
              <label>
                带宽 Hz
                <input
                  type="number"
                  disabled
                  value={value.occupiedBandwidthHz}
                  onChange={(e) =>
                    set("occupiedBandwidthHz", Number(e.target.value))
                  }
                />
              </label>
              <label>
                声源级 dB
                <input
                  type="number"
                  disabled
                  value={value.sourceLevelDb}
                  onChange={(e) => set("sourceLevelDb", Number(e.target.value))}
                />
              </label>
              <label>
                保护时间 ns
                <input
                  disabled
                  value={value.guardIntervalNs}
                  onChange={(e) => set("guardIntervalNs", e.target.value)}
                />
              </label>
            </div>
            {physicalLayer === "Channel2Waveform" && <div className="waveform-config"><label>调制方式<select value={waveformModulation} onChange={(event) => setWaveformModulation(event.target.value as "Bpsk" | "Bfsk")}><option value="Bpsk">BPSK</option><option value="Bfsk">BFSK</option></select></label><label>波形噪声<select value={waveformNoise} onChange={(event) => setWaveformNoise(event.target.value as "None" | "Awgn")}><option value="None">关闭</option><option value="Awgn">AWGN</option></select></label>{waveformNoise === "Awgn" && <label>噪声 PSD（dB re 1 μPa²/Hz）<input type="number" value={waveformNoisePsd} onChange={(event) => setWaveformNoisePsd(Number(event.target.value))} /></label>}<p>该配置随 StartRunCommand 进入 worker，并整体选择 Waveform PHY；不是界面开关。</p></div>}
            <p className="runtime-authority-note">当前运行内核只开放既有 Acceptance preset 的路由、TDMA、速率、频率、带宽与声源级，因此这些参数只读；不支持的值不会假保存。</p>
          </section>
          <section id="execution" className="panel form-panel">
            <h2>执行设置</h2>
            <div className="form-grid">
              <label>
                仿真周期
                <input
                  value={value.simulationCycleCount}
                  onChange={(e) => set("simulationCycleCount", e.target.value)}
                />
              </label>
              <label>
                确定性 seed
                <input
                  value={value.deterministicSeed}
                  onChange={(e) => set("deterministicSeed", e.target.value)}
                />
              </label>
            </div>
            <button onClick={() => save.mutate()} disabled={save.isPending}>
              保存实验草稿
            </button>
            {server && (
              <p className="save-toast">
                已保存到服务器 Workspace · revision {server.revision}
              </p>
            )}
            {server && <button className="primary-action" onClick={() => run.mutate()} disabled={run.isPending}>{run.isPending ? "正在创建正式运行…" : `使用 ${physicalLayer === "Channel2Waveform" ? "Channel2" : "Channel1"} 运行已保存草稿`}</button>}
            {run.error && <ErrorState error={run.error} />}
          </section>
        </div>
      </div>
    </>
  );
}

export function ServerWorkspacePage() {
  const drafts = useQuery(queries.drafts());
  const client = useQueryClient();
  const remove = useMutation({
    mutationFn: apiClient.deleteDraft,
    onSuccess: () =>
      client.invalidateQueries({ queryKey: ["authoring-drafts"] }),
  });
  if (drafts.isPending) return <LoadingState />;
  if (drafts.error) return <ErrorState error={drafts.error} />;
  const route = (draft: AuthoringDraftDto) =>
    `/workspace/${draft.kind}?draft=${draft.draft_id}`;
  const label: Record<DraftKind, string> = {
    environment: "声学环境",
    scenario: "节点场景",
    experiment: "网络实验",
  };
  return (
    <>
      <PageHeader
        title="Workspace 资源管理"
        detail="正式草稿保存在后端项目工作区；浏览器只保存界面偏好。"
      />
      <div className="catalog-actions">
        <Link className="button-link" to="/workspace/environment">
          新建环境
        </Link>
        <Link className="button-link" to="/workspace/scenario">
          新建场景
        </Link>
        <Link className="button-link" to="/workspace/experiment">
          新建实验
        </Link>
      </div>
      <section className="panel">
        {drafts.data.length ? (
          <table>
            <thead>
              <tr>
                <th>类型</th>
                <th>名称</th>
                <th>校验</th>
                <th>版本</th>
                <th>更新时间</th>
                <th>操作</th>
              </tr>
            </thead>
            <tbody>
              {drafts.data.map((draft) => (
                <tr key={draft.draft_id}>
                  <td>{label[draft.kind]}</td>
                  <td>
                    <Link to={route(draft)}>{draft.name}</Link>
                  </td>
                  <td>
                    {draft.validation_issues.length
                      ? `${draft.validation_issues.length} 项待处理`
                      : "✓ 通过"}
                  </td>
                  <td>r{draft.revision}</td>
                  <td>{new Date(draft.updated_at).toLocaleString()}</td>
                  <td>
                    <button
                      className="text-button"
                      onClick={() => remove.mutate(draft)}
                    >
                      删除
                    </button>
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        ) : (
          <EmptyState>服务器工作区暂无草稿。</EmptyState>
        )}
      </section>
    </>
  );
}
