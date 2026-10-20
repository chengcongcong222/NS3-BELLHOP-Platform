import { AcousticPlots } from "../components/AcousticPlots";
import { OsdmSettings } from "../components/OsdmSettings";
import type { OsdmConfig } from "../domain/experimentPresets";
import { useExperimentIdentity } from "./experimentEditorContext";
import { DepthSamplingInput } from "../components/DepthSamplingInput";
import { acousticParameterErrors, automaticSampling, depthSamples, restoreSampling, samplingBudget, type DepthSampling } from "../domain/environmentSampling";
import { useEffect, useMemo, useRef, useState, useTransition } from "react";
import { useMutation, useQuery, useQueryClient } from "@tanstack/react-query";
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
import { ExperimentParticipation } from "../components/ExperimentParticipation";
import { isSceneDomain } from "../domain/sceneSpatial";
import { trajectoryRunCoverage } from "../domain/trajectoryCoverage";
import { planningConfigurationIssues, planningNanosecondsInput, planningSecondsInput } from "../domain/planningConfiguration";
import { AlgorithmParameters } from "../components/AlgorithmParameters";
import { algorithmDefaults, algorithmIssues, algorithmKey, bindFlowIdentities } from "../domain/algorithms";
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
  name: "",
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
  sourceSampling: DepthSampling;
  receiverSampling: DepthSampling;
  rangeSampleCount: number;
}
const defaultBuildParameters: EnvironmentBuildParameters = {
  bottomSoundSpeed: 1700, bottomDensity: 1.8,
  bottomAttenuation: 0.5, beamCount: 501,
  minimumAngle: -30, maximumAngle: 30,
  sourceSampling: automaticSampling(), receiverSampling: automaticSampling(), rangeSampleCount: 101,
};
const acquisitionInitial = { latitudeDeg: 18, longitudeDeg: 110, month: 4 as number | null, requestedSspDepthMeters: 200,
  bearingDeg: 90, requestedRangeMeters: 10000, terrainSamples: 41, useReferenceCache: false };
