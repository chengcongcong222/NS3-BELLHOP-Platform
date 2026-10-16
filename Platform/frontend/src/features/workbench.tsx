import { useEffect, useState } from "react";
import { useMutation, useQuery, useQueryClient } from "@tanstack/react-query";
import { Link, useNavigate, useParams, useSearchParams } from "react-router-dom";
import { apiClient } from "../api/client";
import { queries } from "../api/queries";
import type { ExperimentDto, ScenarioDto } from "../api/types";
import { DraftEnvironmentProfile } from "../components/EnvironmentProfile";
import { EmptyState, ErrorState, Field, LoadingState, MetricCard, PageHeader, Status } from "../components/common";
import { ScenarioDesigner } from "../components/ScenarioDesigner";
import { composeCases } from "../domain/cases";
import { formatFrequency, formatNanoseconds } from "../domain/format";
import { physicalModeLabel } from "../productLanguage";
import { ScenarioTopology } from "../components/ScenarioTopology";
import "./workbenchLibrary.css";
import {
  deleteDraft, experimentDraftFrom, loadDrafts, newEnvironmentDraft, saveDraft,
  scenarioDraftFrom, validateDraft, type EnvironmentDraft, type ExperimentDraft,
  type ScenarioDraft, type WorkspaceDraft,
} from "../domain/workspace";

function useCatalog() {
  const environments = useQuery(queries.environments());
  const scenarios = useQuery(queries.scenarios());
  const experiments = useQuery(queries.experiments());
  const pending = environments.isPending || scenarios.isPending || experiments.isPending;
  const error = environments.error || scenarios.error || experiments.error;
  return {
    environments: { ...environments, data: environments.data ?? [] },
    scenarios: { ...scenarios, data: scenarios.data ?? [] },
    experiments: { ...experiments, data: experiments.data ?? [] },
    pending,
    error,
    cases: composeCases(environments.data ?? [], scenarios.data ?? [], experiments.data ?? []),
  };
}

function useDrafts() {
  const [drafts, setDrafts] = useState<WorkspaceDraft[]>(() => loadDrafts());
  useEffect(() => { const sync = () => setDrafts(loadDrafts()); window.addEventListener("workbench-drafts-changed", sync); return () => window.removeEventListener("workbench-drafts-changed", sync); }, []);
  return drafts;
}

function CaseCard({ item, compact = false }: { item: ReturnType<typeof composeCases>[number]; compact?: boolean }) {
  return <article className={`wb-case-card ${compact ? "compact" : ""}`}><header><span>{item.classification}</span><small>实验版本 {item.experiment.version}</small></header><h2><Link to={`/cases/${item.id}`}>{item.title}</Link></h2><p>{item.summary}</p><dl><Field label="节点 / 应用接收点">{item.scenario.nodes.length} 个 / {item.scenario.fusion_center_node_id === null ? "无固定接收点" : `N${item.scenario.fusion_center_node_id}`}</Field><Field label="工作频率">{item.experiment.phy.center_frequency_hz / 1000} kHz</Field><Field label="声场范围">{item.environment.axes.horizontal_range.maximum / 1000} km</Field><Field label="仿真周期">{item.experiment.simulation_cycle_count}</Field></dl><Link to={`/cases/${item.id}`}>查看配置与运行 →</Link></article>;
}

