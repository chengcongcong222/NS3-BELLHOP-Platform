export interface OsdmConfig {
  block_length: number; vector_length: number; cyclic_prefix: number;
  constellation: string; minimum_sync_correlation: number; maximum_sync_search: number;
}
export interface ExperimentPreset {
  id: string; version: number; name: string; physical_layer: string; osdm: OsdmConfig;
}
export function isExperimentPresets(value: unknown): value is ExperimentPreset[] {
  const keys = ["block_length", "vector_length", "cyclic_prefix", "minimum_sync_correlation", "maximum_sync_search"];
  return Array.isArray(value) && value.every(p => p && typeof p.id === "string" &&
    Number.isInteger(p.version) && p.version > 0 && typeof p.name === "string" &&
    ["Channel2Waveform", "Channel3Recorded"].includes(p.physical_layer) && p.osdm &&
    ["Bpsk", "Qpsk"].includes(p.osdm.constellation) && keys.every(k => typeof p.osdm[k] === "number" && Number.isFinite(p.osdm[k])));
}