const flatBottomSource = { kind: "ideal-flat-bottom", description: "平坦海底计算假设，非实测地形" };
type AcquisitionParameters = typeof acquisitionInitial;
const samplingWaterDepth = (value: EnvironmentDraft, sources: Record<string, any>) => sources.bottom_boundary?.kind === "ideal-flat-bottom" || !value.bathymetry.length
  ? value.maximumDepthMeters : Math.min(...value.bathymetry.map(point => point.depthMeters));
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
  bathymetry: (sources.bottom_boundary?.kind === "ideal-flat-bottom"
    ? [{ rangeMeters: 0, depthMeters: value.maximumDepthMeters }, { rangeMeters: value.maximumRangeMeters, depthMeters: value.maximumDepthMeters }]
    : value.bathymetry).map((point) => ({
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
    source_depths_meters: depthSamples(parameters.sourceSampling, samplingWaterDepth(value, sources)).points,
    receiver_depths_meters: depthSamples(parameters.receiverSampling, samplingWaterDepth(value, sources)).points,
    sampling_controls: { source: parameters.sourceSampling, receiver: parameters.receiverSampling },
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
    maximumDepthMeters: Array.isArray(p.bathymetry) && p.bathymetry.length ? Math.max(...p.bathymetry.map((x: any) => Number(x.depth_meters))) : Number(p.maximum_depth_meters ?? 200),
    maximumRangeMeters: Array.isArray(p.bathymetry) && p.bathymetry.length ? Math.max(...p.bathymetry.map((x: any) => Number(x.range_meters))) : Number(p.maximum_range_meters ?? 10000),
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
        { id: 1, label: "基本信息" },
        { id: 2, label: "水文来源" },
        { id: 4, label: "配置与预览" },
        { id: 5, label: "构建与发布" },
      ].map(({ id, label }, index) => (
        <li className={id === current ? "active" : ""} key={label}>
          <button type="button" aria-current={id === current ? "step" : undefined} onClick={() => select(id)}><b>{index + 1}</b><span>{label}</span></button>
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
  const [params, setParams] = useSearchParams();
  const drafts = useQuery(queries.drafts("environment"));
  const client = useQueryClient();
  const [value, setValue] = useState(environmentInitial);
  const [serverDraft, setServerDraft] = useState<AuthoringDraftDto | null>(
    null,
  );
  const [step, setStep] = useState(2);
  const sourceId = params.get("source") ?? "";
  const [showPlots, setShowPlots] = useState(Boolean(sourceId));
  const source = useQuery({ queryKey: ["environment-inputs", sourceId], queryFn: () => apiClient.environmentInputs(sourceId), enabled: !!sourceId });
  const loadedSource = useRef("");
  const [hydrologySource, setHydrologySource] = useState<
    "woa" | "file"
  >("woa");
  const [build, setBuild] = useState<EnvironmentBuildDto | null>(null);
  const requestedBuild = params.get("build") ?? "";
  const savedBuild = useQuery({ queryKey: ["environment-build", requestedBuild], queryFn: () => apiClient.getEnvironmentBuild(requestedBuild), enabled: !!requestedBuild, staleTime: Infinity });
  const loadedBuild = useRef("");
  const [hydratedBuild, setHydratedBuild] = useState("");
  const [builtInput, setBuiltInput] = useState<string | null>(null);
  const [notice, setNotice] = useState("");
  const nameInput = useRef<HTMLInputElement>(null);
  const [nameError, setNameError] = useState("");
  const [publishedBuildId, setPublishedBuildId] = useState("");
  const requireName = () => {
    if (value.name.trim() && value.name.trim() !== "未命名水声环境") { setNameError(""); return true; }
    setNameError("请先填写环境名称。"); nameInput.current?.focus(); return false;
  };
  const [importError, setImportError] = useState<unknown>(null);
  const [buildParameters, setBuildParameters] = useState(defaultBuildParameters);
  const [acquisition, setAcquisition] = useState(acquisitionInitial);
  const [sources, setSources] = useState<Record<string, any>>({ bottom_boundary: flatBottomSource });
  const waterDepth = samplingWaterDepth(value, sources);
  const sourceSampling = depthSamples(buildParameters.sourceSampling, waterDepth);
  const receiverSampling = depthSamples(buildParameters.receiverSampling, waterDepth);
  const budget = samplingBudget(sourceSampling.points, receiverSampling.points, buildParameters.rangeSampleCount);
  const samplingErrors = [sourceSampling.error && `发射深度：${sourceSampling.error}`, receiverSampling.error && `接收深度：${receiverSampling.error}`].filter(Boolean) as string[];
  const buildErrors = [...samplingErrors, ...budget.errors, ...acousticParameterErrors(buildParameters, value.centerFrequencyHz)];
  const currentInput = JSON.stringify([value.name, toEnvironmentPayload(value, buildParameters, acquisition, sources)]);
  const currentInputRef = useRef(currentInput); currentInputRef.current = currentInput;
  const acquisitionRef = useRef(acquisition); acquisitionRef.current = acquisition;
  const woaAbort = useRef<AbortController | null>(null);
  const [woaProgress, setWoaProgress] = useState<AcquisitionProgress | null>(null);
  const [woaElapsed, setWoaElapsed] = useState(0);
  useEffect(() => () => woaAbort.current?.abort(), []);
  const capabilities = useQuery({ queryKey: ["environment-capabilities"], queryFn: () => apiClient.environmentCapabilities() });
  const hydrologyCache = useQuery({ queryKey: ["hydrology-cache"], queryFn: () => apiClient.hydrologyCache(), enabled: capabilities.data?.hydrology_cache_available === true });
  const [selectedProfile, setSelectedProfile] = useState("");
  const reuseProfile = useMutation({
    mutationFn: (id: string) => apiClient.hydrologyProfile(id),
    onSuccess: record => {
      const result = record.result as any;
      const selection = record.selection as any;
      const points = result.sound_speed_profile as Array<any>;
      const { sound_speed_profile: _, ...metadata } = result;
      const request = selection ? { latitude_deg: selection.latitude_deg, longitude_deg: selection.longitude_deg,
        month: selection.month, maximum_depth_meters: selection.maximum_depth_meters, use_reference_cache: selection.use_reference_cache } : undefined;
      if (selection) setAcquisition(current => ({ ...current, latitudeDeg: selection.latitude_deg, longitudeDeg: selection.longitude_deg,
        month: selection.month, requestedSspDepthMeters: selection.maximum_depth_meters, useReferenceCache: false }));
      setSources(current => ({ ...current, ssp: { ...metadata, ...(request ? { request } : {}), cache_id: record.cache_id } }));
      setValue(current => ({ ...current, soundSpeedProfile: points.map(point => ({ depthMeters: point.depth_meters, speedMetersPerSecond: point.speed_meters_per_second })),
        maximumDepthMeters: current.bathymetry.length ? current.maximumDepthMeters : points.at(-1)!.depth_meters }));
      setNotice("已复用服务器保存的水文剖面，没有重新下载。来源与原查询条件随草稿保存。");
      setStep(4);
    },
  });
  useEffect(() => {
    if (requestedBuild && !savedBuild.data) return;
    const snapshot = savedBuild.data?.artifact?.source_snapshot as AuthoringDraftDto | undefined;
    let selected = snapshot ?? drafts.data?.find(
      (item) => item.draft_id === params.get("draft"),
    );
    if (!selected && source.data && sourceId && loadedSource.current !== sourceId) {
      loadedSource.current = sourceId;
      setServerDraft(null);
      selected = { kind: "environment", draft_id: "", name: String(source.data.name), payload: source.data.payload as Record<string, unknown>, revision: 0, created_at: "", updated_at: "", validation_issues: [] };
      setNotice((source.data.limitations as string[]).join("；"));
    }
    if (selected && (selected.draft_id !== serverDraft?.draft_id || (snapshot && hydratedBuild !== savedBuild.data!.build_id))) {
      if (selected.draft_id) setServerDraft(selected);
      setValue(fromEnvironmentDraft(selected));
      const p = selected.payload as any;
      setAcquisition({ ...acquisitionInitial, ...p.acquisition, useReferenceCache: false });
      setSources(p.bathymetry?.length ? p.sources ?? {} : { ...p.sources, bottom_boundary: flatBottomSource });
      setBuildParameters({
        bottomSoundSpeed: p.bottom?.sound_speed_meters_per_second ?? defaultBuildParameters.bottomSoundSpeed,
        bottomDensity: p.bottom?.density_grams_per_cubic_centimeter ?? defaultBuildParameters.bottomDensity,
        bottomAttenuation: p.bottom?.attenuation_db_per_wavelength ?? defaultBuildParameters.bottomAttenuation,
        beamCount: p.bellhop?.beam_count ?? defaultBuildParameters.beamCount,
        minimumAngle: p.bellhop?.minimum_launch_angle_degrees ?? defaultBuildParameters.minimumAngle,
        maximumAngle: p.bellhop?.maximum_launch_angle_degrees ?? defaultBuildParameters.maximumAngle,
        sourceSampling: restoreSampling(p.bellhop?.source_depths_meters, p.bellhop?.sampling_controls?.source, samplingWaterDepth(fromEnvironmentDraft(selected), p.sources ?? {})),
        receiverSampling: restoreSampling(p.bellhop?.receiver_depths_meters, p.bellhop?.sampling_controls?.receiver, samplingWaterDepth(fromEnvironmentDraft(selected), p.sources ?? {})),
        rangeSampleCount: p.bellhop?.range_sample_count ?? defaultBuildParameters.rangeSampleCount,
      });
      if (snapshot) setHydratedBuild(savedBuild.data!.build_id);
    }
  }, [drafts.data, params, serverDraft?.draft_id, source.data, sourceId, requestedBuild, savedBuild.data, hydratedBuild]);
  useEffect(() => {
    const result = savedBuild.data;
    const snapshot = result?.artifact?.source_snapshot as AuthoringDraftDto | undefined;
    if (!result || !snapshot || hydratedBuild !== result.build_id || loadedBuild.current === result.build_id || serverDraft?.draft_id !== snapshot.draft_id) return;
    loadedBuild.current = result.build_id;
    setBuild(result);
    setBuiltInput(currentInput);
    setShowPlots(result.state === "Completed");
    setStep(result.state === "Completed" ? 5 : 4);
    if (result.artifact?.published) setPublishedBuildId(result.build_id);
    setNotice("已打开保存的计算结果，未重新计算。修改后需重新构建。");
  }, [savedBuild.data, serverDraft?.draft_id, currentInput, hydratedBuild]);
  const save = useMutation({
    onError: () => setNotice(""),
    mutationFn: async () => {
      if (!requireName()) throw new Error("请先填写环境名称。");
      if (samplingErrors.length) { setStep(4); setShowPlots(false); throw new Error(samplingErrors.join(" ")); }
      const submittedInput = currentInput;
      const draft = serverDraft
        ? apiClient.updateDraft(
            serverDraft,
            value.name,
            toEnvironmentPayload(value, buildParameters, acquisition, sources),
          )
        : apiClient.createDraft(
            "environment",
            value.name,
            toEnvironmentPayload(value, buildParameters, acquisition, sources),
          );
      return { draft: await draft, submittedInput };
    },
    onSuccess: async ({ draft, submittedInput }) => {
      const editedWhileSaving = currentInputRef.current !== submittedInput;
      setServerDraft(draft);
      // The response acknowledges the submitted revision, not subsequent edits.
      setValue(current => editedWhileSaving
        ? { ...current, id: draft.draft_id, updatedAt: draft.updated_at }
        : fromEnvironmentDraft(draft));
      setNotice(editedWhileSaving
        ? "上次提交的内容已保存；保存期间的新修改仍保留在页面，请再次保存。"
        : "草稿已保存到服务器工作区");
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
        maximumDepthMeters: !current.soundSpeedProfile.length && !current.bathymetry.length ? availableDepth : current.maximumDepthMeters,
        soundSpeedProfile: points.map((x) => ({
          depthMeters: Number(x.depth_meters),
          speedMetersPerSecond: Number(x.speed_meters_per_second),
        })),
      }));
      setNotice(result.result_cache_reused ? `已复用本地 ${result.provider} 剖面，共 ${points.length} 个采样点；没有重新下载。` : `已从 ${result.provider} 获取 ${points.length} 个水文采样点`);
      void client.invalidateQueries({ queryKey: ["hydrology-cache"] });
      setWoaProgress({ stage: "completed", message: `已获取 ${points.length} 个声速采样点${result.cache_files_reused === 2 ? "，使用已缓存的 NOAA 月度数据" : ""}` });
      setStep(4);
    },
  });
  const importFile = async (file: File) => {
    setImportError(null);
    try {
    const result = await apiClient.importSsp(file.name, await file.text());
    setSources(current => ({ ...current, ssp: { source: "file", filename: file.name, cache_id: result.cache_id } }));
      const points = result.sound_speed_profile as Array<any>;
      setValue({
        ...value,
        maximumDepthMeters: value.bathymetry.length ? Math.max(...value.bathymetry.map(x => x.depthMeters)) : Math.max(...points.map(x => Number(x.depth_meters))),
        soundSpeedProfile: points.map((x) => ({
          depthMeters: Number(x.depth_meters),
          speedMetersPerSecond: Number(x.speed_meters_per_second),
        })),
      });
    setNotice(`${file.name} 已解析并通过基础检查`);
    void client.invalidateQueries({ queryKey: ["hydrology-cache"] });
    } catch (error) { setImportError(error); }
  };
  const startBuild = useMutation({
    onError: () => setNotice(""),
    mutationFn: async () => {
      if (!requireName()) throw new Error("请先填写环境名称。");
      if (buildErrors.length) { setStep(4); setShowPlots(false); throw new Error(buildErrors.join(" ")); }
      setNotice("正在保存输入并计算声场…");
      setBuiltInput(currentInput);
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
      loadedBuild.current = result.build_id;
      setHydratedBuild(result.build_id);
      client.setQueryData(["environment-build", result.build_id], result);
      setBuild(result);
      setParams(current => { const next = new URLSearchParams(current); next.set("build", result.build_id); next.set("draft", result.draft_id); next.delete("source"); return next; }, { replace: true });
      setStep(result.state === "Completed" ? 5 : 4);
      setShowPlots(result.state === "Completed");
      setNotice(result.state === "Completed" ? "声场计算完成，可以发布。" : "");
    },
  });
  const publish = useMutation({
    onError: () => setNotice(""),
    mutationFn: () => apiClient.publishEnvironmentBuild(build!.build_id),
    onSuccess: async (artifact) => {
      setPublishedBuildId(build!.build_id);
      setNotice(`“${value.name}”已发布，可在实验工作台选择。`);
      await client.invalidateQueries({ queryKey: ["environments"] });
    },
  });
  if (drafts.error || source.error || savedBuild.error) return <ErrorState error={drafts.error || source.error || savedBuild.error} />;
  if (drafts.isPending || (requestedBuild && savedBuild.isPending) || (sourceId && (source.isPending || loadedSource.current !== sourceId))) return <LoadingState />;
  const sspMaximumDepth = value.soundSpeedProfile.length ? Math.max(...value.soundSpeedProfile.map((point) => point.depthMeters)) : 0;
  const flatBoundary = sources.bottom_boundary?.kind === "ideal-flat-bottom";
  const terrainMaximumDepth = flatBoundary ? value.maximumDepthMeters : value.bathymetry.length ? Math.max(...value.bathymetry.map((point) => point.depthMeters)) : 0;
  const terrainMaximumRange = flatBoundary ? value.maximumRangeMeters : value.bathymetry.length ? Math.max(...value.bathymetry.map((point) => point.rangeMeters)) : 0;
  const hasEnvironmentSamples = value.soundSpeedProfile.length >= 2 && (flatBoundary || value.bathymetry.length >= 2) && terrainMaximumDepth > 0 && terrainMaximumRange > 0;
  const profileCoversTerrain = hasEnvironmentSamples && sspMaximumDepth >= terrainMaximumDepth;
  // Saved JSON may have alphabetically sorted keys. Compare scientific query
  // fields, not serialization order; a reopened cache must not look "changed".
  const staleSources = (["ssp"] as const).filter(kind => sources[kind]?.request && Object.entries(sourceQuery(acquisition, kind)).some(([key, expected]) => key !== "use_reference_cache" && sources[kind].request[key] !== expected));
  const inputReady = hasEnvironmentSamples && profileCoversTerrain && !staleSources.length;
  const matchesBuild = builtInput === currentInput;
  return (
    <div className="environment-builder classic-environment-page focused-environment" data-step={step} data-view={showPlots ? "plots" : "inputs"}>
      <section className="environment-classic-heading">
        <div><p className="eyebrow">Bellhop 环境管理</p><h1>环境建设</h1><span>选择水文剖面与声学计算范围，离线计算并保存水声信道。</span></div>
        <div className="environment-classic-state"><b>{drafts.data?.length ?? 0}</b><span>服务器草稿</span><small>{serverDraft ? `当前修订 ${serverDraft.revision}` : "当前未保存"}</small></div>
      </section>
      <section className="workspace-toolbar environment-classic-toolbar">
        <div>
          <small>{sourceId ? "已发布环境的输入 · 修改后另存新版本" : "环境建设 · 服务器草稿"}</small>
          <Link to="/environments">← 环境目录</Link>
        </div>
        <label>
          环境名称
          <input
            ref={nameInput} required placeholder="请输入环境名称" aria-invalid={!!nameError}
            value={value.name}
            onChange={(event) => { setNameError(""); setValue({ ...value, name: event.target.value }); }}
          />
        </label>
        <button
          type="button"
          onClick={() => { if (requireName()) { setNotice("正在保存…"); save.mutate(); } }}
          disabled={save.isPending || startBuild.isPending}
        >
          {save.isPending ? "正在保存…" : "保存草稿"}
        </button>
        {build?.state === "Completed" && <button className="primary-button" disabled={publish.isPending || startBuild.isPending || !matchesBuild || publishedBuildId === build.build_id} onClick={() => { if (requireName()) { setNotice("正在发布…"); publish.mutate(); } }}>{publish.isPending ? "正在发布…" : publishedBuildId === build.build_id ? "已发布" : "发布为正式环境"}</button>}
      </section>
      <div className="environment-action-feedback" aria-live="polite">
        {nameError && <p role="alert">{nameError}</p>}
        {notice && <p role="status">{notice}</p>}
        {(save.error || startBuild.error || publish.error) && <ErrorState error={save.error || startBuild.error || publish.error} />}
        {build?.state === "Failed" && <p role="alert">本次计算未完成：{build.failure?.message || "服务未返回具体原因，请重试或查看计算记录。"} 请在下方修改参数后重新计算。</p>}
        {build?.state === "Completed" && !matchesBuild && <p>输入已修改，请重新计算后发布。</p>}
      </div>
      {(sourceId || build?.state === "Completed") && <div className="environment-view-tabs"><button aria-pressed={!showPlots} onClick={() => setShowPlots(false)}>输入与计算参数</button><button aria-pressed={showPlots} onClick={() => setShowPlots(true)}>信道计算结果</button></div>}
      {showPlots && (sourceId || build?.state === "Completed") && <AcousticPlots key={build?.build_id ?? sourceId} environmentId={sourceId || undefined} buildId={build?.state === "Completed" ? build.build_id : undefined} />}
      <div className="builder-layout">
        <aside>
          <StepRail current={step} select={setStep} />
          <div className="plane-note">
            <strong>离线建设</strong>
            <span>
              获取或复用水文，计算声场并发布。实验绑定发布版本，不随草稿修改。
            </span>
          </div>
        </aside>
        <main className="builder-main">
          <section className="builder-section">
            <div className="section-number">01</div>
            <div>
              <h2>定义海域与用途</h2>
              <p>{flatBoundary ? "计算采用平坦海底，非实测地形。" : "此历史环境保留原有海底边界。"}</p>
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
                  计算水深（m，不超过声速剖面）
                  <input
                    type="number"
                    readOnly={!flatBoundary}
                    value={value.maximumDepthMeters}
                    onChange={e => setValue({ ...value, maximumDepthMeters: Number(e.target.value) })}
                  />
                </label>
                <label>
                  声场计算距离（m）
                  <input
                    type="number"
                    readOnly={!flatBoundary}
                    value={value.maximumRangeMeters}
                    onChange={e => setValue({ ...value, maximumRangeMeters: Number(e.target.value) })}
                  />
                </label>
              </div>
            </div>
          </section>
          <section className="builder-section">
            <div className="section-number">02</div>
            <div>
              <h2>选择水文来源</h2>
              <div className="hydrology-reuse"><label>已保存的水文剖面<select value={selectedProfile} onChange={event => setSelectedProfile(event.target.value)}><option value="">选择已获取或已导入的数据</option>{(hydrologyCache.data ?? []).map((item: any) => <option key={item.cache_id} value={item.cache_id}>{item.filename || item.provider || "已保存剖面"} · {item.selection ? `${item.selection.latitude_deg}°, ${item.selection.longitude_deg}° · ${item.selection.month ? `${item.selection.month}月` : "年平均"} · ` : ""}{item.maximum_depth_meters} m · {item.point_count}点</option>)}</select></label><button type="button" disabled={!selectedProfile || reuseProfile.isPending} onClick={() => reuseProfile.mutate(selectedProfile)}>使用所选水文</button><small>获取按钮先查本地剖面和月度原始文件；相同内容不重复保存。</small>{(reuseProfile.error || hydrologyCache.error) && <ErrorState error={reuseProfile.error || hydrologyCache.error} />}</div>
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
                  <label>纬度（°）<input type="number" min="-90" max="90" value={acquisition.latitudeDeg} onChange={(e) => setAcquisition({ ...acquisition, latitudeDeg: Number(e.target.value) })} /></label>
                  <label>经度（°）<input type="number" min="-180" max="180" value={acquisition.longitudeDeg} onChange={(e) => setAcquisition({ ...acquisition, longitudeDeg: Number(e.target.value) })} /></label>
                  <label>月份<input type="number" min="1" max="12" value={acquisition.month ?? ""} placeholder="留空为年平均" onChange={(e) => setAcquisition({ ...acquisition, month: e.target.value === "" ? null : Number(e.target.value) })} /></label>
                  <label>请求 SSP 深度上限（m）<input type="number" min="1" value={acquisition.requestedSspDepthMeters} onChange={(e) => setAcquisition({ ...acquisition, requestedSspDepthMeters: Number(e.target.value) })} /></label>
                </div>
                {!acquisition.useReferenceCache && capabilities.data?.woa_online_available === false && <p role="status">{String(capabilities.data.woa_unavailable_reason)}</p>}
                <div className="acquisition-actions"><button type="button" onClick={() => woa.mutate(sourceQuery(acquisition, "ssp"))} disabled={woa.isPending || (!acquisition.useReferenceCache && capabilities.data?.woa_online_available === false)}>{woa.isPending ? "正在获取…" : acquisition.useReferenceCache ? "载入参考声速剖面" : "获取 WOA 数据"}</button>{woa.isPending && <button type="button" onClick={() => woaAbort.current?.abort()}>取消获取</button>}</div>
                {!acquisition.useReferenceCache && <p className="acquisition-hint">首次获取某个月份需下载温度、盐度数据，可能需要数分钟。下载后，更换经纬度会直接使用该月缓存提取剖面。</p>}
                {woaProgress && <div className="acquisition-progress" role="status" aria-live="polite"><strong>{woaProgress.message}</strong>{woa.isPending && <span>已等待 {woaElapsed} 秒{woaProgress.dataset ? ` · ${woaProgress.dataset}（${woaProgress.file_index}/${woaProgress.file_count}）` : ""}</span>}{woa.isPending && woaProgress.received_bytes !== undefined && <><span>{(woaProgress.received_bytes / 1048576).toFixed(1)} MB{woaProgress.total_bytes ? ` / ${(woaProgress.total_bytes / 1048576).toFixed(1)} MB` : " · 总大小由服务端决定"}</span><progress aria-label="WOA 文件下载进度" max={woaProgress.total_bytes ?? undefined} value={woaProgress.total_bytes ? woaProgress.received_bytes : undefined} /></>}</div>}
                {woa.error && <ErrorState error={woa.error} />}
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
                      void importFile(e.target.files[0])
                    }
                  />
                </label>
              )}
              {importError != null && <ErrorState error={importError} />}
            </div>
          </section>
          <section className="builder-section preview-section">
            <div className="section-number">03</div>
            <div>
              <h2>水文预览与计算范围</h2>
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
                  <b>{flatBoundary ? "平坦假设" : value.bathymetry.length ? "历史输入" : "未确认"}</b> 计算边界
                </span>
                <span>
                  <b>{value.maximumDepthMeters} m</b> 深度
                </span>
                <span>
                  <b>{value.maximumRangeMeters / 1000} km</b> 距离
                </span>
              </div>
              <div className={`coverage-check ${profileCoversTerrain ? "valid" : "invalid"}`}><strong>数据覆盖检查</strong><span>声速剖面至 {sspMaximumDepth} m · 计算水深 {terrainMaximumDepth} m · 距离 {terrainMaximumRange} m</span><p>{value.soundSpeedProfile.length < 2 ? "请先获取至少两个声速采样点。" : !hasEnvironmentSamples ? "水文剖面已就绪，请确认计算边界、水深及范围。" : profileCoversTerrain ? "声速剖面覆盖计算水深，可进入 Bellhop 构建。" : "声速剖面未覆盖计算水深。请减小水深或获取更深的水文数据；不会静默外推。"}</p></div>
              {flatBoundary && <div className="form-grid"><label>计算水深（m）<input type="number" min="1" max={sspMaximumDepth || undefined} value={value.maximumDepthMeters} onChange={event => setValue({ ...value, maximumDepthMeters: Number(event.target.value) })} /></label><label>计算距离（m）<input type="number" min="1" value={value.maximumRangeMeters} onChange={event => setValue({ ...value, maximumRangeMeters: Number(event.target.value) })} /></label></div>}
              {step < 4 && <button onClick={() => setStep(4)}>配置与预览</button>}
            </div>
          </section>
          <section className="builder-section">
            <div className="section-number">03</div>
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
                <DepthSamplingInput label="发送" value={buildParameters.sourceSampling} waterDepth={waterDepth} onChange={sourceSampling => setBuildParameters(current => ({ ...current, sourceSampling }))} />
                <DepthSamplingInput label="接收" value={buildParameters.receiverSampling} waterDepth={waterDepth} onChange={receiverSampling => setBuildParameters(current => ({ ...current, receiverSampling }))} />
                <label>距离采样数<input type="number" min="2" max="1000" value={buildParameters.rangeSampleCount} onChange={e => setBuildParameters({ ...buildParameters, rangeSampleCount: Number(e.target.value) })} /></label>
                <p className="sampling-summary">距离 0～{value.maximumRangeMeters} m{Number.isInteger(buildParameters.rangeSampleCount) && buildParameters.rangeSampleCount >= 2 ? ` · 间隔 ${Number((value.maximumRangeMeters / (buildParameters.rangeSampleCount - 1)).toPrecision(5))} m` : ""}。包含两端，点数不是节点数。</p>
                <section className="sampling-budget" aria-label="计算规模检查"><strong>计算规模</strong><p>{sourceSampling.points.length} 个发射深度 × {receiverSampling.points.length} 个接收深度 × {Number.isFinite(buildParameters.rangeSampleCount) ? buildParameters.rangeSampleCount : "—"} 个距离点{budget.cells > 0 ? ` = ${budget.cells.toLocaleString("en-US")} 个位置` : ""}</p>
                  {buildErrors.length > 0 && <ul role="alert">{buildErrors.map(error => <li key={error}>{error}</li>)}</ul>}
                  {budget.errors.length > 0 && !samplingErrors.length && budget.maxRanges >= 2 && <div><p>保持深度采样不变，可将距离点数改为 {budget.maxRanges}（间隔约 {Number((value.maximumRangeMeters / (budget.maxRanges - 1)).toPrecision(4))} m）。</p><button type="button" onClick={() => setBuildParameters(current => ({ ...current, rangeSampleCount: budget.maxRanges }))}>距离点数改为 {budget.maxRanges}</button></div>}
                  <small>平台当前上限 200,000 个采样位置，并非 Bellhop 极限。实际文件大小还取决于多径数量；点数不代表精度。</small>
                </section>
                {capabilities.data?.bellhop_available === false && <p role="status">本机未配置 Bellhop 离线计算程序。可以保存草稿，但不会用参考声场代替计算。</p>}
                <details className="bellhop-advanced"><summary>海底材料与声线计算参数</summary><p>海底材料是模型配置，不来自 WOA。以下保留当前设置，修改后需重新计算。</p><div className="form-grid">
                <label>海底声速（m/s）<input type="number" value={buildParameters.bottomSoundSpeed} onChange={(event) => setBuildParameters({...buildParameters, bottomSoundSpeed: Number(event.target.value)})} /></label>
                <label>海底密度（g/cm³）<input type="number" step="0.1" value={buildParameters.bottomDensity} onChange={(event) => setBuildParameters({...buildParameters, bottomDensity: Number(event.target.value)})} /></label>
                <label>海底衰减（dB/λ）<input type="number" step="0.1" value={buildParameters.bottomAttenuation} onChange={(event) => setBuildParameters({...buildParameters, bottomAttenuation: Number(event.target.value)})} /></label>
                <label>波束数<input type="number" min="1" value={buildParameters.beamCount} onChange={(event) => setBuildParameters({...buildParameters, beamCount: Number(event.target.value)})} /></label>
                <label>最小发射角（°）<input type="number" value={buildParameters.minimumAngle} onChange={(event) => setBuildParameters({...buildParameters, minimumAngle: Number(event.target.value)})} /></label>
                <label>最大发射角（°）<input type="number" value={buildParameters.maximumAngle} onChange={(event) => setBuildParameters({...buildParameters, maximumAngle: Number(event.target.value)})} /></label>
                </div><p>波束数是计算使用的发射方向数量；角度以水平为 0°，负角朝海面、正角朝海底。</p></details>
              </div>
              <p>按当前声速、计算边界和底质重新计算声场。声速在采样点之间线性插值，不外推。发布后可在实验中选择。</p>
              <button
                type="button"
                disabled={!inputReady || buildErrors.length > 0 || startBuild.isPending || save.isPending || capabilities.data?.bellhop_available === false}
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
                </div>
              )}
            </section>
          )}
          {!build && step === 5 && <section className="build-console"><h2>计算与发布</h2><p>当前尚未计算声场。请检查采样覆盖和 Bellhop 参数后开始计算。</p><button onClick={() => setStep(4)}>设置计算参数</button></section>}
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
        fusionCenterNodeId: null,
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
  executionMode: "PlanningV1",
  planningCycleDurationNs: "30000000000",
  adaptiveSafetyMarginNs: "0",
  flows: [],
  fusionReceiverNodeId: null,
  id: "",
  name: "未命名网络实验",
  sourceExperimentId: null,
  scenarioId: "",
  provenance: { kind: "new", label: "新建" },
  routingMode: "Direct",
  macMode: "RegisteredMac",
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
  const algorithmCatalog = useQuery({ queryKey: ["algorithms"], queryFn: apiClient.algorithmCatalog, retry: false, staleTime: 30000 });
  const capabilities = useQuery({ queryKey: ["authoring-capabilities"], queryFn: apiClient.authoringCapabilities, retry: false });
  const { context: editorContext, draftId: requestedDraftId, scenarioId: initialScenarioId } = useExperimentIdentity();
  const scenarios = useQuery(queries.scenarios());
  const environments = useQuery(queries.environments());
  const trajectories = useQuery(queries.trajectories());
  const environmentNames = useQuery(queries.environmentSummaries());
  const [inspectorSection, setInspectorSection] = useState("basic");
  const [targetExpanded, setTargetExpanded] = useState(false);
  const scenarioDrafts = useQuery(queries.drafts("scenario"));
  const experimentDrafts = useQuery({ ...queries.drafts("experiment"), enabled: Boolean(requestedDraftId) });
  const client = useQueryClient();
  const navigate = useNavigate();
  const [value, setValue] = useState(() => ({ ...experimentInitial, scenarioId: initialScenarioId ?? "" }));
  const planning = value.executionMode === "PlanningV1";
  const [server, setServer] = useState<AuthoringDraftDto | null>(null);
  useEffect(() => {
    if (requestedDraftId || server || value.macMode !== "RegisteredMac" || value.macAlgorithm) return;
    const installed = algorithmCatalog.data?.algorithms.find(a => a.id === "platform.adaptive-tdma");
    if (installed) setValue(current => ({ ...current, macAlgorithm: algorithmDefaults(installed) }));
  }, [requestedDraftId, server, value.macMode, value.macAlgorithm, algorithmCatalog.data]);
  const installedAlgorithm = value.macAlgorithm && algorithmCatalog.data?.algorithms.find(a => algorithmKey(a) === algorithmKey(value.macAlgorithm!));
  const algorithmErrors = value.macMode === "RegisteredMac" ? algorithmIssues(value.macAlgorithm, algorithmCatalog.data) : [];
  const [scene, setScene] = useState<ScenarioDraft>(() => scenarioInitial([], ""));
  const selection = editorContext?.selection;
  const reportResource = editorContext?.reportResource;
  useEffect(() => { reportResource?.(scene.environmentAssetId); }, [reportResource, scene.environmentAssetId]);
  useEffect(() => {
    if (selection) setScene(s => ({ ...s, environmentAssetId: selection.id }));
  }, [selection]);
  const sceneEnvironmentInputs = useQuery({ queryKey: ["environment-inputs", scene.environmentAssetId],
    queryFn: () => apiClient.environmentInputs(scene.environmentAssetId), enabled: !!scene.environmentAssetId,
    staleTime: Infinity });
  const initialSceneSignature = useRef(JSON.stringify([scene.name, scene.environmentAssetId, scene.nodes, scene.trajectoryBindings, scene.spatialDomain]));
  const [sceneServer, setSceneServer] = useState<AuthoringDraftDto | null>(null);
  const [sceneBusinessReset, setSceneBusinessReset] = useState(false);
  const [pendingSceneId, setPendingSceneId] = useState<string | null>(null);
  const [sceneSwitching, startSceneTransition] = useTransition();
  const switchScene = (id: string) => {
    setPendingSceneId(null);
    startSceneTransition(() => {
      setValue(current => ({ ...current, scenarioId: id, flows: [], fusionReceiverNodeId: null }));
      loadedScene.current = ""; setSceneServer(null); setSceneBusinessReset(true);
      if (!id) setScene(scenarioInitial([], ""));
    });
  };
  const loadedScene = useRef("");
  const loadedExperiment = useRef("");
  const [savedSignature, setSavedSignature] = useState("");
  const [physicalLayer, setPhysicalLayer] = useState<"Channel1Abstract" | "Channel2Waveform">("Channel1Abstract");
  const [pairedRun, setPairedRun] = useState(false);
  useEffect(() => { if (value.workload !== "CommunicationOnly" || !planning) setPairedRun(false); }, [value.workload, planning]);
  const [waveformModulation, setWaveformModulation] = useState<"Bpsk" | "Bfsk" | "Osdm">("Bpsk");
  const [osdm, setOsdm] = useState<OsdmConfig | null>(null);
  const [waveformNoise, setWaveformNoise] = useState<"None" | "Awgn">("None");
  const [waveformNoisePsd, setWaveformNoisePsd] = useState(0);
  const [slotDurationNs, setSlotDurationNs] = useState("6000000000");
  const [equivalentNoise, setEquivalentNoise] = useState(45);
  const [target, setTarget] = useState({ x: 200, y: 150 });
  const set = <K extends keyof ExperimentDraft>(
    key: K,
    val: ExperimentDraft[K],
  ) => setValue(current => {
    const next = { ...current, [key]: val };
    if (key === "flows") next.flows = bindFlowIdentities(next.flows ?? [], next.macAlgorithm?.interface === "planning-mac-v2");
    return next;
  });
  const selectedScenarioDraft = scenarioDrafts.data?.find((item) => item.draft_id === value.scenarioId);
  const sceneEnvironment = environments.data?.find(item => item.environment_asset_id === scene.environmentAssetId);
  useEffect(() => {
    if (!selectedScenarioDraft || loadedScene.current === selectedScenarioDraft.draft_id) return;
    loadedScene.current = selectedScenarioDraft.draft_id;
    const p = selectedScenarioDraft.payload;
    setSceneServer(selectedScenarioDraft);
    setScene({ kind: "scenario", id: selectedScenarioDraft.draft_id, name: selectedScenarioDraft.name,
      environmentAssetId: String(p.environment_asset_id ?? ""), fusionCenterNodeId: p.fusion_center_node_id == null ? null : String(p.fusion_center_node_id),
      nodes: structuredClone(p.nodes as ScenarioDraft["nodes"] ?? []),
      trajectoryBindings: structuredClone(p.trajectory_bindings as ScenarioDraft["trajectoryBindings"] ?? []),
      spatialDomain: structuredClone(p.spatial_domain as ScenarioDraft["spatialDomain"] ?? null),
      provenance: { kind: "new", label: "新建" }, updatedAt: selectedScenarioDraft.updated_at });
  }, [selectedScenarioDraft]);
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
      executionMode: source.execution_mode == null ? undefined : String(source.execution_mode),
      planningCycleDurationNs: text("planning_cycle_duration_ns", "30000000000"),
      adaptiveSafetyMarginNs: String((source.adaptive_tdma as { safety_margin_ns?: string } | null)?.safety_margin_ns ?? "0"),
      macAlgorithm: structuredClone(source.mac_algorithm as ExperimentDraft["macAlgorithm"]),
      flows: structuredClone(source.flows as ExperimentDraft["flows"] ?? []),
      fusionReceiverNodeId: source.fusion_receiver_node_id == null ? null : String(source.fusion_receiver_node_id),
      id: selected.draft_id,
      name: selected.name,
      scenarioId: text("scenario_id", ""),
      routingMode: text("routing_mode", source.execution_mode === "PlanningV1" ? "Direct" : "DirectToFusionCenter"),
      macMode: text("mac_mode", source.execution_mode === "PlanningV1" ? "AdaptiveTdma" : "Tdma"),
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
    const savedModem = text("waveform_modulation", "Bpsk");
    setWaveformModulation(savedModem === "Osdm" ? "Osdm" : savedModem === "Bfsk" ? "Bfsk" : "Bpsk");
    if (source.osdm && typeof source.osdm === "object") setOsdm(source.osdm as typeof osdm);
    setWaveformNoise(text("waveform_noise", "None") === "Awgn" ? "Awgn" : "None");
    setWaveformNoisePsd(number("waveform_noise_psd_db_re_1upa2_per_hz", 0));
    setSlotDurationNs(text("slot_duration_ns", "6000000000"));
    setEquivalentNoise(number("equivalent_noise_power_db_re_1upa2", 45));
    setTarget({ x: number("target_x_m", 200), y: number("target_y_m", 150) });
  }, [experimentDrafts.data, requestedDraftId, server?.draft_id]);
  const payload: Record<string, unknown> = {
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
    waveform_modulation: physicalLayer === "Channel1Abstract" ? "Bpsk" : waveformModulation,
    waveform_noise: waveformNoise,
    waveform_noise_psd_db_re_1upa2_per_hz: waveformNoisePsd,
    slot_duration_ns: slotDurationNs,
    equivalent_noise_power_db_re_1upa2: equivalentNoise,
    target_x_m: target.x,
    target_y_m: target.y,
  };
  delete payload.osdm;
  if (physicalLayer === "Channel2Waveform" && waveformModulation === "Osdm") payload.osdm = osdm;
  if (planning) {
    payload.execution_mode = "PlanningV1";
    payload.planning_cycle_duration_ns = value.planningCycleDurationNs;
    delete payload.network_update_interval_cycles;
    delete payload.mac_algorithm;
    if (value.macMode === "AdaptiveTdma") {
      payload.adaptive_tdma = { safety_margin_ns: value.adaptiveSafetyMarginNs };
      delete payload.slot_duration_ns;
      delete payload.guard_interval_ns;
    } else delete payload.adaptive_tdma;
    if (value.macMode === "RegisteredMac") {
      payload.mac_algorithm = value.macAlgorithm;
      delete payload.slot_duration_ns;
      delete payload.guard_interval_ns;
    }
    if (value.workload === "CommunicationOnly") {
      payload.flows = value.macAlgorithm?.interface === "planning-mac-v2" ? value.flows ?? [] : bindFlowIdentities(value.flows ?? [], false);
      for (const field of ["fusion_receiver_node_id", "minimum_bearing_points", "maximum_fusion_period_ns", "target_x_m", "target_y_m"]) delete payload[field];
    } else {
      payload.fusion_receiver_node_id = value.fusionReceiverNodeId ?? null;
      delete payload.flows;
    }
  }
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
    if (requestedDraftId && server?.draft_id !== requestedDraftId) throw new Error("原实验尚未读取完成，未创建替代实验。");
    if (physicalLayer === "Channel2Waveform" && waveformModulation === "Osdm" && !osdm) throw new Error("请先读取或选择完整 OSDM 配置。");
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
      throw new Error(`节点场景已保存（修订 ${savedScene.revision}）；实验保存失败，未启动运行。请保留本页修改并重试。`, { cause: error });
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
      const positiveIntegers = [value.bitRateBitsPerSecond, value.simulationCycleCount, value.networkUpdateIntervalCycles, ...(value.macMode === "Tdma" ? [slotDurationNs] : [])];
      if (!positiveIntegers.every(text => /^[1-9]\d*$/.test(text))) throw new Error("速率、周期数、网络刷新间隔与时隙时长必须是有效正数。");
      if (value.macMode === "Tdma" && (!/^\d+$/.test(value.guardIntervalNs) || BigInt(value.guardIntervalNs) >= BigInt(slotDurationNs))) throw new Error("保护时间必须非负且小于一个时隙。");
      if (!/^\d+$/.test(value.deterministicSeed) || BigInt(value.deterministicSeed) > 18446744073709551615n) throw new Error("随机种子必须是 0～18446744073709551615 之间的整数。");
      if (![value.centerFrequencyHz, value.occupiedBandwidthHz, value.sourceLevelDb, equivalentNoise, waveformNoisePsd, target.x, target.y].every(Number.isFinite)
        || value.centerFrequencyHz <= 0 || value.occupiedBandwidthHz <= 0) throw new Error("设备与噪声参数必须是有限数值，频率和带宽必须大于零。");
      if (algorithmErrors.length) throw new Error(algorithmErrors.join("；"));
      const { saved, savedScene } = await saveComposition();
      if (savedScene.validation_issues.length) throw new Error(`节点场景已保存，但尚不能运行：${savedScene.validation_issues.join("；")}`);
      if (saved.validation_issues.length) throw new Error(saved.validation_issues.join("；"));
      if (!scene.nodes.length) throw new Error("实验草稿已保存；场景至少需要一个节点，未启动运行。");
      if (!planning && (scene.nodes.length < 3 || scene.nodes.length > 6)) throw new Error("实验草稿已保存；历史执行方式仅支持 3～6 个节点，未启动运行。不会自动补充或删减节点。");
      const planningIssues = planningConfigurationIssues(value, scene.nodes);
      if (planningIssues.length) throw new Error(`实验已保存，运行前检查：${planningIssues.join("；")}`);
      const coverage = trajectoryRunCoverage(scene, trajectories.data ?? [], value.simulationCycleCount,
        slotDurationNs, value.macMode, planning ? value.planningCycleDurationNs : undefined);
      if (coverage.issues.length) throw new Error(`实验已保存，运行前检查：${coverage.issues.join("；")}`);
      if (pairedRun) return apiClient.createRunGroup(saved.draft_id, saved.revision, `pair-${crypto.randomUUID()}`);
      return apiClient.createAuthoringRun(saved.draft_id, saved.revision);
    },
    onSuccess: async (created) => {
      await client.invalidateQueries({ queryKey: ["runs"] });
      navigate("group_id" in created ? `/run-groups/${created.group_id}` : `/runs/${created.run_id}?presentation=live`);
    },
  });
  useEffect(() => { editorContext?.registerSave(save.mutateAsync); });
  useEffect(() => { editorContext?.reportState(signature !== savedSignature, save.isPending || run.isPending); });
  if (scenarios.isPending || environments.isPending || trajectories.isPending || scenarioDrafts.isPending || (requestedDraftId && experimentDrafts.isPending)) return <LoadingState />;
  const loadError = scenarios.error || environments.error || trajectories.error || scenarioDrafts.error || (requestedDraftId && experimentDrafts.error);
  if (loadError) return <ErrorState error={loadError} />;
  if (selectedScenarioDraft?.payload.environment_source === "Recorded") return <p role="alert">录音链路不支持 Bellhop 几何配置。请从工作台打开原模式 C 实验。<Link to="/">返回工作台</Link></p>;
  if (requestedDraftId && !experimentDrafts.data?.some(draft => draft.draft_id === requestedDraftId)) return <p role="alert">这个实验草稿不存在，未改用其他案例。<Link to="/workspace/experiment">新建实验</Link></p>;
  if (requestedDraftId && server?.draft_id !== requestedDraftId) return <LoadingState />;
  const coverage = trajectoryRunCoverage(scene, trajectories.data ?? [], value.simulationCycleCount,
    slotDurationNs, value.macMode, planning ? value.planningCycleDurationNs : undefined);
  const savedIssues = [
    ...(sceneServer?.validation_issues ?? []).map(issue => `节点场景：${issue}`),
    ...(server?.validation_issues ?? []).map(issue => `实验配置：${issue}`),
  ];
  const needsBusinessEndpoints = value.workload === "BearingFusion"
    ? !scene.nodes.some(node => node.node_id === value.fusionReceiverNodeId && node.can_receive)
    : !(value.flows?.length) || value.flows.some(flow => flow.source_node_id === flow.destination_node_id
      || !scene.nodes.some(node => node.node_id === flow.source_node_id && node.can_transmit)
      || !scene.nodes.some(node => node.node_id === flow.destination_node_id && node.can_receive));
  const inspector = (
        <div className="experiment-inspector" data-section={inspectorSection}>
          <div className="studio-pane-title"><strong>环境与实验参数</strong></div>
          <nav className="inspector-tabs" aria-label="实验参数分组"><button type="button" className={inspectorSection === "basic" ? "selected" : ""} onClick={() => setInspectorSection("basic")}>常用设置</button>
            <details className="research-navigation"><summary>研究设置</summary>{[["network", "网络"], ["phy", "物理模式"], ["execution", "运行"]].map(([id, label]) => <button type="button" key={id} className={id === inspectorSection ? "selected" : ""} onClick={() => setInspectorSection(id)}>{label}</button>)}</details>
          </nav>
          {planning && sceneBusinessReset && needsBusinessEndpoints && <p className="property-help" role="status">场景已切换，通信流和应用接收点已清空。请在“网络”中为新场景重新指定业务端点。</p>}
          {value.executionMode !== undefined && !planning && <p role="alert">此草稿的执行模式不受当前工作台支持，不能按历史模式运行。可以新建实验重新配置。</p>}
          <section id="basic" className="panel form-panel">
            <h2>场景与资源</h2>
            <label>场景名称<input value={scene.name} onChange={e => setScene({ ...scene, name: e.target.value })} /></label>
            <label>
              使用已有节点场景
              <select
                value={value.scenarioId}
                onChange={(e) => {
                  const sceneChanged = JSON.stringify([scene.name, scene.environmentAssetId, scene.nodes, scene.trajectoryBindings, scene.spatialDomain]) !== initialSceneSignature.current;
                  if (signature !== savedSignature && (sceneServer || sceneChanged)) { setPendingSceneId(e.target.value); return; }
                  switchScene(e.target.value);
                }}
              >
                <option value="">新建节点场景</option>
                {scenarioDrafts.data.filter(x => x.payload.environment_source !== "Recorded").map((x) => (
                  <option value={x.draft_id} key={x.draft_id}>
                    {x.name} · 修订 {x.revision}
                  </option>
                ))}
              </select>
            </label>
            {sceneSwitching && <p role="status">正在切换场景…</p>}
            {!sceneSwitching && value.scenarioId && !sceneServer && <p role="alert">当前场景引用无法读取，请重新选择。</p>}
            {sceneEnvironmentInputs.isFetching && <p role="status">正在读取场景环境…</p>}
            {environmentNames.error && <ErrorState error={environmentNames.error} />}
            <details className="resource-binding-details"><summary>场景环境绑定详情</summary><label>实验环境<select value={scene.environmentAssetId} onChange={e => setScene({ ...scene, environmentAssetId: e.target.value })}>
              <option value="">请选择已发布环境</option>{environments.data!.map((environment, index) => <option key={environment.environment_asset_id} value={environment.environment_asset_id}>{String((environmentNames.data?.[environment.environment_asset_id] as { name?: string } | undefined)?.name ?? (environment.environment_asset_id === "reference-shallow-water-v1" ? "南海浅水参考环境" : `已发布环境 ${index + 1}`))} · v{environment.asset_format_version}</option>)}
            </select></label></details>
            <p className="property-help">环境在上方选择；点击节点设置轨迹和收发能力。</p>
            {scene.spatialDomain && <p className="property-help">此历史场景保留原边界校验，以免改变已有实验。地形关联编辑已冻结；新场景不启用该功能。</p>}
            <h3>实验任务与通信方案</h3>
            <label>实验任务<select value={value.workload} onChange={e => set("workload", e.target.value as ExperimentDraft["workload"])}><option value="CommunicationOnly">通信性能实验</option><option value="BearingFusion">方位探测与定位</option></select></label>
            <p className="property-help">{installedAlgorithm?.label ?? (value.macMode === "AdaptiveTdma" ? "自适应 TDMA" : value.macMode === "Tdma" ? "配置式 TDMA" : "已保存算法方案")} · {value.routingMode === "Direct" ? "直接发送" : value.routingMode === "ShortestPath" ? "最短路径" : "案例路由"}</p>
            {value.workload === "BearingFusion" && <p className="property-help">集中式方位交汇 · 至少 {value.minimumBearingPoints} 条有效观测。目标可在画布中调整。</p>}
            {needsBusinessEndpoints && <button type="button" onClick={() => setInspectorSection("network")}>配置必要的通信对象</button>}
            <label>通信接收<select value={physicalLayer} onChange={e => setPhysicalLayer(e.target.value as typeof physicalLayer)}><option value="Channel1Abstract">快速网络实验 · A</option><option value="Channel2Waveform">波形通信实验 · B</option></select></label>
            {physicalLayer === "Channel2Waveform" && <label>波形体制<select value={waveformModulation} onChange={e => setWaveformModulation(e.target.value as typeof waveformModulation)}><option value="Bpsk">单载波 BPSK</option><option value="Bfsk">单载波 BFSK</option><option value="Osdm" disabled={!planning}>OSDM</option></select></label>}
            {physicalLayer === "Channel2Waveform" && waveformModulation === "Osdm" && <p className="property-help">OSDM 完整配置随实验保存；参数见研究设置 → 物理模式。</p>}
            <label>运行周期数<input type="number" min="1" step="1" value={value.simulationCycleCount} onChange={e => set("simulationCycleCount", e.target.value)} /></label>
            <p className="duration-preview">{coverage.endNs === null ? "请检查周期配置" : `预计仿真 ${Number(coverage.endNs) / 1e9} s`} · {value.bitRateBitsPerSecond} bit/s</p>
          </section>
          <section id="network" className="panel form-panel">
            <h2>网络与协议</h2>
            {!planning && <><label>业务接收 / 融合节点<select value={scene.fusionCenterNodeId ?? ""} onChange={event => setScene({ ...scene, fusionCenterNodeId: event.target.value || null })}><option value="">未指定</option>{scene.nodes.map(node => <option key={node.node_id} value={node.node_id}>N{node.node_id}</option>)}</select></label><p className="property-help">保留此历史草稿的配置式执行方式。新建实验可使用显式通信流与自适应 TDMA；预置案例需从有轨迹的场景派生。</p></>}
            <div className="form-grid">
              <label>
                路由
                <select
                  value={value.routingMode}
                  onChange={(e) => set("routingMode", e.target.value)}
                >
                  {planning && <option value="Direct">直接发送到业务目的节点</option>}
                  {!planning && <option value="DirectToFusionCenter">直接发送到汇聚节点（兼容）</option>}
                  {planning && !["Direct", "ShortestPath"].includes(value.routingMode) && <option value={value.routingMode} disabled>当前路由不受支持，请重新选择</option>}
                  <option value="ShortestPath">{planning ? "最短路径到业务目的节点" : "最短路径到汇聚节点"}</option>
                </select>
              </label>
              <label>
                MAC
                <select
                  value={value.macMode === "RegisteredMac" ? value.macAlgorithm ? algorithmKey(value.macAlgorithm) : "RegisteredMac" : value.macMode}
                  onChange={(e) => {
                    const algorithm = algorithmCatalog.data?.algorithms.find(a => algorithmKey(a) === e.target.value);
                    setValue(current => algorithm ? { ...current, macMode: "RegisteredMac", macAlgorithm: algorithmDefaults(algorithm),
                      flows: bindFlowIdentities(current.flows ?? [], algorithm.interface === "planning-mac-v2") }
                      : { ...current, macMode: e.target.value, macAlgorithm: undefined, flows: bindFlowIdentities(current.flows ?? [], false) });
                  }}
                >
                  {planning && <optgroup label="已安装算法">{algorithmCatalog.data?.algorithms.map(a => <option key={algorithmKey(a)} value={algorithmKey(a)}>{a.label} · v{a.version}</option>)}</optgroup>}
                  {planning && value.macMode === "RegisteredMac" && !installedAlgorithm && <option value={value.macAlgorithm ? algorithmKey(value.macAlgorithm) : "RegisteredMac"} disabled>{value.macAlgorithm ? "原算法版本不可用" : algorithmCatalog.isError ? "算法目录不可用" : "正在加载算法"}</option>}
                  <optgroup label="历史实验兼容"><option value="Tdma">配置式 TDMA · 节点按时隙轮流发送</option>
                  {planning && <option value="AdaptiveTdma">自适应 TDMA · 历史配置兼容</option>}</optgroup>
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
              <p className="field-explanation workload-explanation">{value.workload === "CommunicationOnly" ? "统计网络收发，不进行定位。" : "节点发送方位报告，接收后用于定位。"}</p>
            </div>
            {planning && algorithmCatalog.isError && <p role="alert">算法目录加载失败。<button type="button" onClick={() => void algorithmCatalog.refetch()}>重试</button></p>}
            {planning && value.macMode === "RegisteredMac" && installedAlgorithm && value.macAlgorithm && <AlgorithmParameters algorithm={installedAlgorithm} value={value.macAlgorithm} onChange={selection => set("macAlgorithm", selection)} />}
            {planning && algorithmErrors.length > 0 && <p role="alert">{algorithmErrors.join("；")}</p>}
            {planning && value.workload === "CommunicationOnly" && <section className="parameter-detail" aria-label="显式通信流">
              <h3>通信流 · 源节点 → 目的节点</h3><p>双向通信添加两条相反方向的流。</p>
              {!value.flows?.length && <p className="property-help">请先添加通信流。</p>}
              {(value.flows ?? []).map((flow, index) => <div className="form-grid" key={index}>
                <label>源节点 {index + 1}<select value={flow.source_node_id} onChange={event => set("flows", value.flows!.map((item, i) => i === index ? { ...item, source_node_id: event.target.value } : item))}><option value="">请选择</option>{scene.nodes.map(node => <option key={node.node_id} value={node.node_id} disabled={!node.can_transmit}>N{node.node_id}{node.can_transmit ? "" : " · 不可发送"}</option>)}</select></label>
                <label>目的节点 {index + 1}<select value={flow.destination_node_id} onChange={event => set("flows", value.flows!.map((item, i) => i === index ? { ...item, destination_node_id: event.target.value } : item))}><option value="">请选择</option>{scene.nodes.map(node => <option key={node.node_id} value={node.node_id} disabled={!node.can_receive}>N{node.node_id}{node.can_receive ? "" : " · 不可接收"}</option>)}</select></label>
                <label>每周期包数 {index + 1}<input type="number" min="1" max="1000" step="1" value={Number.isFinite(flow.packets_per_cycle) ? flow.packets_per_cycle : ""} onChange={event => set("flows", value.flows!.map((item, i) => i === index ? { ...item, packets_per_cycle: Number(event.target.value) } : item))} /></label>
                <button type="button" onClick={() => set("flows", value.flows!.filter((_, i) => i !== index))}>删除通信流 {index + 1}</button>
              </div>)}
              <button type="button" onClick={() => set("flows", [...(value.flows ?? []), { source_node_id: "", destination_node_id: "", packets_per_cycle: 1 }])}>添加单向通信流</button>
            </section>}
            {planning && <ExperimentParticipation nodes={scene.nodes} workload={value.workload} fusionReceiverNodeId={value.fusionReceiverNodeId ?? null} simulationCycleCount={value.simulationCycleCount} minimumBearingPoints={Number(value.minimumBearingPoints)} flows={value.flows} />}
            {value.workload === "BearingFusion" && <details className="parameter-detail"><summary>定位算法 · 集中式方位交汇</summary>
              <small>按方位角交汇定位（AOA），目前采用单中心处理。</small>
              {planning && <label>当前算法的固定处理节点<select value={value.fusionReceiverNodeId ?? ""} onChange={event => set("fusionReceiverNodeId", event.target.value || null)}><option value="">请选择可接收节点</option>{scene.nodes.map(node => <option key={node.node_id} value={node.node_id} disabled={!node.can_receive}>N{node.node_id}{!node.can_receive ? " · 不可接收" : ""}</option>)}</select></label>}
              <details><summary>算法高级设置：积累 {value.minimumBearingPoints} 条后尝试定位</summary><label>本算法的报告积累门槛<input type="number" min="5" step="1" value={value.minimumBearingPoints} onChange={e => set("minimumBearingPoints", e.target.value)} /></label>
              <small>收到足够报告后尝试定位；条数不是节点数。</small></details>
            </details>}
            {value.workload === "BearingFusion" && <details className="parameter-detail" open={targetExpanded} onToggle={event => setTargetExpanded(event.currentTarget.open)}><summary>探测目标位置</summary><div className="form-grid"><label>目标 X（m）<input type="number" value={target.x} onChange={e => setTarget({ ...target, x: Number(e.target.value) })} /></label><label>目标 Y（m）<input type="number" value={target.y} onChange={e => setTarget({ ...target, y: Number(e.target.value) })} /></label></div><small>可拖动画布上的目标。此位置用于生成观测，不是定位结果。</small></details>}
            {value.macMode === "Tdma" && <details className="parameter-detail"><summary>配置式 TDMA · 私有高级参数</summary>
              <small>按手工时隙轮流发送。</small>
              <div className="form-grid"><label>每个时隙（s）<input type="number" min="0.001" step="0.1" value={Number(slotDurationNs) / 1e9} onChange={e => setSlotDurationNs(String(Math.round(Number(e.target.value) * 1e9)))} /></label><label>时隙保护时间（s）<input type="number" min="0" step="0.1" value={guardSeconds} onChange={e => set("guardIntervalNs", String(Math.round(Number(e.target.value) * 1e9)))} /></label></div>
              <small>保护时间包含在时隙内，给传播和接收留出余量。</small>
              {!planning && <><label>重新规划间隔（周期）<input type="number" min="1" step="1" value={value.networkUpdateIntervalCycles} onChange={e => set("networkUpdateIntervalCycles", e.target.value)} /></label><p>当前每 {value.networkUpdateIntervalCycles} 个通信周期重新计算网络结构与计划；不是向 TDMA 读取调度表的频率。较短间隔表示更频繁更新，尚不代表已实现自适应参数算法。</p></>}
            </details>}
            {planning && value.macMode === "AdaptiveTdma" && <details className="parameter-detail"><summary>自适应 TDMA · 私有高级参数</summary><small>按待发数据、发送时长和传播时间自动排槽。</small><label>时隙安全余量（s）<input type="number" min="0" step="0.001" value={planningSecondsInput(value.adaptiveSafetyMarginNs)} onChange={event => set("adaptiveSafetyMarginNs", planningNanosecondsInput(event.target.value))} /></label><small>额外预留时间，通常保持 0。</small></details>}
            {planning && <details className="parameter-detail"><summary>业务产生节拍（高级）</summary><label>当前规划方案的业务周期（s）<input type="number" min="0.000000001" step="1" value={planningSecondsInput(value.planningCycleDurationNs)} onChange={event => set("planningCycleDurationNs", planningNanosecondsInput(event.target.value))} /></label><small>每周期产生新业务并重新规划；不是手工时隙长度。</small></details>}
          </section>
          <section id="phy" className="panel form-panel">
            <h2>物理模式</h2>
            <label>接收方法<select aria-label="物理层实现" value={physicalLayer} onChange={e => setPhysicalLayer(e.target.value as "Channel1Abstract" | "Channel2Waveform")}>
              <option value="Channel1Abstract">模式 A · 快速网络仿真</option><option value="Channel2Waveform">模式 B · 波形通信仿真</option>
            </select></label>
            <p className="property-help">{physicalLayer === "Channel1Abstract" ? "用于比较路由、接入与网络表现。以声学传播和接收模型计算结果，不逐比特生成波形。" : "逐比特生成、传播、解调波形，可观察实际误码与 CRC；计算量更大，多径畸变可能造成解码失败。"}</p>
            {physicalLayer === "Channel2Waveform" && <div className="waveform-config">
              <label>调制方式<select value={waveformModulation} onChange={e => setWaveformModulation(e.target.value as typeof waveformModulation)}><option value="Bpsk">BPSK · 二进制相移键控</option><option value="Bfsk">BFSK · 二进制频移键控</option><option value="Osdm" disabled={!planning}>OSDM · 训练序列信道估计</option></select></label>
              {waveformModulation === "Osdm" && <OsdmSettings layer="Channel2Waveform" value={osdm} onChange={setOsdm} allowInitialPreset={server?.payload.waveform_modulation !== "Osdm"} />}
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
            </div><p>参数实际进入运行。中心频率必须在所选环境的离线计算覆盖范围内；更改速率会影响发送时长；自适应模式需检查业务周期容量，配置式 TDMA 需同时检查手工时隙。配置速率不等于有效吞吐量。</p></details>
          </section>
          <section id="execution" className="panel form-panel">
            <h2>运行时长</h2>
            <label>通信周期数<input type="number" min="1" step="1" value={value.simulationCycleCount} onChange={e => set("simulationCycleCount", e.target.value)} /></label>
            {planning ? <p className="property-help">每轮长度取自所选 MAC 的私有规划配置，不再在通用运行设置中编辑。业务不足以在本轮发完时排队到后续轮次；运行结束不会假装全部发送成功。</p> : <p className="property-help">历史执行方式保留原有周期语义；不是轨迹采样次数，也不是每秒帧数。</p>}
            <div className="duration-preview"><strong>预计仿真时段</strong><b>{coverage.endNs === null ? "请检查周期设置" : `0～${Number(coverage.endNs) / 1e9} s`}</b><small>{planning ? `${Number(value.planningCycleDurationNs) / 1e9} s / 业务周期 × ${value.simulationCycleCount} 周期` : `${Math.max(0, scene.nodes.length - 1)} 个发送节点 × ${Number(slotDurationNs) / 1e9} s / 时隙 × ${value.simulationCycleCount} 周期`}</small></div>
            {coverage.issues.length > 0 && <details className="parameter-detail coverage-warning"><summary>轨迹覆盖待检查 · {coverage.issues.length} 项</summary>{coverage.issues.map((issue, i) => <p key={i}>{issue}</p>)}</details>}
            <details className="parameter-detail"><summary>重复实验设置（高级）</summary><label>随机序列编号（随机种子）<input inputMode="numeric" value={value.deterministicSeed} onChange={e => set("deterministicSeed", e.target.value)} /></label><small>同一配置和编号可重复实验；换编号用于比较不同随机噪声。</small></details>
          </section>
        </div>
  );
  const workspaceControls = <section className="experiment-control-panel" aria-label="实验操作">
    <div className="unified-experiment-toolbar">
      <div><h1>实验工作台</h1><small>节点、轨迹与网络配置 · 同页保存</small></div>
      <label>实验名称<input value={value.name} onChange={e => set("name", e.target.value)} disabled={save.isPending || run.isPending} /></label>
      <span role="status">{server ? signature === savedSignature ? `已保存${savedIssues.length ? " · 尚有待完善项" : ""}` : "有未保存修改" : "尚未保存"}</span>
      <button type="button" onClick={() => save.mutate()} disabled={save.isPending || run.isPending}>保存实验草稿</button>
      {planning && value.workload === "CommunicationOnly" && <label>执行方式<select value={pairedRun ? "Both" : "Single"} onChange={e => setPairedRun(e.target.value === "Both")} disabled={run.isPending || save.isPending}><option value="Single">仅运行当前模式</option><option value="Both">A+B 顺序对照</option></select></label>}
      <button type="button" className="primary-button" onClick={() => run.mutate()} disabled={run.isPending || save.isPending}>{run.isPending ? "正在保存并启动…" : "保存并运行"}</button>
    </div>
    {(save.error || run.error) && <ErrorState error={save.error || run.error} />}
    {server && <details className="studio-save-note"><summary>保存与版本记录</summary>场景修订 {sceneServer?.revision} / 实验修订 {server.revision}。保存草稿不代表已通过运行检查。</details>}
    {savedIssues.length > 0 && <section className="studio-save-note" aria-label="保存校验" aria-live="polite">
      <strong>草稿已保存，以下问题尚待完善，暂不能运行</strong>
      <ul>{savedIssues.map((issue, index) => <li key={index}>{issue}</li>)}</ul>
      <p>点击“环境与实验参数”，在“资源”中检查环境，在“网络”中配置通信流或应用接收节点；轨迹与收发能力请点击对应节点检查。</p>
      <p>{signature === savedSignature ? "修改后再次保存以更新检查结果。启动时还会检查轨迹覆盖与运行条件。" : "以上为上次保存的检查结果；当前有未保存修改，请再次保存更新。"}</p>
    </section>}
    <p className="experiment-control-help">在中间布设节点，右侧编辑所选节点。点击画布空白处或“环境与实验参数”，切回网络与运行配置。</p>
  </section>;
  return <div className="unified-experiment-page authoring-layout" aria-busy={sceneSwitching}>
    {pendingSceneId !== null && <div className="scene-switch-backdrop"><section role="alertdialog" aria-modal="true" aria-labelledby="scene-switch-title" className="scene-switch-dialog" onKeyDown={event => { if (event.key === "Escape") setPendingSceneId(null); }}><h2 id="scene-switch-title">切换节点场景？</h2><p>未保存的节点修改将丢弃，通信流和处理节点需重新选择。</p><div><button autoFocus onClick={() => setPendingSceneId(null)}>保留当前场景</button><button className="primary-button" onClick={() => switchScene(pendingSceneId)}>确认切换</button></div></section></div>}
    {sceneSwitching && <div className="scene-switch-progress" role="status">正在切换场景…</div>}
    <fieldset className="studio-editing-surface" disabled={save.isPending || run.isPending || sceneSwitching || pendingSceneId !== null}>
      <ScenarioDesigner draft={scene} onChange={setScene} trajectories={trajectories.data} globalInspector={inspector}
        target={value.workload === "BearingFusion" ? target : undefined} onTargetChange={setTarget} onTargetSelect={() => { setInspectorSection("network"); setTargetExpanded(true); }}
        applicationReceiverNodeId={planning ? value.workload === "BearingFusion" ? value.fusionReceiverNodeId ?? null : null : undefined}
        workspaceControls={workspaceControls}
        allowRelativePlacement={capabilities.data?.relative_trajectory_placement === true}
        environmentInputs={sceneEnvironmentInputs.data}
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