export function WorkbenchPage() {
  const catalog = useCatalog(); const runs = useQuery(queries.runs()); const results = useQuery(queries.results()); const drafts = useQuery(queries.drafts());
  const [search, setSearch] = useState("");
  const recentRun = runs.data?.at(-1);
  const recentDetails = useQuery({ ...queries.runActions(recentRun?.run_id ?? ""), enabled: Boolean(recentRun) });
  if (catalog.pending || runs.isPending || results.isPending || drafts.isPending) return <LoadingState />;
  if (catalog.error || runs.error || results.error || drafts.error) return <ErrorState error={catalog.error || runs.error || results.error || drafts.error} />;
  const recentResult = results.data.find(item => item.run_id === recentRun?.run_id);
  const latestCase = catalog.cases[0];
  const editing = drafts.data.filter(item => item.kind === "experiment").sort((a, b) => b.updated_at.localeCompare(a.updated_at));
  const visibleDrafts = editing.filter(item => `${item.name} ${item.draft_id}`.toLocaleLowerCase().includes(search.toLocaleLowerCase()));
  return <div className="wb-home">
    <header className="wb-heading"><div><h1>水声网络数字孪生仿真工作台</h1><p>建设可复用资源，在同一个实验中配置节点、协议和物理模式。</p></div><Link className="primary-button" to="/workspace/experiment">＋ 新建实验</Link></header>
    <nav className="wb-resource-strip" aria-label="资源概览"><Link to="/environments">声学环境 <b>{catalog.environments.data.length}</b></Link><Link to="/workspace/experiment">节点与轨迹</Link><Link to="/cases">预置案例 <b>{catalog.cases.length}</b></Link><Link to="/runs">运行记录 <b>{runs.data.length}</b></Link><span>正式结果 <b>{results.data.length}</b></span><span>ns-3 离散事件</span></nav>
    <div className="wb-home-grid"><section className="wb-panel wb-drafts"><header><h2>我的实验 <small>{editing.length}</small></h2><label className="wb-search"><span className="wb-sr-only">查找我的实验</span><input type="search" placeholder="按实验名称查找" value={search} onChange={event => setSearch(event.target.value)} /></label></header>
      <div className="wb-draft-list">{visibleDrafts.length ? <table><thead><tr><th>实验 / 场景</th><th>物理模式</th><th>最近保存</th><th>编辑</th></tr></thead><tbody>{visibleDrafts.slice(0, 6).map(item => { const scene = drafts.data.find(draft => draft.kind === "scenario" && draft.draft_id === item.payload.scenario_id); return <tr key={item.draft_id}><td><Link to={`/workspace/experiment?draft=${encodeURIComponent(item.draft_id)}`}>{item.name}</Link><small>{scene?.name ?? "场景引用待核对"} · 修订 {item.revision}</small></td><td title={physicalModeLabel(String(item.payload.physical_layer ?? ""))}>{item.payload.physical_layer === "Channel1Abstract" ? "A · 快速网络" : item.payload.physical_layer === "Channel2Waveform" ? "B · 波形通信" : item.payload.physical_layer === "Channel3Recorded" ? "C · 录音回放" : "待选择"}</td><td>{new Date(item.updated_at).toLocaleString("zh-CN", { month: "2-digit", day: "2-digit", hour: "2-digit", minute: "2-digit" })}</td><td><Link aria-label={`继续编辑 ${item.name}`} to={`/workspace/experiment?draft=${encodeURIComponent(item.draft_id)}`}>打开 →</Link></td></tr>; })}</tbody></table> : <div className="wb-empty"><strong>{search ? "没有匹配的实验" : "还没有自己的实验"}</strong><p>{search ? "换一个名称试试。" : "可以新建实验，也可以先查看右侧预置案例的资源与配置。"}</p></div>}</div>
      <footer><span>{visibleDrafts.length > 6 ? `显示最近 6 / ${visibleDrafts.length} 个匹配实验，可按名称定位` : "草稿保存在服务器；运行会冻结当次配置"}</span><Link to="/workspace/experiment">进入实验编辑 →</Link></footer>
    </section>
    <section className="wb-panel wb-reference"><header><h2>预置案例</h2><Link to="/cases">全部案例 →</Link></header>{latestCase ? <><h3><Link to={`/cases/${latestCase.id}`}>{latestCase.title}</Link></h3><div className="wb-preview"><span>初始节点布设 · 非实时位置</span><ScenarioTopology scenario={latestCase.scenario} /></div><div className="wb-reference-facts"><span>{latestCase.scenario.nodes.length} 节点</span><span>{latestCase.experiment.phy.center_frequency_hz / 1000} kHz</span><span>{latestCase.scenario.fusion_center_node_id === null ? "无固定接收点" : `应用接收点 N${latestCase.scenario.fusion_center_node_id}`}</span><Link to={`/cases/${latestCase.id}`}>查看配置 →</Link></div></> : <div className="wb-empty">尚无可运行案例</div>}</section></div>
    <section className="wb-panel wb-recent"><header><h2>最近运行</h2><Link to="/runs">全部运行 →</Link></header>{recentRun ? <div className="wb-recent-content"><div><strong>{recentDetails.data?.experiment_name ?? recentRun.experiment_id}</strong><small title={recentRun.run_id}>运行记录 · 配置已冻结</small><span>场景：{recentDetails.data?.scenario_name ?? recentRun.scenario_id} · v{recentRun.scenario_version}</span></div><Status value={recentRun.lifecycle} /><Link to={`/runs/${recentRun.run_id}`}>打开运行工作台</Link>{recentResult && <Link to={`/results/${recentResult.run_id}`}>查看最近结果</Link>}</div> : <div className="wb-empty">尚无运行记录，保存实验后即可开始仿真。</div>}</section>
  </div>;
}



export function CaseCatalogPage() {
  const catalog = useCatalog(); if (catalog.pending) return <LoadingState />; if (catalog.error) return <ErrorState error={catalog.error} />;
  return <div className="wb-cases"><PageHeader title="案例中心" detail="查看预置实验的真实环境、节点与通信参数，再按已发布配置运行。" /><div className="wb-case-grid">{catalog.cases.map((item) => <CaseCard key={item.id} item={item} />)}</div></div>;
}

