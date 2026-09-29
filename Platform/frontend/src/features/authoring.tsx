import { useEffect, useMemo, useRef, useState } from "react";
import { useMutation, useQuery, useQueries, useQueryClient } from "@tanstack/react-query";
import { Link, useNavigate, useSearchParams } from "react-router-dom";
import { apiClient } from "../api/client";
import type { AcquisitionProgress } from "../api/acquisition";
import { queries } from "../api/queries";
import type {
  AuthoringDraftDto,
  DraftKind,
  EnvironmentBuildDto,
  ScenarioDto,
} from "../api/types";

export { TrajectoryAuthoringPage } from "./trajectoryBuilder";
import { DraftEnvironmentProfile } from "../components/EnvironmentProfile";
import { ScenarioDesigner } from "../components/ScenarioDesigner";
import { SceneDomainEditor } from "../components/SceneDomainEditor";
import { isSceneDomain } from "../domain/sceneSpatial";
import { trajectoryRunCoverage } from "../domain/trajectoryCoverage";
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
  sourceDepths: string;
  receiverDepths: string;
  rangeSampleCount: number;
}
const defaultBuildParameters: EnvironmentBuildParameters = {
  bottomSoundSpeed: 1700, bottomDensity: 1.8,
  bottomAttenuation: 0.5, beamCount: 501,
  minimumAngle: -30, maximumAngle: 30,
  sourceDepths: "8, 20, 40, 60, 75", receiverDepths: "8, 20, 40, 60, 75", rangeSampleCount: 26,
};
const acquisitionInitial = { latitudeDeg: 18, longitudeDeg: 110, month: 4, requestedSspDepthMeters: 200,
  bearingDeg: 90, requestedRangeMeters: 10000, terrainSamples: 41, useReferenceCache: false };
type AcquisitionParameters = typeof acquisitionInitial;
const sourceQuery = (p: AcquisitionParameters, kind: "ssp" | "terrain") => kind === "ssp"
  ? { latitude_deg: p.latitudeDeg, longitude_deg: p.longitudeDeg, month: p.month, maximum_depth_meters: p.requestedSspDepthMeters, use_reference_cache: p.useReferenceCache }
  : { latitude_deg: p.latitudeDeg, longitude_deg: p.longitudeDeg, bearing_deg: p.bearingDeg, maximum_range_meters: p.requestedRangeMeters, sample_count: p.terrainSamples, use_reference_cache: p.useReferenceCache };
