import { useEffect } from "react";
import { useQuery } from "@tanstack/react-query";
import { apiClient } from "../api/client";
import { ErrorState } from "./common";

import type { OsdmConfig } from "../domain/experimentPresets";

export function OsdmSettings({ layer, value, onChange, allowInitialPreset }: {
  layer: "Channel2Waveform" | "Channel3Recorded"; value: OsdmConfig | null;
  onChange: (value: OsdmConfig) => void; allowInitialPreset: boolean;
}) {
  const presets = useQuery({ queryKey: ["experiment-presets"], queryFn: apiClient.experimentPresets, staleTime: Infinity });
  const preset = presets.data?.find(p => p.physical_layer === layer);
  useEffect(() => {
    if (!value && allowInitialPreset && preset) onChange({ ...preset.osdm });
  }, [value, allowInitialPreset, preset, onChange]);
  const standard = value && preset && Object.keys(preset.osdm).every(k =>
    value[k as keyof OsdmConfig] === preset.osdm[k as keyof OsdmConfig]);
  return <div className="osdm-settings">
    <p role="status">{!value ? "等待完整 OSDM 配置" : standard ? `${preset!.name} · v${preset!.version}` : "OSDM · 已自定义（保留保存值）"}</p>
    {presets.error && <ErrorState error={presets.error} />}
    {!allowInitialPreset && !value && <p role="alert">历史实验缺少完整 OSDM 配置，请明确选择预设；不会自动覆盖历史值。</p>}
    <details className="parameter-detail"><summary>OSDM 分块与同步</summary>
      <small>固定工程预设，不是在线自适应。完整参数随实验保存。</small>
      {preset && <button type="button" onClick={() => onChange({ ...preset.osdm })}>恢复该版本预设</button>}
      {value && <>
        <label>星座<select value={value.constellation} onChange={e => onChange({ ...value, constellation: e.target.value })}><option value="Bpsk">BPSK</option><option value="Qpsk">QPSK</option></select></label>
        {([['block_length', '分块长度 K'], ['vector_length', '向量长度 M'], ['cyclic_prefix', '循环前缀'], ['maximum_sync_search', '同步搜索样本数'], ['minimum_sync_correlation', '同步相关门限']] as const).map(([key, label]) =>
          <label key={key}>{label}<input type="number" value={value[key]} step={key === "minimum_sync_correlation" ? .01 : 1} onChange={e => onChange({ ...value, [key]: Number(e.target.value) })} /></label>)}
      </>}
    </details>
  </div>;
}