function StartCaseButton({ experiment }: { experiment: ExperimentDto }) {
  const navigate = useNavigate(); const client = useQueryClient(); const mutation = useMutation({ mutationFn: () => apiClient.createRun(experiment.experiment_id, experiment.version), onSuccess: async (run) => { await client.invalidateQueries({ queryKey: ["runs"] }); navigate(`/runs/${run.run_id}`); } });
  return <><button type="button" onClick={() => mutation.mutate()} disabled={mutation.isPending}>{mutation.isPending ? "正在创建真实运行…" : "运行默认实验"}</button>{mutation.error && <ErrorState error={mutation.error} />}</>;
}

export function CaseDetailPage() {
  const { caseId } = useParams(); const catalog = useCatalog();
  if (catalog.pending) return <LoadingState />; if (catalog.error) return <ErrorState error={catalog.error} />;
  const item = catalog.cases.find((candidate) => candidate.id === caseId); if (!item) return <EmptyState>案例不存在或其正式资源组合不完整。</EmptyState>;
  const seconds = (value: string) => formatNanoseconds(value).split(" (")[0];
  const routing = item.experiment.routing.mode === "Direct" ? "直接送往业务目的节点" : item.experiment.routing.mode === "DirectToFusionCenter" ? "直接送往应用接收点" : item.experiment.routing.mode === "ShortestPath" ? "最短路径" : "路由配置未识别";
  return <div className="wb-case-detail"><header className="wb-heading"><div><Link to="/cases">← 案例中心</Link><h1>{item.title}</h1><p>{item.summary}</p></div><div className="wb-case-launch"><span>已发布配置 · 只读</span><StartCaseButton experiment={item.experiment} /></div></header>
    <div className="wb-case-detail-grid"><section className="wb-panel"><header><h2>场景与节点</h2><span>{item.scenario.nodes.length} 个节点 · {item.scenario.fusion_center_node_id === null ? "无固定接收点" : `应用接收点 N${item.scenario.fusion_center_node_id}`}</span></header><div className="wb-case-geometry"><ScenarioTopology scenario={item.scenario} /></div><div className="wb-node-table"><table><caption>{item.scenario.name} · 初始坐标，单位 m</caption><thead><tr><th>节点</th><th>X</th><th>Y</th><th>Z</th><th>收发能力</th></tr></thead><tbody>{item.scenario.nodes.map(node => <tr key={node.node_id}><th>N{node.node_id}{node.node_id === item.scenario.fusion_center_node_id ? " · 应用接收点" : ""}</th><td>{node.initial_position.x_meters}</td><td>{node.initial_position.y_meters}</td><td>{node.initial_position.z_meters}</td><td>{node.can_transmit ? "发送" : "不发送"} / {node.can_receive ? "接收" : "不接收"}</td></tr>)}</tbody></table></div></section>
    <aside className="wb-case-settings"><section className="wb-panel"><header><h2>绑定环境</h2><Link to={`/environments/${item.environment.environment_asset_id}`}>查看环境 →</Link></header><dl><Field label="环境版本">{item.environment.asset_format_version}</Field><Field label="声场距离">{item.environment.axes.horizontal_range.minimum}–{item.environment.axes.horizontal_range.maximum} m</Field><Field label="收发深度">{Math.min(item.environment.axes.source_depth.minimum, item.environment.axes.receiver_depth.minimum)}–{Math.max(item.environment.axes.source_depth.maximum, item.environment.axes.receiver_depth.maximum)} m</Field><Field label="计算频率">{formatFrequency(item.environment.axes.frequency.minimum)}</Field></dl><small className="wb-resource-id">{item.environment.environment_asset_id}</small></section>
    <section className="wb-panel"><header><h2>通信与运行参数</h2><span>实验版本 {item.experiment.version}</span></header><dl><Field label="路由策略">{routing}</Field><Field label="信道接入">{item.experiment.mac.mode === "AdaptiveTdma" ? "自适应 TDMA · 自动规划时隙" : item.experiment.mac.mode === "Tdma" ? "配置式 TDMA · 分时发送" : "接入方式未识别"}</Field><Field label="发送比特率">{item.experiment.phy.bit_rate_bits_per_second} bit/s</Field><Field label="频率 / 带宽">{item.experiment.phy.center_frequency_hz / 1000} / {item.experiment.phy.occupied_bandwidth_hz / 1000} kHz</Field><Field label="时隙 / 保护时间">{item.experiment.mac.slot_duration_ns === null ? "自动规划" : seconds(item.experiment.mac.slot_duration_ns)} / {item.experiment.mac.guard_interval_ns === null ? "算法内计算" : seconds(item.experiment.mac.guard_interval_ns)}</Field><Field label="仿真周期 / 种子">{item.experiment.simulation_cycle_count} / {item.experiment.deterministic_seed}</Field><Field label="业务">{item.experiment.fusion.workload === "CommunicationOnly" ? "纯网络通信" : "方位特征融合"}</Field></dl></section>
    <section className="wb-case-note"><strong>这个案例将如何执行</strong><p>按本页环境、场景和实验版本创建独立运行。查看真实轨迹位置、通信事件和结果，不修改预置资源。</p></section></aside></div>
    <details className="wb-identities"><summary>精确资源标识</summary><dl><Field label="场景">{item.scenario.scenario_id} · v{item.scenario.version}</Field><Field label="实验">{item.experiment.experiment_id} · v{item.experiment.version}</Field><Field label="环境校验值">{item.environment.checksum.value}</Field></dl></details>
  </div>;
}