const toEnvironmentPayload = (value: EnvironmentDraft, parameters = defaultBuildParameters, acquisition = acquisitionInitial, sources: Record<string, any> = {}) => ({
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
  build_source: "authored-bellhop-inputs",
  acquisition,
  sources,
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
    source_depths_meters: parameters.sourceDepths.split(",").map((part) => Number(part.trim())),
    receiver_depths_meters: parameters.receiverDepths.split(",").map((part) => Number(part.trim())),
    range_sample_count: parameters.rangeSampleCount,
  },
});
const fromEnvironmentDraft = (draft: AuthoringDraftDto): EnvironmentDraft => {
  const p = draft.payload as any;
  return {
    ...environmentInitial,
    id: draft.draft_id,
    name: draft.name,
    description: String(p.description ?? ""),
    maximumDepthMeters: Array.isArray(p.bathymetry) && p.bathymetry.length ? Math.max(...p.bathymetry.map((x: any) => Number(x.depth_meters))) : 0,
    maximumRangeMeters: Array.isArray(p.bathymetry) && p.bathymetry.length ? Math.max(...p.bathymetry.map((x: any) => Number(x.range_meters))) : 0,
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

function StepRail({ current, select }: { current: number; select: (step: number) => void }) {
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
        <li className={index + 1 === current ? "active" : ""} key={label}>
          <button type="button" aria-current={index + 1 === current ? "step" : undefined} onClick={() => select(index + 1)}><b>{index + 1}</b><span>{label}</span></button>
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
  const [step, setStep] = useState(2);
  const sourceId = params.get("source") ?? "";
  const source = useQuery({ queryKey: ["environment-inputs", sourceId], queryFn: () => apiClient.environmentInputs(sourceId), enabled: !!sourceId });
  const loadedSource = useRef("");
  const [hydrologySource, setHydrologySource] = useState<
    "woa" | "file"
  >("woa");
  const [terrainSource, setTerrainSource] = useState<
    "gebco" | "file"
  >("gebco");
  const [build, setBuild] = useState<EnvironmentBuildDto | null>(null);
  const [builtInput, setBuiltInput] = useState<string | null>(null);
  const [notice, setNotice] = useState("");
  const [buildParameters, setBuildParameters] = useState(defaultBuildParameters);
  const [acquisition, setAcquisition] = useState(acquisitionInitial);
  const [sources, setSources] = useState<Record<string, any>>({});
  const acquisitionRef = useRef(acquisition); acquisitionRef.current = acquisition;
  const woaAbort = useRef<AbortController | null>(null);
  const [woaProgress, setWoaProgress] = useState<AcquisitionProgress | null>(null);
  const [woaElapsed, setWoaElapsed] = useState(0);
  useEffect(() => () => woaAbort.current?.abort(), []);
  const capabilities = useQuery({ queryKey: ["environment-capabilities"], queryFn: () => apiClient.environmentCapabilities() });
  useEffect(() => {
    let selected = drafts.data?.find(
      (item) => item.draft_id === params.get("draft"),
    );
    if (!selected && source.data && sourceId && loadedSource.current !== sourceId) {
      loadedSource.current = sourceId;
      setServerDraft(null);
      selected = { kind: "environment", draft_id: "", name: String(source.data.name), payload: source.data.payload as Record<string, unknown>, revision: 0, created_at: "", updated_at: "", validation_issues: [] };
      setNotice((source.data.limitations as string[]).join("；"));
    }
    if (selected && selected.draft_id !== serverDraft?.draft_id) {
      if (selected.draft_id) setServerDraft(selected);
      setValue(fromEnvironmentDraft(selected));
      const p = selected.payload as any;
      setAcquisition({ ...acquisitionInitial, ...p.acquisition });
      setSources(p.sources ?? {});
      setBuildParameters({
        bottomSoundSpeed: p.bottom?.sound_speed_meters_per_second ?? defaultBuildParameters.bottomSoundSpeed,
        bottomDensity: p.bottom?.density_grams_per_cubic_centimeter ?? defaultBuildParameters.bottomDensity,
        bottomAttenuation: p.bottom?.attenuation_db_per_wavelength ?? defaultBuildParameters.bottomAttenuation,
        beamCount: p.bellhop?.beam_count ?? defaultBuildParameters.beamCount,
        minimumAngle: p.bellhop?.minimum_launch_angle_degrees ?? defaultBuildParameters.minimumAngle,
        maximumAngle: p.bellhop?.maximum_launch_angle_degrees ?? defaultBuildParameters.maximumAngle,
        sourceDepths: p.bellhop?.source_depths_meters?.join(", ") ?? defaultBuildParameters.sourceDepths,
        receiverDepths: p.bellhop?.receiver_depths_meters?.join(", ") ?? defaultBuildParameters.receiverDepths,
        rangeSampleCount: p.bellhop?.range_sample_count ?? defaultBuildParameters.rangeSampleCount,
      });
    }
  }, [drafts.data, params, serverDraft?.draft_id, source.data, sourceId]);
  const save = useMutation({
    mutationFn: async () =>
      serverDraft
        ? apiClient.updateDraft(
            serverDraft,
            value.name,
            toEnvironmentPayload(value, buildParameters, acquisition, sources),
          )
        : apiClient.createDraft(
            "environment",
            value.name,
            toEnvironmentPayload(value, buildParameters, acquisition, sources),
          ),
    onSuccess: async (draft) => {
      setServerDraft(draft);
      setValue(fromEnvironmentDraft(draft));
      setNotice("草稿已保存到服务器工作区");
      await client.invalidateQueries({ queryKey: ["authoring-drafts"] });
    },
  });
  const woa = useMutation({
    onError: () => setWoaProgress(null),
    mutationFn: async (request: Record<string, unknown>) => {
      const controller = new AbortController();
      woaAbort.current = controller;
      setWoaElapsed(0);
      setWoaProgress({ stage: "connecting", message: "正在连接水文服务…" });
      const started = Date.now();
      const interval = window.setInterval(() => setWoaElapsed(Math.floor((Date.now() - started) / 1000)), 1000);
      const deadline = window.setTimeout(() => controller.abort("timeout"), 1_260_000);
      try {
        return await apiClient.acquireWoaProgress(request, setWoaProgress, controller.signal);
      } catch (error) {
        if (controller.signal.aborted) throw new Error(controller.signal.reason === "timeout"
          ? "下载等待超时，请检查网络后重试。不会改用参考数据。"
          : "已取消获取；现有剖面未替换，已完成的月度下载仍可复用。");
        throw error instanceof TypeError ? new Error("无法连接水文服务，请检查后端是否启动及网络连接。") : error;
      } finally {
        window.clearInterval(interval); window.clearTimeout(deadline); woaAbort.current = null;
      }
    },
    onSuccess: (result, request) => {
      if (JSON.stringify(request) !== JSON.stringify(sourceQuery(acquisitionRef.current, "ssp"))) {
        setWoaProgress({ stage: "stale", message: "查询参数已改变，旧请求结果未应用。请重新获取当前海域。" });
        return;
      }
      const { sound_speed_profile: _, ...metadata } = result;
      setSources(current => ({ ...current, ssp: { ...metadata, request } }));
      const points = result.sound_speed_profile as Array<any>;
      const availableDepth = Math.max(...points.map((x) => Number(x.depth_meters)));
      setValue((current) => ({
        ...current,
        maximumDepthMeters: current.bathymetry.length ? Math.max(...current.bathymetry.map((x) => x.depthMeters)) : availableDepth,
        soundSpeedProfile: points.map((x) => ({
          depthMeters: Number(x.depth_meters),
          speedMetersPerSecond: Number(x.speed_meters_per_second),
        })),
      }));
      setNotice(`已从 ${result.provider} 获取 ${points.length} 个水文采样点`);
      setWoaProgress({ stage: "completed", message: `已获取 ${points.length} 个声速采样点${result.cache_files_reused === 2 ? "，使用已缓存的 NOAA 月度数据" : ""}` });
      setStep(3);
    },
  });
  const terrain = useMutation({
    mutationFn: (request: Record<string, unknown>) => apiClient.acquireTerrain(request),
    onSuccess: (result, request) => {
      if (JSON.stringify(request) !== JSON.stringify(sourceQuery(acquisitionRef.current, "terrain"))) return;
      const { bathymetry: _, ...metadata } = result;
      setSources(current => ({ ...current, terrain: { ...metadata, request } }));
      const points = result.bathymetry as Array<any>;
      setValue((current) => ({
        ...current,
        maximumDepthMeters: Math.max(...points.map((x) => Number(x.depth_meters))),
        maximumRangeMeters: Math.max(...points.map((x) => Number(x.range_meters))),
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
    setSources(current => ({ ...current, [kind]: { source: "file", filename: file.name } }));
    if (kind === "ssp") {
      const points = result.sound_speed_profile as Array<any>;
      setValue({
        ...value,
        maximumDepthMeters: value.bathymetry.length ? Math.max(...value.bathymetry.map(x => x.depthMeters)) : Math.max(...points.map(x => Number(x.depth_meters))),
        soundSpeedProfile: points.map((x) => ({
          depthMeters: Number(x.depth_meters),
          speedMetersPerSecond: Number(x.speed_meters_per_second),
        })),
      });
    } else {
      const points = result.bathymetry as Array<any>;
      setValue({
        ...value,
        maximumDepthMeters: Math.max(...points.map(x => Number(x.depth_meters))),
        maximumRangeMeters: Math.max(...points.map(x => Number(x.range_meters))),
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
      setBuiltInput(JSON.stringify(toEnvironmentPayload(value, buildParameters, acquisition, sources)));
      const saved = serverDraft
        ? await apiClient.updateDraft(
            serverDraft,
            value.name,
            toEnvironmentPayload(value, buildParameters, acquisition, sources),
          )
        : await apiClient.createDraft(
            "environment",
            value.name,
            toEnvironmentPayload(value, buildParameters, acquisition, sources),
          );
      setServerDraft(saved);
      return apiClient.createEnvironmentBuild(saved.draft_id, saved.revision);
    },
    onSuccess: (result) => {
      setBuild(result);
      setStep(6);
    },
  });
  const publish = useMutation({
    mutationFn: () => apiClient.publishEnvironmentBuild(build!.build_id),
    onSuccess: async (artifact) => {
      setNotice(`正式环境 ${String(artifact.environment_asset_id ?? "")} 已发布并可被新 Run 精确引用；发布版本不可变。`);
      await client.invalidateQueries({ queryKey: ["environments"] });
    },
  });
  if (drafts.error || source.error) return <ErrorState error={drafts.error || source.error} />;
  if (drafts.isPending || (sourceId && source.isPending)) return <LoadingState />;
  const sspMaximumDepth = value.soundSpeedProfile.length ? Math.max(...value.soundSpeedProfile.map((point) => point.depthMeters)) : 0;
  const terrainMaximumDepth = value.bathymetry.length ? Math.max(...value.bathymetry.map((point) => point.depthMeters)) : 0;
  const terrainMaximumRange = value.bathymetry.length ? Math.max(...value.bathymetry.map((point) => point.rangeMeters)) : 0;
  const hasEnvironmentSamples = value.soundSpeedProfile.length >= 2 && value.bathymetry.length >= 2;
  const profileCoversTerrain = hasEnvironmentSamples && sspMaximumDepth >= terrainMaximumDepth;
  const staleSources = (["ssp", "terrain"] as const).filter(kind => sources[kind]?.request && JSON.stringify(sources[kind].request) !== JSON.stringify(sourceQuery(acquisition, kind)));
  const inputReady = hasEnvironmentSamples && profileCoversTerrain && !staleSources.length;
  const matchesBuild = builtInput === JSON.stringify(toEnvironmentPayload(value, buildParameters, acquisition, sources));
  return (
    <div className="environment-builder classic-environment-page focused-environment" data-step={step}>
      <section className="environment-classic-heading">
        <div><p className="eyebrow">Bellhop 环境管理</p><h1>环境建设</h1><span>在同一工作区准备 SSP、海底地形与传播参数，再由正式 Build Job 固定产物。</span></div>
        <div className="environment-classic-state"><b>{drafts.data?.length ?? 0}</b><span>服务器草稿</span><small>{serverDraft ? `当前 revision ${serverDraft.revision}` : "当前未保存"}</small></div>
      </section>
      <section className="workspace-toolbar environment-classic-toolbar">
        <div>
          <small>{sourceId ? "已发布环境的输入 · 修改后另存新版本" : "环境建设 · 服务器草稿"}</small>
          <Link to="/environments">← 环境目录</Link>
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
          disabled={save.isPending || startBuild.isPending}
        >
          保存草稿
        </button>
      </section>
      <div className="builder-layout">
        <aside>
          <StepRail current={step} select={setStep} />
          <div className="plane-note">
            <strong>离线建设</strong>
            <span>
              先取得水文与地形，再计算声场并发布。实验绑定发布版本，不随草稿修改。
            </span>
          </div>
        </aside>
        <main className="builder-main">
          <section className="builder-section">
            <div className="section-number">01</div>
            <div>
              <h2>定义海域与用途</h2>
              <p>海域坐标用于 WOA 与 GEBCO 查询；最终水深和距离由返回的数据覆盖范围确定。</p>
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
                  数据确定的最大水深（m）
                  <input
                    type="number"
                    readOnly
                    value={value.maximumDepthMeters}
                  />
                </label>
                <label>
                  地形覆盖距离（m）
                  <input
                    type="number"
                    readOnly
                    value={value.maximumRangeMeters}
                  />
                </label>
              </div>
            </div>
          </section>
          <section className="builder-section">
            <div className="section-number">02</div>
            <div>
              <h2>选择水文来源</h2>
              <div className="source-grid two-sources">
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
              </div>
              {hydrologySource === "woa" && (
                <div className="acquisition-form"><div className="form-grid">
                  <label>纬度（°）<input disabled={acquisition.useReferenceCache} type="number" min="-90" max="90" value={acquisition.latitudeDeg} onChange={(e) => setAcquisition({ ...acquisition, latitudeDeg: Number(e.target.value) })} /></label>
                  <label>经度（°）<input disabled={acquisition.useReferenceCache} type="number" min="-180" max="180" value={acquisition.longitudeDeg} onChange={(e) => setAcquisition({ ...acquisition, longitudeDeg: Number(e.target.value) })} /></label>
                  <label>月份<input disabled={acquisition.useReferenceCache} type="number" min="1" max="12" value={acquisition.month} onChange={(e) => setAcquisition({ ...acquisition, month: Number(e.target.value) })} /></label>
                  <label>请求 SSP 深度上限（m）<input type="number" min="1" value={acquisition.requestedSspDepthMeters} onChange={(e) => setAcquisition({ ...acquisition, requestedSspDepthMeters: Number(e.target.value) })} /></label>
                </div><label>数据来源<select value={acquisition.useReferenceCache ? "reference" : "online"} onChange={(e) => setAcquisition(e.target.value === "reference" ? { ...acquisitionInitial, useReferenceCache: true } : { ...acquisition, useReferenceCache: false })}><option value="online">在线查询所选海域</option><option value="reference">内置参考案例：北纬18°、东经110°、4月</option></select></label>
                {!acquisition.useReferenceCache && capabilities.data?.woa_online_available === false && <p role="status">{String(capabilities.data.woa_unavailable_reason)}</p>}
                <div className="acquisition-actions"><button type="button" onClick={() => woa.mutate(sourceQuery(acquisition, "ssp"))} disabled={woa.isPending || (!acquisition.useReferenceCache && capabilities.data?.woa_online_available === false)}>{woa.isPending ? "正在获取…" : acquisition.useReferenceCache ? "载入参考声速剖面" : "获取 WOA 数据"}</button>{woa.isPending && <button type="button" onClick={() => woaAbort.current?.abort()}>取消获取</button>}</div>
                {!acquisition.useReferenceCache && <p className="acquisition-hint">首次获取某个月份需下载温度、盐度数据，可能需要数分钟。下载后，更换经纬度会直接使用该月缓存提取剖面。</p>}
                {woaProgress && <div className="acquisition-progress" role="status" aria-live="polite"><strong>{woaProgress.message}</strong>{woa.isPending && <span>已等待 {woaElapsed} 秒{woaProgress.dataset ? ` · ${woaProgress.dataset}（${woaProgress.file_index}/${woaProgress.file_count}）` : ""}</span>}{woa.isPending && woaProgress.received_bytes !== undefined && <><span>{(woaProgress.received_bytes / 1048576).toFixed(1)} MB{woaProgress.total_bytes ? ` / ${(woaProgress.total_bytes / 1048576).toFixed(1)} MB` : " · 总大小由服务端决定"}</span><progress aria-label="WOA 文件下载进度" max={woaProgress.total_bytes ?? undefined} value={woaProgress.total_bytes ? woaProgress.received_bytes : undefined} /></>}</div>}
                {woa.error && <p className="acquisition-error" role="alert">{woa.error.message}</p>}
                </div>
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
              <div className="source-grid two-sources">
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
              </div>
              {terrainSource === "gebco" && (
                <div className="acquisition-form"><div className="form-grid">
                  <label>起点纬度（°）<input disabled={acquisition.useReferenceCache} type="number" min="-90" max="90" value={acquisition.latitudeDeg} onChange={(e) => setAcquisition({ ...acquisition, latitudeDeg: Number(e.target.value) })} /></label>
                  <label>起点经度（°）<input disabled={acquisition.useReferenceCache} type="number" min="-180" max="180" value={acquisition.longitudeDeg} onChange={(e) => setAcquisition({ ...acquisition, longitudeDeg: Number(e.target.value) })} /></label>
                  <label>剖面方位（°）<input disabled={acquisition.useReferenceCache} type="number" value={acquisition.bearingDeg} onChange={(e) => setAcquisition({ ...acquisition, bearingDeg: Number(e.target.value) })} /></label>
                  <label>请求剖面距离（m）<input disabled={acquisition.useReferenceCache} type="number" min="1" value={acquisition.requestedRangeMeters} onChange={(e) => setAcquisition({ ...acquisition, requestedRangeMeters: Number(e.target.value) })} /></label>
                  <label>采样点数<input disabled={acquisition.useReferenceCache} type="number" min="2" max="100" value={acquisition.terrainSamples} onChange={(e) => setAcquisition({ ...acquisition, terrainSamples: Number(e.target.value) })} /></label>
                </div><button type="button" onClick={() => terrain.mutate(sourceQuery(acquisition, "terrain"))} disabled={terrain.isPending}>{terrain.isPending ? "正在获取…" : acquisition.useReferenceCache ? "载入参考地形" : "获取 GEBCO 地形"}</button></div>
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
              {staleSources.length > 0 && <p role="alert">采集参数已改变，请重新获取{staleSources.map(kind => kind === "ssp" ? "声速剖面" : "地形").join("、")}。旧数据仅供查看，不可据此构建。</p>}
              {sources.ssp?.grid_coordinate && <p>实际水文采样位置：纬度 {sources.ssp.grid_coordinate.latitude_deg}°、经度 {sources.ssp.grid_coordinate.longitude_deg}° · {sources.ssp.month ? `${sources.ssp.month}月统计` : "年平均"}<br /><small>当前使用 WOA 1° 网格；邻近坐标可能落入同一格点，得到相同月平均剖面。</small></p>}
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
              <div className={`coverage-check ${profileCoversTerrain ? "valid" : "invalid"}`}><strong>数据覆盖检查</strong><span>SSP 至 {sspMaximumDepth} m · 地形最深 {terrainMaximumDepth} m · 距离 {terrainMaximumRange} m</span><p>{!hasEnvironmentSamples ? "请先获取 SSP 与地形数据；每组至少需要两个有效采样点。" : profileCoversTerrain ? "SSP 已覆盖当前地形水深，可进入 Bellhop 构建。" : "SSP 未覆盖地形最深处。系统不会静默外推声速剖面；请重新获取更深的 WOA 数据或导入完整 SSP。"}</p></div>
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
                    <option>多径到达结果</option>
                  </select>
                </label>
                <label>发送深度采样（m，逗号分隔）<input value={buildParameters.sourceDepths} onChange={e => setBuildParameters({ ...buildParameters, sourceDepths: e.target.value })} /></label>
                <label>接收深度采样（m，逗号分隔）<input value={buildParameters.receiverDepths} onChange={e => setBuildParameters({ ...buildParameters, receiverDepths: e.target.value })} /></label>
                <label>距离采样数<input type="number" min="2" max="1000" value={buildParameters.rangeSampleCount} onChange={e => setBuildParameters({ ...buildParameters, rangeSampleCount: Number(e.target.value) })} /></label>
                {capabilities.data?.bellhop_available === false && <p role="status">本机未配置 Bellhop 离线计算程序。可以保存草稿，但不会用参考声场代替计算。</p>}
                <label>海底声速（m/s）<input type="number" value={buildParameters.bottomSoundSpeed} onChange={(event) => setBuildParameters({...buildParameters, bottomSoundSpeed: Number(event.target.value)})} /></label>
                <label>海底密度（g/cm³）<input type="number" step="0.1" value={buildParameters.bottomDensity} onChange={(event) => setBuildParameters({...buildParameters, bottomDensity: Number(event.target.value)})} /></label>
                <label>海底衰减（dB/λ）<input type="number" step="0.1" value={buildParameters.bottomAttenuation} onChange={(event) => setBuildParameters({...buildParameters, bottomAttenuation: Number(event.target.value)})} /></label>
                <label>波束数<input type="number" min="1" value={buildParameters.beamCount} onChange={(event) => setBuildParameters({...buildParameters, beamCount: Number(event.target.value)})} /></label>
                <label>最小发射角（°）<input type="number" value={buildParameters.minimumAngle} onChange={(event) => setBuildParameters({...buildParameters, minimumAngle: Number(event.target.value)})} /></label>
                <label>最大发射角（°）<input type="number" value={buildParameters.maximumAngle} onChange={(event) => setBuildParameters({...buildParameters, maximumAngle: Number(event.target.value)})} /></label>
              </div>
              <p>按当前声速、地形和底质重新计算声场。声速在采样点之间线性插值，不外推。发布后可在实验中选择。</p>
              <button
                type="button"
                disabled={!inputReady || startBuild.isPending || save.isPending || capabilities.data?.bellhop_available === false}
                onClick={() => startBuild.mutate()}
              >
                {startBuild.isPending
                  ? "正在计算声场…"
                  : "开始离线声场构建"}
              </button>
            </div>
          </section>
          {build && (
            <section className="build-console">
              <div className="panel-title">
                <div>
                  <small>声场计算记录</small>
                  <h2>计算与发布</h2>
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
                    <Status value={item.state} />
                  </div>
                ))}
              </div>
              {build.failure && (
                <ErrorState error={new Error(build.failure.message)} />
              )}
              {build.state === "Completed" && (
                <div className="publish-ready">
                  <div>
                    <strong>当前环境计算已完成</strong>
                    <span>{matchesBuild ? "发布时将完整校验声场并保存本次输入快照；已有运行使用的版本不会改变。" : "输入已修改。请按当前参数重新计算，不能把旧计算结果作为当前环境发布。"}</span>
                  </div>
                  <button
                    type="button"
                    onClick={() => publish.mutate()}
                    disabled={publish.isPending || !matchesBuild}
                  >
                    发布为正式环境
                  </button>
                </div>
              )}
            </section>
          )}
          {!build && step === 6 && <section className="build-console"><h2>计算与发布</h2><p>当前尚未计算声场。请检查采样覆盖和 Bellhop 参数后开始计算。</p><button onClick={() => setStep(5)}>设置计算参数</button></section>}
          {(notice ||
            save.error ||
            terrain.error ||
            startBuild.error ||
            publish.error) && (
            <div className="authoring-notice">
              {notice}
              {(save.error ||
                terrain.error ||
                startBuild.error ||
                publish.error) && (
                <ErrorState
                  error={
                    save.error ||
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


const experimentInitial: ExperimentDraft = {
  kind: "experiment",
  id: "",
  name: "未命名网络实验",
  sourceExperimentId: null,
  scenarioId: "",
  provenance: { kind: "new", label: "新建" },
  routingMode: "DirectToFusionCenter",
  macMode: "Tdma",
  workload: "BearingFusion",
  minimumBearingPoints: "5",
  maximumFusionPeriodNs: "180000000000",
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
  const capabilities = useQuery({ queryKey: ["authoring-capabilities"], queryFn: apiClient.authoringCapabilities, retry: false });
  const [params] = useSearchParams();
  const requestedDraftId = params.get("draft");
  const scenarios = useQuery(queries.scenarios());
  const environments = useQuery(queries.environments());
  const trajectories = useQuery(queries.trajectories());
  const environmentNames = useQueries({ queries: (environments.data ?? []).map(environment => ({
    queryKey: ["environment-inputs", environment.environment_asset_id], queryFn: () => apiClient.environmentInputs(environment.environment_asset_id),
  })) });
  const [inspectorSection, setInspectorSection] = useState("basic");
  const scenarioDrafts = useQuery(queries.drafts("scenario"));
  const experimentDrafts = useQuery({ ...queries.drafts("experiment"), enabled: Boolean(requestedDraftId) });
  const client = useQueryClient();
  const navigate = useNavigate();
  const [value, setValue] = useState(() => ({ ...experimentInitial, scenarioId: params.get("scenario") ?? "" }));
  const [server, setServer] = useState<AuthoringDraftDto | null>(null);
  const [scene, setScene] = useState<ScenarioDraft>(() => scenarioInitial([], ""));
  const initialSceneSignature = useRef(JSON.stringify([scene.name, scene.environmentAssetId, scene.nodes, scene.trajectoryBindings, scene.spatialDomain]));
  const [sceneServer, setSceneServer] = useState<AuthoringDraftDto | null>(null);
  const loadedScene = useRef("");
  const loadedExperiment = useRef("");
  const [savedSignature, setSavedSignature] = useState("");
  const [physicalLayer, setPhysicalLayer] = useState<"Channel1Abstract" | "Channel2Waveform">("Channel1Abstract");
  const [waveformModulation, setWaveformModulation] = useState<"Bpsk" | "Bfsk">("Bpsk");
  const [waveformNoise, setWaveformNoise] = useState<"None" | "Awgn">("None");
  const [waveformNoisePsd, setWaveformNoisePsd] = useState(0);
  const [slotDurationNs, setSlotDurationNs] = useState("6000000000");
  const [equivalentNoise, setEquivalentNoise] = useState(45);
  const [target, setTarget] = useState({ x: 200, y: 150 });
  const set = <K extends keyof ExperimentDraft>(
    key: K,
    val: ExperimentDraft[K],
  ) => setValue({ ...value, [key]: val });
  const selectedScenarioDraft = scenarioDrafts.data?.find((item) => item.draft_id === value.scenarioId);
  const sceneEnvironment = environments.data?.find(item => item.environment_asset_id === scene.environmentAssetId);
  useEffect(() => {
    if (!selectedScenarioDraft || loadedScene.current === selectedScenarioDraft.draft_id) return;
    loadedScene.current = selectedScenarioDraft.draft_id;
    const p = selectedScenarioDraft.payload;
    setSceneServer(selectedScenarioDraft);
    setScene({ kind: "scenario", id: selectedScenarioDraft.draft_id, name: selectedScenarioDraft.name,
      environmentAssetId: String(p.environment_asset_id ?? ""), fusionCenterNodeId: String(p.fusion_center_node_id ?? "0"),
      nodes: structuredClone(p.nodes as ScenarioDraft["nodes"] ?? []),
      trajectoryBindings: structuredClone(p.trajectory_bindings as ScenarioDraft["trajectoryBindings"] ?? []),
      spatialDomain: structuredClone(p.spatial_domain as ScenarioDraft["spatialDomain"] ?? null),
      provenance: { kind: "new", label: "新建" }, updatedAt: selectedScenarioDraft.updated_at });
  }, [selectedScenarioDraft]);
  const fusionPeriodSeconds = Number(value.maximumFusionPeriodNs) / 1e9;
  const guardSeconds = Number(value.guardIntervalNs) / 1e9;
  useEffect(() => {
    const selected = experimentDrafts.data?.find((item) => item.draft_id === requestedDraftId);
    if (!selected || server?.draft_id === selected.draft_id) return;
    const source = selected.payload;
    const text = (key: string, fallback: string) => source[key] === undefined ? fallback : String(source[key]);
    const number = (key: string, fallback: number) => source[key] === undefined ? fallback : Number(source[key]);
    setServer(selected);
    setValue({
      ...experimentInitial,
      id: selected.draft_id,
      name: selected.name,
      scenarioId: text("scenario_id", ""),
      routingMode: text("routing_mode", experimentInitial.routingMode),
      macMode: text("mac_mode", experimentInitial.macMode),
      workload: text("workload", experimentInitial.workload) === "CommunicationOnly" ? "CommunicationOnly" : "BearingFusion",
      minimumBearingPoints: text("minimum_bearing_points", experimentInitial.minimumBearingPoints),
      maximumFusionPeriodNs: text("maximum_fusion_period_ns", experimentInitial.maximumFusionPeriodNs),
      bitRateBitsPerSecond: text("bit_rate_bits_per_second", experimentInitial.bitRateBitsPerSecond),
      centerFrequencyHz: number("center_frequency_hz", experimentInitial.centerFrequencyHz),
      occupiedBandwidthHz: number("occupied_bandwidth_hz", experimentInitial.occupiedBandwidthHz),
      sourceLevelDb: number("source_level_db", experimentInitial.sourceLevelDb),
      guardIntervalNs: text("guard_interval_ns", experimentInitial.guardIntervalNs),
      simulationCycleCount: text("simulation_cycle_count", experimentInitial.simulationCycleCount),
      deterministicSeed: text("deterministic_seed", experimentInitial.deterministicSeed),
      networkUpdateIntervalCycles: text("network_update_interval_cycles", experimentInitial.networkUpdateIntervalCycles),
      updatedAt: selected.updated_at,
    });
    setPhysicalLayer(text("physical_layer", "Channel1Abstract") === "Channel2Waveform" ? "Channel2Waveform" : "Channel1Abstract");
    setWaveformModulation(text("waveform_modulation", "Bpsk") === "Bfsk" ? "Bfsk" : "Bpsk");
    setWaveformNoise(text("waveform_noise", "None") === "Awgn" ? "Awgn" : "None");
    setWaveformNoisePsd(number("waveform_noise_psd_db_re_1upa2_per_hz", 0));
    setSlotDurationNs(text("slot_duration_ns", "6000000000"));
    setEquivalentNoise(number("equivalent_noise_power_db_re_1upa2", 45));
    setTarget({ x: number("target_x_m", 200), y: number("target_y_m", 150) });
  }, [experimentDrafts.data, requestedDraftId, server?.draft_id]);
  const payload = {
    ...server?.payload,
    scenario_id: value.scenarioId,
    routing_mode: value.routingMode,
    mac_mode: value.macMode,
    workload: value.workload,
    minimum_bearing_points: value.minimumBearingPoints,
    maximum_fusion_period_ns: value.maximumFusionPeriodNs,
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
    slot_duration_ns: slotDurationNs,
    equivalent_noise_power_db_re_1upa2: equivalentNoise,
    target_x_m: target.x,
    target_y_m: target.y,
  };
  const scenePayload = {
    ...sceneServer?.payload,
    environment_asset_id: scene.environmentAssetId,
    environment_version: Number(sceneEnvironment?.asset_format_version ?? sceneServer?.payload.environment_version ?? 1),
    fusion_center_node_id: scene.fusionCenterNodeId,
    nodes: scene.nodes,
    trajectory_bindings: scene.trajectoryBindings ?? [],
    spatial_domain: scene.spatialDomain ?? null,
  };
  const signature = JSON.stringify([scene.name, scenePayload, value.name, payload]);
  useEffect(() => {
    if (server && sceneServer && server.payload.scenario_id === sceneServer.draft_id && loadedExperiment.current !== server.draft_id) {
      loadedExperiment.current = server.draft_id;
      setSavedSignature(signature);
    }
  }, [server, sceneServer, signature]);
  const saveComposition = async () => {
    if (scene.spatialDomain && (capabilities.data?.scene_domain_validation !== true || !isSceneDomain(scene.spatialDomain))) throw new Error("场景范围不合法或当前后端不支持范围校验，未保存或运行。");
    if (scene.trajectoryBindings?.some(binding => binding.placement_mode === "RelativeToNodeStart") && capabilities.data?.relative_trajectory_placement !== true) throw new Error("当前服务不支持跟随场景起点的轨迹绑定，请使用新版服务。未保存或启动运行。");
    if (value.scenarioId && !sceneServer) throw new Error("所选场景未能载入。请重新选择，不能替换为默认场景。");
    const savedScene = sceneServer ? await apiClient.updateDraft(sceneServer, scene.name, scenePayload)
      : await apiClient.createDraft("scenario", scene.name, scenePayload);
    setSceneServer(savedScene); loadedScene.current = savedScene.draft_id;
    setValue(current => ({ ...current, scenarioId: savedScene.draft_id }));
    // Freeze the scene revision saved by this action, not whichever draft is latest later.
    const exactPayload = { ...payload, scenario_id: savedScene.draft_id, scenario_revision: savedScene.revision };
    let saved: AuthoringDraftDto;
    try {
      saved = server ? await apiClient.updateDraft(server, value.name, exactPayload)
        : await apiClient.createDraft("experiment", value.name, exactPayload);
    } catch (error) {
      throw new Error(`节点场景已保存（修订 ${savedScene.revision}）；实验保存失败，未启动运行。${error instanceof Error ? error.message : "请重试"}`);
    }
    setServer(saved);
    setSavedSignature(JSON.stringify([scene.name, scenePayload, value.name, exactPayload]));
    await client.invalidateQueries({ queryKey: ["authoring-drafts"] });
    return { saved, savedScene };
  };
  const save = useMutation({
    mutationFn: saveComposition,
    onSuccess: () => run.reset(),
  });
  const run = useMutation({
    mutationFn: async () => {
      const positiveIntegers = [value.bitRateBitsPerSecond, value.simulationCycleCount, value.networkUpdateIntervalCycles, slotDurationNs];
      if (!positiveIntegers.every(text => /^[1-9]\d*$/.test(text))) throw new Error("速率、周期数、网络刷新间隔与时隙时长必须是有效正数。");
      if (!/^\d+$/.test(value.guardIntervalNs) || BigInt(value.guardIntervalNs) >= BigInt(slotDurationNs)) throw new Error("保护时间必须非负且小于一个时隙。");
      if (!/^\d+$/.test(value.deterministicSeed) || BigInt(value.deterministicSeed) > 18446744073709551615n) throw new Error("随机种子必须是 0～18446744073709551615 之间的整数。");
      if (![value.centerFrequencyHz, value.occupiedBandwidthHz, value.sourceLevelDb, equivalentNoise, waveformNoisePsd, target.x, target.y].every(Number.isFinite)
        || value.centerFrequencyHz <= 0 || value.occupiedBandwidthHz <= 0) throw new Error("设备与噪声参数必须是有限数值，频率和带宽必须大于零。");
      const { saved, savedScene } = await saveComposition();
      if (savedScene.validation_issues.length) throw new Error(`节点场景已保存，但尚不能运行：${savedScene.validation_issues.join("；")}`);
      if (saved.validation_issues.length) throw new Error(saved.validation_issues.join("；"));
      if (scene.nodes.length < 3 || scene.nodes.length > 6) throw new Error("实验草稿已保存；当前运行引擎仅支持 3～6 个节点，未启动运行。不会自动补充或删减节点。");
      const coverage = trajectoryRunCoverage(scene, trajectories.data ?? [], value.simulationCycleCount,
        slotDurationNs, value.macMode);
      if (coverage.issues.length) throw new Error(`实验已保存，运行前检查：${coverage.issues.join("；")}`);
      return apiClient.createAuthoringRun(saved.draft_id, saved.revision);
    },
    onSuccess: async (created) => {
      await client.invalidateQueries({ queryKey: ["runs"] });
      navigate(`/runs/${created.run_id}?presentation=live`);
    },
  });
  if (scenarios.isPending || environments.isPending || trajectories.isPending || scenarioDrafts.isPending || (requestedDraftId && experimentDrafts.isPending)) return <LoadingState />;
  const loadError = scenarios.error || environments.error || trajectories.error || scenarioDrafts.error || (requestedDraftId && experimentDrafts.error);
  if (loadError) return <ErrorState error={loadError} />;
  if (requestedDraftId && !experimentDrafts.data?.some(draft => draft.draft_id === requestedDraftId)) return <p role="alert">这个实验草稿不存在，未改用其他案例。<Link to="/workspace/experiment">新建实验</Link></p>;
  const coverage = trajectoryRunCoverage(scene, trajectories.data ?? [], value.simulationCycleCount,
    slotDurationNs, value.macMode);
  const inspector = (
        <div className="experiment-inspector" data-section={inspectorSection}>
          <div className="studio-pane-title"><strong>环境与实验参数</strong></div>
          <nav className="inspector-tabs" aria-label="实验参数分组">{[["basic", "资源"], ["network", "网络"], ["phy", "物理模式"], ["execution", "运行"]].map(([id, label]) => <button type="button" key={id} className={id === inspectorSection ? "selected" : ""} onClick={() => setInspectorSection(id)}>{label}</button>)}</nav>
          <section id="basic" className="panel form-panel">
            <h2>场景与资源</h2>
            <label>场景名称<input value={scene.name} onChange={e => setScene({ ...scene, name: e.target.value })} /></label>
            <label>
              使用已有节点场景
              <select
                value={value.scenarioId}
                onChange={(e) => {
                  const sceneChanged = JSON.stringify([scene.name, scene.environmentAssetId, scene.nodes, scene.trajectoryBindings, scene.spatialDomain]) !== initialSceneSignature.current;
                  if (signature !== savedSignature && (sceneServer || sceneChanged) && !window.confirm("切换场景将丢弃本页尚未保存的节点修改，是否继续？")) return;
                  set("scenarioId", e.target.value); loadedScene.current = "";
                  if (!e.target.value) { setScene(scenarioInitial([], "")); setSceneServer(null); }
                }}
              >
                <option value="">新建节点场景</option>
                {scenarioDrafts.data.map((x) => (
                  <option value={x.draft_id} key={x.draft_id}>
                    {x.name} · 修订 {x.revision}
                  </option>
                ))}
              </select>
            </label>
            {value.scenarioId && !sceneServer && <p role="alert">当前场景引用无法读取，请重新选择。不会使用默认场景。</p>}
            <label>实验环境<select value={scene.environmentAssetId} onChange={e => setScene({ ...scene, environmentAssetId: e.target.value })}>
              <option value="">请选择已发布环境</option>{environments.data!.map((environment, index) => <option key={environment.environment_asset_id} value={environment.environment_asset_id}>{String(environmentNames[index].data?.name ?? (environment.environment_asset_id === "reference-shallow-water-v1" ? "南海浅水参考环境" : `已发布环境 ${index + 1}`))} · v{environment.asset_format_version}</option>)}
            </select></label>
            <p className="property-help">点击画布节点，在此选择轨迹和收发能力。环境需先在环境建设中发布。</p>
            <p className="property-help">当前运行引擎支持 3～6 个节点。其他规模可编辑、保存，尚不能启动运行。</p>
            <SceneDomainEditor value={scene.spatialDomain} onChange={spatialDomain => setScene({ ...scene, spatialDomain })}
              supported={capabilities.data?.scene_domain_validation === true} environment={sceneEnvironment}
              inputs={environmentNames[(environments.data ?? []).findIndex(e => e.environment_asset_id === scene.environmentAssetId)]?.data} />
          </section>
          <section id="network" className="panel form-panel">
            <h2>网络与协议</h2>
            <div className="form-grid">
              <label>
                路由
                <select
                  value={value.routingMode}
                  onChange={(e) => set("routingMode", e.target.value)}
                >
                  <option value="DirectToFusionCenter">直接发送到汇聚节点</option>
                  <option value="ShortestPath">最短路径到汇聚节点</option>
                </select>
              </label>
              <label>
                MAC
                <select
                  value={value.macMode}
                  onChange={(e) => set("macMode", e.target.value)}
                >
                  <option value="Tdma">TDMA · 节点按时隙轮流发送</option>
                </select>
              </label>
              <label>
                业务负载
                <select aria-label="业务负载" value={value.workload}
                  onChange={(e) => set("workload", e.target.value as ExperimentDraft["workload"])}>
                  <option value="CommunicationOnly">通信性能实验</option>
                  <option value="BearingFusion">探测与定位实验</option>
                </select>
              </label>
              <p className="field-explanation workload-explanation">{value.workload === "CommunicationOnly" ? "纯网络通信：只产生网络业务和收发结果，不要求生成方位报告或融合结果。" : "方位特征融合：节点产生方位报告；只有经网络成功交付到融合中心的报告才参与融合。"}</p>
              <p className="field-explanation">直达：所有业务送往汇聚节点；最短路径：依据当前有向网络选择可达路径，可能中继。TDMA 是当前开放的接入方式，不提供尚未接入的算法。</p>
            </div>
            {value.workload === "BearingFusion" && <details className="parameter-detail"><summary>融合算法设置 · 当前至少 {value.minimumBearingPoints} 条有效观测</summary>
              <p>场景决定可产生的观测，链路决定能交付的观测。算法只在周期末检查已交付报告；满足数量且几何可解时才形成融合。同一节点可在不同时间贡献观测，观测数不等于节点数。</p>
              <label>算法最少有效观测数<input type="number" min="5" step="1" value={value.minimumBearingPoints} onChange={e => set("minimumBearingPoints", e.target.value)} /></label>
              <small>当前实验策略要求至少 5 条，不是说最小二乘数学上必然需要 5 个节点。普通实验保留现有设置即可，不需要安排触发时刻。</small>
            </details>}
            {value.workload === "BearingFusion" && <details className="parameter-detail"><summary>探测目标 · 合成观测的参考位置</summary><div className="form-grid"><label>目标 X（m）<input type="number" value={target.x} onChange={e => setTarget({ ...target, x: Number(e.target.value) })} /></label><label>目标 Y（m）<input type="number" value={target.y} onChange={e => setTarget({ ...target, y: Number(e.target.value) })} /></label></div><p>用于生成节点观测，不直接送入融合求解器；融合仍只使用网络成功交付的观测。</p></details>}
            {value.workload === "BearingFusion" && <details className="parameter-detail"><summary>结果对照条件 · 不控制仿真</summary>
              <label>报告收集与融合耗时参考（s）<input type="number" min="1" step="1" value={fusionPeriodSeconds} onChange={e => set("maximumFusionPeriodNs", String(Math.round(Number(e.target.value) * 1e9)))} /></label>
              <p>从本轮最早有效观测的采样时刻，到融合完成的仿真时长；不是 CPU 计算耗时、定时器或自动终止条件。仅在结果中供人工对照。</p>
            </details>}
            <details className="parameter-detail"><summary>网络刷新策略 · 每 {value.networkUpdateIntervalCycles} 个通信周期</summary><label>重新规划间隔（周期）<input type="number" min="1" step="1" value={value.networkUpdateIntervalCycles} onChange={e => set("networkUpdateIntervalCycles", e.target.value)} /></label><p>重新计算网络结构与计划的间隔；不是向 TDMA 读取调度表的频率。基础实验可保留当前值；较短间隔表示更频繁更新网络结构。</p></details>
          </section>
          <section id="phy" className="panel form-panel">
            <h2>物理模式</h2>
            <label>仿真模式<select aria-label="物理层实现" value={physicalLayer} onChange={e => setPhysicalLayer(e.target.value as "Channel1Abstract" | "Channel2Waveform")}>
              <option value="Channel1Abstract">模式 A · 快速网络仿真</option><option value="Channel2Waveform">模式 B · 波形通信仿真</option>
            </select></label>
            <p className="property-help">{physicalLayer === "Channel1Abstract" ? "用于比较路由、接入与网络表现。以声学传播和接收模型计算结果，不逐比特生成波形。" : "逐比特生成、传播、解调波形，可观察实际误码与 CRC；计算量更大，多径畸变可能造成解码失败。"}</p>
            {physicalLayer === "Channel2Waveform" && <div className="waveform-config">
              <label>调制方式<select value={waveformModulation} onChange={e => setWaveformModulation(e.target.value as "Bpsk" | "Bfsk")}><option value="Bpsk">BPSK · 二进制相移键控</option><option value="Bfsk">BFSK · 二进制频移键控</option></select></label>
              <label>接收波形噪声<select value={waveformNoise} onChange={e => setWaveformNoise(e.target.value as "None" | "Awgn")}><option value="None">不额外加入噪声</option><option value="Awgn">加入高斯白噪声</option></select></label>
              {waveformNoise === "Awgn" && <label>噪声功率谱密度（dB re 1 μPa²/Hz）<input type="number" value={waveformNoisePsd} onChange={e => setWaveformNoisePsd(Number(e.target.value))} /><small>每赫兹带宽的噪声功率；数值越大噪声越强。它不是误码率，也不等于接收 SNR。</small></label>}
              <p className="property-help">关闭额外噪声仍保留真实多径影响，不保证所有链路解调成功。</p>
            </div>}
            <details className="parameter-detail"><summary>通信设备参数 · 高级设置</summary><dl className="parameter-facts">
              <dt>发送比特率</dt><dd>{value.bitRateBitsPerSecond} bit/s</dd>
              <dt>中心频率</dt><dd>{value.centerFrequencyHz / 1000} kHz</dd>
              <dt>占用带宽</dt><dd>{value.occupiedBandwidthHz / 1000} kHz</dd>
              <dt>声源级</dt><dd>{value.sourceLevelDb} dB re 1 μPa @ 1 m</dd>
            </dl><div className="form-grid">
              <label>发送比特率（bit/s）<input inputMode="numeric" value={value.bitRateBitsPerSecond} onChange={e => set("bitRateBitsPerSecond", e.target.value)} /></label>
              <label>中心频率（Hz）<input type="number" min="1" value={value.centerFrequencyHz} onChange={e => set("centerFrequencyHz", Number(e.target.value))} /></label>
              <label>占用带宽（Hz）<input type="number" min="1" value={value.occupiedBandwidthHz} onChange={e => set("occupiedBandwidthHz", Number(e.target.value))} /></label>
              <label>声源级（dB re 1 μPa @ 1 m）<input type="number" value={value.sourceLevelDb} onChange={e => set("sourceLevelDb", Number(e.target.value))} /></label>
              {physicalLayer === "Channel1Abstract" && <label>等效接收噪声功率（dB re 1 μPa²）<input type="number" value={equivalentNoise} onChange={e => setEquivalentNoise(Number(e.target.value))} /><small>抽象接收模型的带内噪声功率，不是模式 B 的每赫兹噪声谱密度。</small></label>}
            </div><p>参数实际进入运行。中心频率必须在所选环境的离线计算覆盖范围内；更改速率会影响发送时长，需要同时检查时隙。配置速率不等于有效吞吐量。</p></details>
          </section>
          <section id="execution" className="panel form-panel">
            <h2>运行时长</h2>
            <label>通信周期数<input type="number" min="1" step="1" value={value.simulationCycleCount} onChange={e => set("simulationCycleCount", e.target.value)} /></label>
            <p className="property-help">每个周期为所有非汇聚节点各安排一个 TDMA 时隙；不是轨迹采样次数，也不是每秒帧数。</p>
            <div className="duration-preview"><strong>预计仿真时段</strong><b>{coverage.endNs === null ? "请检查周期数" : `0～${Number(coverage.endNs) / 1e9} s`}</b><small>{Math.max(0, scene.nodes.length - 1)} 个发送节点 × {Number(slotDurationNs) / 1e9} s / 时隙 × {value.simulationCycleCount} 周期</small></div>
            {coverage.issues.length > 0 && <details className="parameter-detail coverage-warning"><summary>轨迹覆盖待检查 · {coverage.issues.length} 项</summary>{coverage.issues.map((issue, i) => <p key={i}>{issue}</p>)}</details>}
            <details className="parameter-detail"><summary>TDMA 时序 · 高级设置</summary><div className="form-grid"><label>每个时隙（s）<input type="number" min="0.001" step="0.1" value={Number(slotDurationNs) / 1e9} onChange={e => setSlotDurationNs(String(Math.round(Number(e.target.value) * 1e9)))} /></label><label>时隙保护时间（s）<input type="number" min="0" step="0.1" value={guardSeconds} onChange={e => set("guardIntervalNs", String(Math.round(Number(e.target.value) * 1e9)))} /></label></div><p>保护时间在时隙内部，为传播和收发结束留余量，不另加到周期时长中。基础实验保留现有值；缩短时隙若使物理事件跨出周期，运行会明确失败，不会裁掉晚到事件。</p></details>
            <details className="parameter-detail"><summary>复现实验 · 随机种子 {value.deterministicSeed}</summary><label>随机种子<input inputMode="numeric" value={value.deterministicSeed} onChange={e => set("deterministicSeed", e.target.value)} /></label><p>控制模型内的随机序列。保留同一资源版本、参数和种子用于复现；更换种子用于比较随机噪声影响。它不是节点编号、启动时间或采样频率。</p></details>
          </section>
        </div>
  );
  const workspaceControls = <section className="experiment-control-panel" aria-label="实验操作">
    <div className="unified-experiment-toolbar">
      <div><h1>实验工作台</h1><small>节点、轨迹与网络配置 · 同页保存</small></div>
      <label>实验名称<input value={value.name} onChange={e => set("name", e.target.value)} disabled={save.isPending || run.isPending} /></label>
      <span role="status">{server ? signature === savedSignature ? `已保存 · 修订 ${server.revision}` : `修订 ${server.revision} · 编辑中` : "尚未保存"}</span>
      <button type="button" onClick={() => save.mutate()} disabled={save.isPending || run.isPending}>保存实验草稿</button>
      <button type="button" className="primary-button" onClick={() => run.mutate()} disabled={run.isPending || save.isPending}>{run.isPending ? "正在保存并启动…" : "保存并运行"}</button>
    </div>
    {(save.error || run.error) && <p role="alert" className="studio-save-error">{save.error?.message || run.error?.message}</p>}
    {server && <p className="studio-save-note">场景与实验已保存到服务器 · 场景修订 {sceneServer?.revision} / 实验修订 {server.revision}{sceneServer?.validation_issues.length ? ` · 待完善：${sceneServer.validation_issues.join("；")}` : ""}</p>}
    <p className="experiment-control-help">在中间布设节点，右侧编辑所选节点。点击画布空白处或“环境与实验参数”，切回网络与运行配置。</p>
  </section>;
  return <div className="unified-experiment-page authoring-layout">
    <fieldset className="studio-editing-surface" disabled={save.isPending || run.isPending}>
      <ScenarioDesigner draft={scene} onChange={setScene} trajectories={trajectories.data} globalInspector={inspector}
        workspaceControls={workspaceControls}
        allowRelativePlacement={capabilities.data?.relative_trajectory_placement === true}
        environmentInputs={environmentNames[(environments.data ?? []).findIndex(environment => environment.environment_asset_id === scene.environmentAssetId)]?.data}
        surfaceZMeters={sceneEnvironment?.coordinate_frame.surface_z_meters}
        onTrajectoryCreated={() => { client.invalidateQueries({ queryKey: ["trajectories"] }); }} />
    </fieldset>
  </div>;
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
