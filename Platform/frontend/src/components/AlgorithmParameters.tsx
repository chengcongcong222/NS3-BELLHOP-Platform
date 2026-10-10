import type { AlgorithmDescriptor, AlgorithmParameter, AlgorithmSelection } from "../domain/algorithms";
import { useEffect, useState } from "react";
import { algorithmDefaults } from "../domain/algorithms";
import { planningNanosecondsInput, planningSecondsInput } from "../domain/planningConfiguration";

function DurationInput({ value, onChange }: { value: string; onChange: (value: string) => void }) {
  const [text, setText] = useState(() => planningSecondsInput(value));
  useEffect(() => {
    if (planningNanosecondsInput(text) !== value) setText(planningSecondsInput(value));
  }, [value, text]);
  return <input type="text" inputMode="decimal" autoComplete="off" value={text}
    onChange={event => { setText(event.target.value); onChange(planningNanosecondsInput(event.target.value)); }} />;
}

export function AlgorithmParameters({ algorithm, value, onChange }: {
  algorithm: AlgorithmDescriptor; value: AlgorithmSelection; onChange: (selection: AlgorithmSelection) => void;
}) {
  const set = (id: string, text: string) => onChange({ ...value, parameters: { ...value.parameters, [id]: text } });
  const field = (p: AlgorithmParameter) => <label key={p.id}>
    {p.label}{p.unit ? `（${p.unit}）` : ""}
    {p.type === "enum" || p.type === "boolean" ? <select value={value.parameters[p.id] ?? ""} onChange={e => set(p.id, e.target.value)}>
      {!value.parameters[p.id] && <option value="">请选择</option>}
      {(p.type === "boolean" ? ["true", "false"] : p.choices).map(option => <option key={option} value={option}>{p.type === "boolean" ? option === "true" ? "开启" : "关闭" : option}</option>)}
    </select> : p.type === "duration_ns" ? <DurationInput value={value.parameters[p.id] ?? ""} onChange={text => set(p.id, text)} /> : <input type="text" inputMode="decimal" autoComplete="off"
      value={value.parameters[p.id] ?? ""} onChange={e => set(p.id, e.target.value)} />}
    {p.help && <small>{p.help}</small>}
  </label>;
  return <section className="parameter-detail algorithm-parameters" aria-label={`${algorithm.label}算法参数`}>
    <h3>{algorithm.label}</h3><p className="property-help">{algorithm.description}</p>
    <div className="form-grid">{algorithm.parameters.filter(p => !p.advanced).map(field)}</div>
    {!!algorithm.parameters.filter(p => p.advanced).length && <details><summary>算法高级参数</summary>
      <div className="form-grid">{algorithm.parameters.filter(p => p.advanced).map(field)}</div>
    </details>}
    <details><summary>版本与配置管理</summary><small>版本 {algorithm.version} · 参数只作用于此算法</small>
      <button type="button" onClick={() => onChange(algorithmDefaults(algorithm))}>恢复本算法默认参数</button>
    </details>
  </section>;
}