function DraftActions({ draft, onSaved }: { draft: WorkspaceDraft; onSaved: () => void }) {
  const issues = validateDraft(draft);
  return <div className="draft-actions"><div>{issues.length ? <span className="validation-bad">尚有 {issues.length} 项需要修正：{issues.join("；")}</span> : <span className="validation-good">✓ 草稿校验通过</span>}<small>当前只能保存本地工作草稿；待发布能力接入后才能供团队运行。</small></div><button type="button" disabled={issues.length > 0} onClick={() => { saveDraft({ ...draft, updatedAt: new Date().toISOString() }); onSaved(); }}>保存到本机工作区</button></div>;
}

export function EnvironmentWorkspacePage() {
  const [params] = useSearchParams(); const environments = useQuery(queries.environments()); const existing = loadDrafts().find((item): item is EnvironmentDraft => item.kind === "environment" && item.id === params.get("draft")); const source = environments.data?.find((item) => item.environment_asset_id === params.get("source"));
  const [draft, setDraft] = useState<EnvironmentDraft>(() => existing ?? newEnvironmentDraft()); const [saved, setSaved] = useState(false);
  useEffect(() => { if (source && !existing) setDraft(newEnvironmentDraft(source)); }, [source?.environment_asset_id]);
  if (environments.isPending) return <LoadingState />; if (environments.error) return <ErrorState error={environments.error} />;
  const parseLines = (value: string) => value.split("\n").map((line) => line.trim().split(/[ ,]+/).map(Number)).filter(([a, b]) => Number.isFinite(a) && Number.isFinite(b));
  return <div className="environment-workbench-page"><section className="workspace-toolbar"><div><small>本地工作草稿 · 声学环境</small><h1>{draft.name}</h1></div><label>环境名称<input value={draft.name} onChange={(event) => setDraft({ ...draft, name: event.target.value })} /></label><span className={saved ? "saved-state" : "unsaved-state"}>{saved ? "已保存" : "有未保存修改"}</span></section><div className="environment-studio"><aside className="environment-inputs"><div className="studio-pane-title"><span>环境输入</span><small>Input</small></div><label>说明<textarea rows={3} value={draft.description} onChange={(event) => setDraft({ ...draft, description: event.target.value })} /></label><div className="form-grid"><label>最大水深（m）<input type="number" min="1" value={draft.maximumDepthMeters} onChange={(event) => setDraft({ ...draft, maximumDepthMeters: Number(event.target.value) })} /></label><label>最大距离（m）<input type="number" min="1" value={draft.maximumRangeMeters} onChange={(event) => setDraft({ ...draft, maximumRangeMeters: Number(event.target.value) })} /></label><label>工作频率（Hz）<input type="number" min="1" value={draft.centerFrequencyHz} onChange={(event) => setDraft({ ...draft, centerFrequencyHz: Number(event.target.value) })} /></label></div><h3>声速剖面 SSP</h3><p className="field-help">每行：深度 m, 声速 m/s</p><textarea rows={7} aria-label="声速剖面数据" value={draft.soundSpeedProfile.map((p) => `${p.depthMeters}, ${p.speedMetersPerSecond}`).join("\n")} onChange={(event) => setDraft({ ...draft, soundSpeedProfile: parseLines(event.target.value).map(([depthMeters, speedMetersPerSecond]) => ({ depthMeters, speedMetersPerSecond })) })} /><h3>海底地形 Bathymetry</h3><p className="field-help">每行：水平距离 m, 水深 m</p><textarea rows={6} aria-label="海底地形数据" value={draft.bathymetry.map((p) => `${p.rangeMeters}, ${p.depthMeters}`).join("\n")} onChange={(event) => setDraft({ ...draft, bathymetry: parseLines(event.target.value).map(([rangeMeters, depthMeters]) => ({ rangeMeters, depthMeters })) })} /></aside><section className="environment-canvas"><div className="canvas-toolbar"><strong>环境剖面与建设预览</strong><span>海面、SSP、海底和预期覆盖共享同一工作区</span></div><DraftEnvironmentProfile draft={draft} /><div className="environment-scale"><span>海面 0 m</span><span>水深 {draft.maximumDepthMeters} m</span><span>距离 0–{draft.maximumRangeMeters} m</span></div></section><aside className="environment-output"><div className="studio-pane-title"><span>建设状态</span><small>Output</small></div><dl><Field label="建设方式">Bellhop 离线</Field><Field label="工作频率">{formatFrequency(draft.centerFrequencyHz)}</Field><Field label="SSP 点数">{draft.soundSpeedProfile.length}</Field><Field label="地形点数">{draft.bathymetry.length}</Field><Field label="传播场">尚未生成</Field><Field label="NoArrival">尚无正式数据</Field></dl><div className="boundary-note"><strong>输入与结果边界</strong><p>本页编辑环境建设输入。后端尚未提供离线作业与发布接口，因此不会声称已经生成传播损失或 NoArrival 数据。</p></div></aside></div><DraftActions draft={draft} onSaved={() => setSaved(true)} /></div>;
}

