import type { AlgorithmCatalog } from "../domain/algorithms";
// Test-only catalog. Production always reads the real compiled Worker.
export const algorithmCatalog: AlgorithmCatalog = {
  schema: "platform.algorithm-catalog.v1", worker_sha256: "b".repeat(64), algorithms: [{
    id: "platform.adaptive-tdma", version: "1.0.0", package_sha256: "a".repeat(64), interface: "planning-mac-v1",
    label: "自适应 TDMA", description: "按待发数据、发送时长和传播时延分配机会。", parameters: [{
      id: "safety_margin_ns", label: "额外安全间隔", type: "duration_ns", unit: "s", default: "0", min: "0", max: "60000000000", help: "通常保持 0。", advanced: true, choices: [],
    }],
  }],
};
