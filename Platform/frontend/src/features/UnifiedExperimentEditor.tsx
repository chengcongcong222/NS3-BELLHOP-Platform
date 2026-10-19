import { useEffect, useRef, useState } from "react";
import { useQuery } from "@tanstack/react-query";
import { apiClient } from "../api/client";
import { queries } from "../api/queries";
import { ErrorState } from "../components/common";
import { ExperimentAuthoringPage } from "./authoring";
import { RecordedExperimentPage } from "./recordedExperiment";
import { ExperimentEditorContext, type ResourceKind } from "./experimentEditorContext";
import "./unifiedExperimentEditor.css";

/** Two constrained resource adapters retain separate editing buffers.
 * Switching never retypes or overwrites a persisted experiment/scene.
 */
export function UnifiedExperimentEditor({ initialKind, draftId, scenarioId }: {
  initialKind: ResourceKind; draftId: string | null; scenarioId: string | null;
}) {
  const [kind, setKind] = useState(initialKind);
  const [visited, setVisited] = useState<ResourceKind[]>([initialKind]);
  const [pending, setPending] = useState<{ kind: ResourceKind; id: string } | null>(null);
  const [selections, setSelections] = useState<Partial<Record<ResourceKind, { id: string; serial: number }>>>({});
  const [saving, setSaving] = useState(false);
  const [bindings, setBindings] = useState<Partial<Record<ResourceKind, string>>>({});
  const [states, setStates] = useState<Partial<Record<ResourceKind, { dirty: boolean; busy: boolean }>>>({});
  const busy = Object.values(states).some(s => s.busy);
  const dirty = Object.values(states).some(s => s.dirty);
  useEffect(() => {
    if (!dirty) return;
    const warn = (event: BeforeUnloadEvent) => { event.preventDefault(); event.returnValue = ""; };
    window.addEventListener("beforeunload", warn);
    return () => window.removeEventListener("beforeunload", warn);
  }, [dirty]);
  const [error, setError] = useState<unknown>(null);
  const saves = useRef<Partial<Record<ResourceKind, () => Promise<unknown>>>>({});
  const environments = useQuery(queries.environmentSummaries());
  const recordings = useQuery({ queryKey: ["recorded-channels"], queryFn: apiClient.recordedChannels });
  const apply = () => {
    if (!pending) return;
    setVisited(v => v.includes(pending.kind) ? v : [...v, pending.kind]);
    if (pending.id) setSelections(v => ({ ...v, [pending.kind]: { id: pending.id, serial: (v[pending.kind]?.serial ?? 0) + 1 } }));
    setKind(pending.kind); setPending(null); setError(null);
  };
  const saveAndSwitch = async () => {
    setSaving(true); setError(null);
    try {
      if (!saves.current[kind]) throw new Error("编辑器尚未就绪，请稍后重试。");
      await saves.current[kind]!(); apply();
    } catch (e) { setError(e); } finally { setSaving(false); }
  };
  return <div className="resource-experiment-shell">
    <div className="resource-experiment-bar">
      <label>信道资源 / 实验环境<select value={bindings[kind] ? `${kind}:${bindings[kind]}` : ""} disabled={saving || busy} onChange={e => {
        const [nextKind, ...id] = e.target.value.split(":");
        setPending({ kind: nextKind as ResourceKind, id: id.join(":") });
      }}>
        <option value="" disabled>{kind === "Bellhop" ? "Bellhop 计算声场 · 几何场景" : "UWA 实录信道 · 单向链路"}</option>
        <optgroup label="Bellhop 已发布声场">
          <option value="Bellhop:">打开 Bellhop 场景配置</option>
          {Object.entries(environments.data ?? {}).map(([id, s]) => <option key={id} value={`Bellhop:${id}`}>{String((s as { name?: string }).name ?? id)}</option>)}
        </optgroup>
        {bindings[kind] && (kind === "Bellhop" ? !(bindings[kind]! in (environments.data ?? {})) : !recordings.data?.some(r => r.manifest.asset_id === bindings[kind])) && <option value={`${kind}:${bindings[kind]}`} disabled>原资源不可用 · {bindings[kind]}</option>}
        <optgroup label="UWA 已导入时变信道">
          <option value="Recorded:">打开实录链路配置</option>
          {recordings.data?.map(r => <option key={r.manifest.asset_id} value={`Recorded:${r.manifest.asset_id}`}>{r.manifest.name} · v{r.manifest.version}</option>)}
        </optgroup>
      </select></label>
      <small>{kind === "Recorded" ? "单向双端点 · 示意坐标 · 仅波形接收" : "真实节点几何与轨迹 · 快速网络或波形接收"}</small>
    </div>
    {(environments.error || recordings.error) && <details><summary>资源目录读取异常</summary><ErrorState error={environments.error || recordings.error} /></details>}
    {pending && <div className="scene-switch-backdrop"><section role="alertdialog" aria-modal="true" aria-labelledby="resource-switch-title" className="scene-switch-dialog">
      <h2 id="resource-switch-title">切换实验环境</h2>
      <p>{pending.kind === kind ? "将替换当前编辑缓冲区的环境选择；其他输入保留，保存前会重新校验。" : "不同信道类型使用独立实验。当前输入暂存于本页，已有实验不会被改写。刷新前请保存。"}</p>
      {error != null && <ErrorState error={error} />}
      <button autoFocus disabled={saving} onClick={() => { setPending(null); setError(null); }}>取消</button>
      <button disabled={saving} onClick={saveAndSwitch}>{saving ? "正在保存…" : "保存当前后切换"}</button>
      <button disabled={saving} onClick={apply}>{pending.kind === kind ? "保留其他输入并更换环境" : "保留当前输入，打开另一类实验"}</button>
    </section></div>}
    {visited.map(source => <div key={source} hidden={source !== kind}>
      <ExperimentEditorContext.Provider value={{ draftId: source === initialKind ? draftId : null,
        scenarioId: source === initialKind ? scenarioId : null, selection: selections[source] ?? null,
        registerSave: save => { saves.current[source] = save; },
        reportResource: id => setBindings(v => v[source] === id ? v : { ...v, [source]: id }),
        reportState: (dirty, busy) => setStates(v => v[source]?.dirty === dirty && v[source]?.busy === busy ? v : { ...v, [source]: { dirty, busy } }) }}>
        {source === "Bellhop" ? <ExperimentAuthoringPage /> : <RecordedExperimentPage />}
      </ExperimentEditorContext.Provider>
    </div>)}
  </div>;
}