function DraftProvenanceNotice({ draft }: { draft: WorkspaceDraft }) {
  const source = draft.provenance;
  return <aside className="draft-boundary"><strong>本地草稿 · 可编辑</strong><span>{source.label}</span>{source.kind === "derived" && <><small>已继承：{source.inherited.join("、") || "无"}</small><small>未继承：{source.omitted.join("、") || "无"}</small></>}</aside>;
}

function NumericPointTable({ title, columns, points, onChange, onAdvancedChange }: { title: string; columns: readonly [string, string]; points: Array<{ first: number; second: number }>; onChange: (points: Array<{ first: number; second: number }>) => void; onAdvancedChange: (value: string) => void }) {
  const update = (index: number, field: "first" | "second", value: string) => onChange(points.map((point, current) => current === index ? { ...point, [field]: Number(value) } : point));
  const valid = (point: { first: number; second: number }) => Number.isFinite(point.first) && Number.isFinite(point.second) && point.first >= 0 && point.second >= 0;
  return <section className="point-editor"><div><h3>{title}</h3><button type="button" onClick={() => onChange([...points, { first: 0, second: 0 }])}>添加点</button><button type="button" className="text-button" onClick={() => onChange([...points].sort((a, b) => a.first - b.first))}>按{columns[0]}排序</button></div><table><thead><tr><th>{columns[0]}</th><th>{columns[1]}</th><th>校验</th><th>操作</th></tr></thead><tbody>{points.map((point, index) => <tr key={index}><td><input aria-label={`${title}-${columns[0]}-${index}`} type="number" min="0" value={point.first} onChange={(event) => update(index, "first", event.target.value)} /></td><td><input aria-label={`${title}-${columns[1]}-${index}`} type="number" min="0" value={point.second} onChange={(event) => update(index, "second", event.target.value)} /></td><td>{valid(point) ? "有效" : "需为有限非负数"}</td><td><button type="button" className="text-button" onClick={() => onChange(points.filter((_, current) => current !== index))}>删除</button></td></tr>)}</tbody></table><details><summary>批量数据（高级）</summary><textarea aria-label={`${title}批量数据`} rows={4} value={points.map((point) => `${point.first}, ${point.second}`).join("\n")} onChange={(event) => onAdvancedChange(event.target.value)} /></details></section>;
}

