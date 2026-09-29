const statusLabels: Record<string, string> = { Created: "已创建", Running: "运行中", Completed: "已完成", Failed: "运行失败", Pass: "通过", Fail: "未通过", Valid: "有效", NotEvaluated: "未评估", NotFullyEvaluated: "尚未完整评估", None: "未配置", Modeled: "模型计算", Measured: "实测", External: "外部证据" };
export const productLabel = (value: string) => statusLabels[value] ?? value;
export const physicalModeLabel = (value?: string | null) => value === "Channel1Abstract" ? "模式 A · 快速网络仿真" : value === "Channel2Waveform" ? "模式 B · 波形通信仿真" : "物理模式未提供";