export function R3EnvironmentWorkspacePage() {
  const [params] = useSearchParams(); const environments = useQuery(queries.environments()); const existing = loadDrafts().find((item): item is EnvironmentDraft => item.kind === "environment" && item.id === params.get("draft")); const source = environments.data?.find((item) => item.environment_asset_id === params.get("source"));
  const [draft, setDraft] = useState<EnvironmentDraft>(() => existing ?? newEnvironmentDraft()); const [saved, setSaved] = useState(false);
  useEffect(() => { if (source && !existing) setDraft(newEnvironmentDraft(source)); }, [source?.environment_asset_id]);
  if (environments.isPending) return <LoadingState />; if (environments.error) return <ErrorState error={environments.error} />;
  const parse = (value: string) => value.split("\n").map((line) => line.trim().split(/[ ,]+/).map(Number)).filter(([a, b]) => Number.isFinite(a) && Number.isFinite(b));
  const setSsp = (points: Array<{ first: number; second: number }>) => { setDraft({ ...draft, soundSpeedProfile: points.map((point) => ({ depthMeters: point.first, speedMetersPerSecond: point.second })) }); setSaved(false); };
  const setBathymetry = (points: Array<{ first: number; second: number }>) => { setDraft({ ...draft, bathymetry: points.map((point) => ({ rangeMeters: point.first, depthMeters: point.second })) }); setSaved(false); };
  return <div className="environment-workbench-page"><section className="workspace-toolbar"><div><small>本地工作草稿 · 声学环境</small><h1>{draft.name}</h1></div><label>环境名称<input value={draft.name} onChange={(event) => { setDraft({ ...draft, name: event.target.value }); setSaved(false); }} /></label><span className={saved ? "saved-state" : "unsaved-state"}>{saved ? "已保存" : "有未保存修改"}</span></section><DraftProvenanceNotice draft={draft} /><div className="environment-studio"><aside className="environment-inputs"><div className="studio-pane-title"><span>环境输入</span><small>可编辑</small></div><label>说明<textarea rows={3} value={draft.description} onChange={(event) => setDraft({ ...draft, description: event.target.value })} /></label><div className="form-grid"><label>最大水深（m）<input type="number" min="1" value={draft.maximumDepthMeters} onChange={(event) => setDraft({ ...draft, maximumDepthMeters: Number(event.target.value) })} /></label><label>最大距离（m）<input type="number" min="1" value={draft.maximumRangeMeters} onChange={(event) => setDraft({ ...draft, maximumRangeMeters: Number(event.target.value) })} /></label><label>工作频率（Hz）<input type="number" min="1" value={draft.centerFrequencyHz} onChange={(event) => setDraft({ ...draft, centerFrequencyHz: Number(event.target.value) })} /></label></div><NumericPointTable title="声速剖面 SSP" columns={["深度 m", "声速 m/s"]} points={draft.soundSpeedProfile.map((point) => ({ first: point.depthMeters, second: point.speedMetersPerSecond }))} onChange={setSsp} onAdvancedChange={(value) => setSsp(parse(value).map(([first, second]) => ({ first, second })))} /><NumericPointTable title="海底地形 Bathymetry" columns={["距离 m", "水深 m"]} points={draft.bathymetry.map((point) => ({ first: point.rangeMeters, second: point.depthMeters }))} onChange={setBathymetry} onAdvancedChange={(value) => setBathymetry(parse(value).map(([first, second]) => ({ first, second })))} /></aside><section className="environment-canvas"><div className="canvas-toolbar"><strong>草稿输入预览</strong><span>空草稿不会合成示例曲线</span></div><DraftEnvironmentProfile draft={draft} /></section><aside className="environment-output"><div className="studio-pane-title"><span>建设状态</span><small>未发布</small></div><dl><Field label="建设方式">Bellhop 离线</Field><Field label="工作频率">{formatFrequency(draft.centerFrequencyHz)}</Field><Field label="SSP 点数">{draft.soundSpeedProfile.length}</Field><Field label="地形点数">{draft.bathymetry.length}</Field><Field label="传播场">尚未生成</Field></dl><p className="data-boundary">当前只保存本地草稿；后端未提供离线作业或发布接口。</p></aside></div><DraftActions draft={draft} onSaved={() => setSaved(true)} /></div>;
}

function defaultScenario(scenarios: readonly ScenarioDto[]): ScenarioDraft {
  if (scenarios[0]) return scenarioDraftFrom(scenarios[0]);
  return { kind: "scenario", id: `scenario-draft-${Date.now()}`, name: "未命名场景", environmentAssetId: "", fusionCenterNodeId: "0", provenance: { kind: "new", label: "新建" }, nodes: [{ node_id: "0", can_transmit: true, can_receive: true, duplex_mode: "HalfDuplex", initial_position: { x_meters: 0, y_meters: 0, z_meters: -20 }, initial_velocity: { x_meters_per_second: 0, y_meters_per_second: 0, z_meters_per_second: 0 } }], updatedAt: new Date().toISOString() };
}

export function ScenarioWorkspacePage() {
  const [params] = useSearchParams(); const scenarios = useQuery(queries.scenarios()); const environments = useQuery(queries.environments()); const existing = loadDrafts().find((item): item is ScenarioDraft => item.kind === "scenario" && item.id === params.get("draft")); const [draft, setDraft] = useState<ScenarioDraft | null>(existing ?? null); const [saved, setSaved] = useState(false);
  useEffect(() => { if (!draft && scenarios.data) setDraft(defaultScenario(scenarios.data)); }, [scenarios.data]);
  if (scenarios.isPending || environments.isPending || !draft) return <LoadingState />; if (scenarios.error || environments.error) return <ErrorState error={scenarios.error || environments.error} />;
  const environment = environments.data.find((item) => item.environment_asset_id === draft.environmentAssetId);
  return <div className="scenario-workbench-page"><section className="workspace-toolbar"><div><small>本地工作草稿 · 场景设计</small><h1>{draft.name}</h1></div><label>场景名称<input value={draft.name} onChange={(event) => setDraft({ ...draft, name: event.target.value })} /></label><label>声学环境<select value={draft.environmentAssetId} onChange={(event) => setDraft({ ...draft, environmentAssetId: event.target.value })}>{environments.data.map((item) => <option key={item.environment_asset_id}>{item.environment_asset_id}</option>)}</select></label><span className={saved ? "saved-state" : "unsaved-state"}>{saved ? "已保存" : "有未保存修改"}</span></section><ScenarioDesigner draft={draft} onChange={(next) => { setDraft(next); setSaved(false); }} maximumDepthMeters={environment?.axes.receiver_depth.maximum} maximumRangeMeters={environment?.axes.horizontal_range.maximum} initialSelectedIds={params.get("nodes")?.split(",")} /><DraftActions draft={draft} onSaved={() => setSaved(true)} /></div>;
}

function defaultExperiment(experiments: readonly ExperimentDto[]): ExperimentDraft {
  if (experiments[0]) return experimentDraftFrom(experiments[0]);
  return { kind: "experiment", id: `experiment-draft-${Date.now()}`, name: "未命名实验", sourceExperimentId: null, scenarioId: "", provenance: { kind: "new", label: "新建" }, routingMode: "DirectToFusionCenter", macMode: "Tdma", workload: "BearingFusion", minimumBearingPoints: "5", maximumFusionPeriodNs: "180000000000", bitRateBitsPerSecond: "60", centerFrequencyHz: 25000, occupiedBandwidthHz: 4000, sourceLevelDb: 110, guardIntervalNs: "2000000000", simulationCycleCount: "2", deterministicSeed: "1", networkUpdateIntervalCycles: "10", updatedAt: new Date().toISOString() };
}

export function ExperimentWorkspacePage() {
  const [params] = useSearchParams(); const experiments = useQuery(queries.experiments()); const scenarios = useQuery(queries.scenarios()); const existing = loadDrafts().find((item): item is ExperimentDraft => item.kind === "experiment" && item.id === params.get("draft")); const [draft, setDraft] = useState<ExperimentDraft | null>(existing ?? null); const [saved, setSaved] = useState(false);
  useEffect(() => { if (!draft && experiments.data) setDraft(defaultExperiment(experiments.data)); }, [experiments.data]);
  if (experiments.isPending || scenarios.isPending || !draft) return <LoadingState />; if (experiments.error || scenarios.error) return <ErrorState error={experiments.error || scenarios.error} />;
  const set = <K extends keyof ExperimentDraft>(key: K, value: ExperimentDraft[K]) => { setDraft({ ...draft, [key]: value }); setSaved(false); };
  return <><PageHeader title="配置网络实验" detail="按场景、网络、通信和执行步骤配置实验。当前内容保存在本地工作草稿；开始仿真仍需选择已发布实验。" /><div className="experiment-workspace"><aside className="step-rail"><a href="#basic">01 基础信息</a><a href="#network">02 网络配置</a><a href="#communication">03 通信与物理层</a><a href="#execution">04 仿真设置</a></aside><div><section id="basic" className="panel form-panel"><h2>基础信息</h2><label>实验名称<input value={draft.name} onChange={(event) => set("name", event.target.value)} /></label><label>选择节点场景<select value={draft.scenarioId} onChange={(event) => set("scenarioId", event.target.value)}>{scenarios.data.map((item) => <option key={`${item.scenario_id}:${item.version}`} value={item.scenario_id}>{item.name} · v{item.version}</option>)}</select></label></section><section id="network" className="panel form-panel"><h2>网络配置</h2><div className="form-grid"><label>路由方式<select value={draft.routingMode} onChange={(event) => set("routingMode", event.target.value)}><option>DirectToFusionCenter</option></select></label><label>信道接入方式<select value={draft.macMode} onChange={(event) => set("macMode", event.target.value)}><option>Tdma</option></select></label><label>网络更新周期<input value={draft.networkUpdateIntervalCycles} onChange={(event) => set("networkUpdateIntervalCycles", event.target.value)} /></label></div></section><section id="communication" className="panel form-panel"><h2>通信与物理层</h2><div className="form-grid"><label>通信速率（bit/s）<input value={draft.bitRateBitsPerSecond} onChange={(event) => set("bitRateBitsPerSecond", event.target.value)} /></label><label>中心频率（Hz）<input type="number" value={draft.centerFrequencyHz} onChange={(event) => set("centerFrequencyHz", Number(event.target.value))} /></label><label>占用带宽（Hz）<input type="number" value={draft.occupiedBandwidthHz} onChange={(event) => set("occupiedBandwidthHz", Number(event.target.value))} /></label><label>声源级（dB re 1μPa @ 1m）<input type="number" value={draft.sourceLevelDb} onChange={(event) => set("sourceLevelDb", Number(event.target.value))} /></label><label>保护时间（ns）<input value={draft.guardIntervalNs} onChange={(event) => set("guardIntervalNs", event.target.value)} /></label></div></section><section id="execution" className="panel form-panel"><h2>仿真设置</h2><div className="form-grid"><label>仿真周期数<input value={draft.simulationCycleCount} onChange={(event) => set("simulationCycleCount", event.target.value)} /></label><label>确定性 seed<input value={draft.deterministicSeed} onChange={(event) => set("deterministicSeed", event.target.value)} /></label></div><p className="data-boundary">修改本地草稿不会启动仿真。保存后仍需发布为实验版本，才能供团队运行。</p></section></div></div><DraftActions draft={draft} onSaved={() => setSaved(true)} />{saved && <p className="save-toast">实验草稿已保存。</p>}</>;
}

export function ResourceManagementPage() {
  const catalog = useCatalog(); const drafts = useDrafts(); if (catalog.pending) return <LoadingState />; if (catalog.error) return <ErrorState error={catalog.error} />;
  const route = (draft: WorkspaceDraft) => `/workspace/${draft.kind}?draft=${encodeURIComponent(draft.id)}`;
  return <><PageHeader title="资源管理" detail="集中查看正式发布资源和本机工作区草稿。正式版本不可变，草稿可以继续编辑或删除。" /><section className="metric-grid"><MetricCard label="已发布环境" value={catalog.environments.data.length} /><MetricCard label="已发布场景" value={catalog.scenarios.data.length} /><MetricCard label="已发布实验" value={catalog.experiments.data.length} /><MetricCard label="本机草稿" value={drafts.length} /></section><section className="panel"><div className="panel-title"><h2>本机草稿</h2><div className="button-row"><Link to="/workspace/environment">新建环境</Link><Link to="/workspace/scenario">新建场景</Link><Link to="/workspace/experiment">新建实验</Link></div></div>{drafts.length ? <table><thead><tr><th>类型</th><th>名称</th><th>更新时间</th><th>操作</th></tr></thead><tbody>{drafts.map((draft) => <tr key={`${draft.kind}:${draft.id}`}><td>{{ environment: "声学环境", scenario: "节点场景", experiment: "网络实验" }[draft.kind]}</td><td><Link to={route(draft)}>{draft.name}</Link></td><td>{draft.updatedAt}</td><td><button type="button" className="text-button" onClick={() => deleteDraft(draft.kind, draft.id)}>删除草稿</button></td></tr>)}</tbody></table> : <EmptyState>尚无本机草稿。可以从正式资源派生，也可以从空白开始。</EmptyState>}</section><section className="panel"><h2>边界说明</h2><p>当前工作区使用浏览器 localStorage，适合单机 P0 编辑、演示和可重复测试；它不是团队发布数据库。后端尚未提供 Draft clone/publish API，因此没有任何按钮会覆盖正式资源或声称完成发布。</p></section></>;
}

export function SystemPage() {
  const query = useQuery(queries.systemInfo()); if (query.isPending) return <LoadingState />; if (query.error) return <ErrorState error={query.error} />; const item = query.data;
  return <><PageHeader title="系统信息" detail="运行引擎、构建版本与接口边界集中放在这里，不打扰普通仿真工作流。" /><section className="two-column"><article className="panel"><h2>平台</h2><dl><Field label="产品">{item.platform_name}</Field><Field label="版本">{item.platform_version}</Field><Field label="发布基线">{item.product_baseline}</Field><Field label="构建目标">{item.build_target}</Field><Field label="源代码修订">{item.build.source_revision}</Field><Field label="C++ 标准">{item.build.cxx_standard}</Field></dl></article><article className="panel"><h2>仿真权威</h2><dl><Field label="仿真引擎">{item.simulation.engine} {item.simulation.version}</Field><Field label="时间权威">{item.simulation.time_authority}</Field><Field label="调度权威">{item.simulation.scheduler_authority}</Field><Field label="调度网关">{item.simulation.scheduling_gateway}</Field><Field label="运行模式">{item.runtime_mode}</Field></dl></article></section><details className="technical-details" open><summary>接口版本</summary><dl><Field label="API schema">{item.interfaces.api_schema_version}</Field><Field label="Worker wire schema">{item.interfaces.worker_wire_schema_version}</Field><Field label="Acceptance evidence schema">{item.interfaces.acceptance_evidence_schema_version}</Field><Field label="Frontend release">{item.interfaces.frontend_release}</Field></dl></details></>;
}
