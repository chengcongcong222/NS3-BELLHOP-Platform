import { render, screen, within } from "@testing-library/react";
import { describe, expect, it } from "vitest";
import type { ResultDto, RunEventDto } from "../api/types";
import { matchingRunExperiment, physicalMode, resultEventStatistics, sampleSummary, targetBerReference, waveformSignalToNoise, waveformSummary } from "../domain/resultAnalysis";
import { ResultAnalysis } from "../features/resultAnalysis";
import { acceptance4Result } from "./acceptance4Golden";
import { experiment } from "./fixtures";

const result: ResultDto = {
  ...acceptance4Result,
  run_id: "analysis-run",
  projection: { ...acceptance4Result.projection, transmission_count: "1", channel_signal_count: "2", channel_no_arrival_count: "0", cycle_count: "0", reception_count: "2", local_delivery_count: "1" },
};
const events: RunEventDto[] = [
  { run_id: result.run_id, sequence: "1", trace: { kind: "Transmission", occurred_at_ns: "1000000000", payload: { transmission_id: "7", packet_id: "9", sender_node_id: "0", target: { type: "Unicast", node_id: "2" }, started_at_ns: "1000000000", ended_at_ns: "2000000000" } } },
  { run_id: result.run_id, sequence: "2", trace: { kind: "ChannelOutcome", occurred_at_ns: "1000000000", payload: { transmission_id: "7", receiver_node_id: "2", outcome: { type: "Signal", first_arrival_delay_ns: "500000000", aggregate_transmission_loss_db: 30, path_count: "2" } } } },
  { run_id: result.run_id, sequence: "3", trace: { kind: "ChannelOutcome", occurred_at_ns: "1000000000", payload: { transmission_id: "7", receiver_node_id: "3", outcome: { type: "Signal", first_arrival_delay_ns: "900000000", aggregate_transmission_loss_db: 40, path_count: "4" } } } },
  { run_id: result.run_id, sequence: "4", trace: { kind: "Reception", occurred_at_ns: "2500000000", payload: { reception_id: "4", transmission_id: "7", packet_id: "9", receiver_node_id: "2", disposition: "LocalDelivery", quality: { signal_to_noise_ratio_db: 20, eb_n0_db: 20, bit_error_rate: 0.0001, source: "Modeled" } } } },
  { run_id: result.run_id, sequence: "5", trace: { kind: "Reception", occurred_at_ns: "2900000000", payload: { reception_id: "5", transmission_id: "7", packet_id: "9", receiver_node_id: "3", disposition: "Overheard", quality: null } } },
];

describe("result statistics retain measurement scope", () => {
  it("counts recorded channel events as covered without inventing Bellhop loss or delay samples", () => {
    const recorded: RunEventDto[] = events.map(event => event.trace.kind === "ChannelOutcome" ? {
      ...event, trace: { ...event.trace, payload: { ...event.trace.payload, outcome: {
        type: "Recorded", reference_delay_ns: "2000000000", receive_duration_ns: "500000000",
        gain_reference: "Normalized", delay_reference: "Modeled",
      } } },
    } : event);
    const stats = resultEventStatistics({ ...result, fusion_results: [] }, recorded, true);
    expect(stats.complete).toBe(true);
    expect(stats.loss).toEqual([]);
    expect(stats.delay).toEqual([]);
    expect(stats.paths).toEqual([]);
    expect(resultEventStatistics({ ...result, fusion_results: [] }, recorded.slice(1), true).complete).toBe(false);
  });
  it("computes waveform SNR from recorded powers without replacing absent or unbounded values with zero", () => {
    expect(waveformSignalToNoise({ signal_power_upa2: 1000, noise_power_upa2: 1 })).toBe(30);
    expect(waveformSignalToNoise({ signal_power_upa2: 1e300, noise_power_upa2: 1e-300 })).toBe(6000);
    expect(waveformSignalToNoise({ signal_power_upa2: 1, noise_power_upa2: 0 })).toBe("no-noise");
    expect(waveformSignalToNoise({ signal_power_upa2: 0, noise_power_upa2: 1 })).toBe("no-signal");
    expect(waveformSignalToNoise()).toBeNull();
    expect(waveformSignalToNoise({ signal_power_upa2: -1, noise_power_upa2: 1 })).toBeNull();
  });
  it("does not mistake missing waveform evidence for mode A", () => {
    expect(physicalMode(result)).toBe("unknown");
    expect(physicalMode(result, "Channel1Abstract")).toBe("A");
    expect(physicalMode(result, "Channel2Waveform")).toBe("B");
  });
  it("uses weighted bit counts, not an average of frame BER or CRC failures", () => {
    const item = { ...result, visualization: { positions_complete: true, generated_feature_count: "0", positions: [], fusion_sources: [], waveform: [
      { transmission_id: "1", receiver_node_id: "2", payload_bit_count: "10", bit_error_count: "1", crc_ok: false },
      { transmission_id: "2", receiver_node_id: "2", payload_bit_count: "90", bit_error_count: "0", crc_ok: true },
    ] } };
    expect(waveformSummary(item)).toEqual({ frames: 2, bits: 100n, errors: 1n, ber: 0.01, crcPassed: 1, crcFailed: 1 });
    expect(physicalMode(item)).toBe("B");
    expect(waveformSummary(result).ber).toBeNull();
  });
  it("counts a unicast target separately from overhearing and keeps link timing distinct", () => {
    const stats = resultEventStatistics({ ...result, fusion_results: [] }, [...events, events[0], { ...events[0], run_id: "another-run" }], true);
    expect(stats.complete).toBe(true);
    expect(stats.targetAttempts).toBe(1);
    expect(stats.targetsDecoded).toBe(1);
    expect(stats.targetDecodeRatio).toBe(1);
    expect(stats.overheard).toBe(1);
    expect(stats.deliveredPairs).toBe(1);
    expect(stats.receiveDurationSeconds).toEqual([1.5]);
    expect(stats.links.size).toBe(1);
  });
  it("will not produce a run-wide ratio from partial history or a missing target", () => {
    expect(resultEventStatistics(result, events.slice(0, 4), true).targetDecodeRatio).toBeNull();
    expect(resultEventStatistics(result, events, false).targetDecodeRatio).toBeNull();
    const failedEvents = events.map(event => event.trace.kind === "Reception" && event.trace.payload.receiver_node_id === "2"
      ? { ...event, trace: { ...event.trace, payload: { ...event.trace.payload, disposition: "NotDecoded" as const } } } : event);
    expect(resultEventStatistics({ ...result, fusion_results: [], projection: { ...result.projection, local_delivery_count: "0" } }, failedEvents, true).targetDecodeRatio).toBe(0);
  });
  it("summarizes finite measured samples without adding sentinel samples", () => {
    expect(sampleSummary([0.5, 0.9])).toEqual({ count: 2, minimum: 0.5, mean: 0.7, maximum: 0.9 });
    expect(sampleSummary([])).toBeNull();
  });
  it("does not use another experiment or revision as the run's configured rate", () => {
    expect(matchingRunExperiment(result, experiment)).toBe(experiment);
    expect(matchingRunExperiment(result, { ...experiment, version: "999" })).toBeNull();
    expect(matchingRunExperiment(result, { ...experiment, experiment_id: "other" })).toBeNull();
    expect(matchingRunExperiment(result)).toBeNull();
  });
  it("compares waveform BER only for declared targets, without averaging in overhearing", () => {
    const item: ResultDto = { ...result, fusion_results: [], visualization: { positions_complete: true, generated_feature_count: "0", positions: [], fusion_sources: [], waveform: [
      { transmission_id: "7", receiver_node_id: "2", payload_bit_count: "10", bit_error_count: "2", crc_ok: false },
      { transmission_id: "7", receiver_node_id: "3", payload_bit_count: "90", bit_error_count: "0", crc_ok: true },
    ] } };
    const evidence = targetBerReference(item, resultEventStatistics(item, events, true), "B");
    expect(evidence).toEqual({ groups: [{ source: "Waveform", maximum: 0.2, count: "1" }], missing: 0, complete: true, targetAttempts: 1 });
    const absent = { ...item, visualization: { ...item.visualization!, waveform: item.visualization!.waveform.slice(1) } };
    expect(targetBerReference(absent, resultEventStatistics(absent, events, true), "B")).toEqual({ groups: [], missing: 1, complete: false, targetAttempts: 1 });
  });
  it("keeps modeled and formally measured BER sources distinct", () => {
    const item = { ...result, fusion_results: [] };
    expect(targetBerReference(item, resultEventStatistics(item, events, true), "A").groups).toEqual([{ source: "Modeled", maximum: 0.0001, count: "1" }]);
    const measured = events.map(event => event.trace.kind === "Reception" && event.trace.payload.quality ? { ...event, trace: { ...event.trace, payload: { ...event.trace.payload, quality: { ...event.trace.payload.quality, source: "Measured" as const } } } } : event);
    expect(targetBerReference(item, resultEventStatistics(item, measured, true), "A").groups).toEqual([{ source: "Measured", maximum: 0.0001, count: "1" }]);
    const historical = targetBerReference(item, resultEventStatistics(item, []), "unknown");
    expect(historical.groups[0].source).toBe("Unspecified");
    expect(historical.complete).toBe(false);
  });
});

describe("mode-aware scientific result presentation", () => {
  it("limits ordinary dB and second displays but preserves small BER and exact time evidence", () => {
    const precise = events.map(event => event.trace.kind === "ChannelOutcome" && event.trace.payload.outcome.type === "Signal"
      ? { ...event, trace: { ...event.trace, payload: { ...event.trace.payload, outcome: { ...event.trace.payload.outcome,
        aggregate_transmission_loss_db: 49.9515, first_arrival_delay_ns: "712345678",
      } } } } : event.trace.kind === "Reception" && event.trace.payload.quality ? { ...event, trace: { ...event.trace, payload: { ...event.trace.payload,
        quality: { ...event.trace.payload.quality, bit_error_rate: 1.2341e-9 },
      } } } : event);
    const preciseResult = { ...result, projection: { ...result.projection, simulation_duration_ns: "180123456789" } };
    render(<ResultAnalysis result={preciseResult} events={precise} physicalLayer="Channel1Abstract" />);
    expect(within(screen.getByText("平均信号衰减").closest("div")!).getByText("49.95")).toBeTruthy();
    expect(within(screen.getByText("平均信号传播用时").closest("div")!).getByText("0.712")).toBeTruthy();
    expect(screen.getByTitle("180.123456789 s (180123456789 ns)").textContent).toBe("180.123 s");
    expect(screen.getAllByText("1.234e-9").length).toBeGreaterThan(0);
  });

  it("uses waveform rather than modeled SNR in mode B and reports the partial power coverage", () => {
    const withPowers = events.map(event => event.trace.kind === "Reception" && event.trace.payload.receiver_node_id === "2"
      ? { ...event, trace: { ...event.trace, payload: { ...event.trace.payload, waveform: {
        payload_bit_count: "100", bit_error_count: "2", crc_ok: false, signal_power_upa2: 1000, noise_power_upa2: 1,
      } } } } : event);
    const stats = resultEventStatistics(result, withPowers);
    expect(stats.snr).toEqual([20]);
    expect(stats.waveformSnr).toEqual([30]);
    expect(stats.waveformPowerMissing).toBe(1);
    render(<ResultAnalysis result={result} events={withPowers} physicalLayer="Channel2Waveform" />);
    const summary = screen.getByText("信号比背景噪声强多少？").closest("div")!;
    expect(within(summary).getByText("30")).toBeTruthy();
    expect(within(summary).queryByText("20")).toBeNull();
    expect(screen.getByText(/波形信噪比：1 次可计算，0 次未加噪声，0 次无信号功率，1 次未保存功率/)).toBeTruthy();
  });

  it("labels noiseless mode B frames without inventing a finite SNR sample", () => {
    const noiseless = events.map(event => event.trace.kind === "Reception"
      ? { ...event, trace: { ...event.trace, payload: { ...event.trace.payload, waveform: {
        payload_bit_count: "100", bit_error_count: "0", crc_ok: true, signal_power_upa2: 1, noise_power_upa2: 0,
      } } } } : event);
    render(<ResultAnalysis result={result} events={noiseless} physicalLayer="Channel2Waveform" />);
    const summary = screen.getByText("信号比背景噪声强多少？").closest("div")!;
    expect(within(summary).getByText("未加噪声")).toBeTruthy();
    expect(screen.queryByRole("rowheader", { name: "接收信噪比 dB" })).toBeNull();
    expect(screen.getByText(/波形信噪比：0 次可计算，2 次未加噪声/)).toBeTruthy();
  });

  it("shows readable seconds in the overview and keeps exact nanoseconds in technical detail", () => {
    render(<ResultAnalysis result={result} />);
    const duration = within(document.querySelector<HTMLElement>(".analysis-summary")!).getByTitle("180 s (180000000000 ns)");
    expect(duration.textContent).toBe("180 s");
    expect(document.querySelector(".analysis-summary")?.textContent).not.toContain("180000000000");
    expect(screen.getByText("180000000000")).toBeTruthy();
  });
  it("shows mode A theory but no waveform CRC table or automated acceptance verdict", () => {
    render(<ResultAnalysis result={result} events={events} physicalLayer="Channel1Abstract" eventStreamComplete />);
    expect(screen.getByText("模式 A · 快速抽象仿真")).toBeTruthy();
    expect(screen.getByText("理论误码率（BPSK 白噪声模型）")).toBeTruthy();
    expect(screen.queryByRole("columnheader", { name: "帧 CRC" })).toBeNull();
    expect(screen.queryByText(/满足要求|PASS|验收通过/)).toBeNull();
    expect(screen.getByText(/这是业务统计缺口，与选择模式 A 或 B 无关/)).toBeTruthy();
  });
  it("starts with plain-language quantities and keeps specialist tables collapsed", () => {
    render(<ResultAnalysis result={result} events={events} physicalLayer="Channel1Abstract" />);
    expect(screen.getByText("发出了多少次？")).toBeTruthy();
    expect(screen.getByText("有多少次数据真正送达？")).toBeTruthy();
    const propagation = screen.getByText("平均信号传播用时").closest("div")!;
    expect(within(propagation).getByText("0.7")).toBeTruthy();
    expect(within(propagation).getByText("s")).toBeTruthy();
    expect(screen.getByText(/一次广播也只记一次/)).toBeTruthy();
    expect(screen.getByText(/不能用“送达次数 ÷ 发送次数”直接计算业务成功率/)).toBeTruthy();
    expect(screen.getByRole("img", { name: "信号传播用时趋势" }).querySelector(".analysis-curve")).toBeNull();
    expect(screen.getByRole("img", { name: "信号传播用时趋势" }).querySelectorAll(".analysis-point")).toHaveLength(2);
    expect(screen.getByRole("img", { name: "发送次数随时间增加趋势" }).querySelector(".analysis-curve")).toBeTruthy();
    expect(screen.getByText("累计目标有向链路").closest("details")?.open).toBe(false);
    expect(screen.getByText("没有传播路径的记录").closest("details")?.open).toBe(false);
    for (const heading of screen.getAllByText("样本数 n")) expect(heading.closest("details")?.open).toBe(false);
    for (const table of screen.getAllByRole("table").filter(item => item.getAttribute("aria-label") !== "参考要求与本次记录")) expect(table.closest("details")?.open).toBe(false);
  });
  it("does not interpret a mode B with no received frames as mode A or zero BER", () => {
    render(<ResultAnalysis result={result} physicalLayer="Channel2Waveform" />);
    expect(screen.getByText(/选择模式 B，但结果未保存波形接收帧/)).toBeTruthy();
    expect(screen.queryByText("理论误码率（BPSK 白噪声模型）")).toBeNull();
  });
  it("shows actual weighted BER and CRC with explicit receive-frame scope", () => {
    const item = { ...result, visualization: { positions_complete: true, generated_feature_count: "0", positions: [], fusion_sources: [], waveform: [
      { transmission_id: "7", receiver_node_id: "2", payload_bit_count: "120", bit_error_count: "6", crc_ok: false },
      { transmission_id: "7", receiver_node_id: "3", payload_bit_count: "120", bit_error_count: "0", crc_ok: true },
    ] } };
    render(<ResultAnalysis result={item} />);
    const row = screen.getByText("加权实际误码率 BER").closest("tr")!;
    expect(within(row).getByText("0.025")).toBeTruthy();
    expect(screen.getByText("每次发送 × 接收节点，含旁听")).toBeTruthy();
    expect(screen.getByText("6 / 120")).toBeTruthy();
    expect(screen.getByText("失败")).toBeTruthy();
    expect(screen.getByText("2.5%")).toBeTruthy();
    expect(screen.getByText("1 次通过 / 1 次未通过")).toBeTruthy();
    expect(screen.getByText(/通过只表示校验未检出问题，不表示已交给业务程序/)).toBeTruthy();
    expect(screen.getByText("失败").closest("details")?.open).toBe(false);
    expect(screen.getByRole("columnheader", { name: "帧 CRC" }).closest("details")?.open).toBe(false);
  });
  it("does not treat CommunicationOnly's lack of fusion as a failure", () => {
    render(<ResultAnalysis result={{ ...result, fusion_results: [] }} experiment={{ ...experiment, fusion: { ...experiment.fusion, workload: "CommunicationOnly" } }} />);
    expect(screen.getAllByText(/本实验未启用融合业务/).length).toBeGreaterThan(0);
    expect(screen.queryByText(/尚未形成融合结果/)).toBeNull();
  });
  it("only plots populated event series and retains actual fusion identities", () => {
    const item = { ...result, visualization: { positions_complete: true, generated_feature_count: "8", positions: [], waveform: [], fusion_sources: [{ fusion_sequence: "1", node_id: "0", observation_sequence: "3" }] } };
    render(<ResultAnalysis result={item} />);
    expect(screen.queryByRole("img", { name: "传播时延趋势" })).toBeNull();
    expect(screen.getByText(/N0 \/ #3/)).toBeTruthy();
    expect(screen.getByText(/不从发送端报告补造/)).toBeTruthy();
    expect(screen.getByText(/同一个节点可以贡献多条观测/)).toBeTruthy();
    expect(screen.getByText(/不是算法单独计算的耗时/)).toBeTruthy();
    expect(screen.getByText(/N0 \/ #3/).closest("details")?.open).toBe(false);
  });
  it("places reference requirements beside actual frozen settings without inventing a verdict", () => {
    render(<ResultAnalysis result={result} experiment={{ ...experiment, phy: { ...experiment.phy, bit_rate_bits_per_second: "120" } }} physicalLayer="Channel1Abstract" events={events} />);
    const reference = screen.getByRole("table", { name: "参考要求与本次记录" });
    const rate = within(reference).getByRole("rowheader", { name: "节点发送速率" }).closest("tr")!;
    expect(within(rate).getByText("60 bit/s")).toBeTruthy();
    expect(within(rate).getByText("120 bit/s")).toBeTruthy();
    expect(within(rate).getByText(/不.*吞吐量替代/)).toBeTruthy();
    expect(within(reference).getByText("BER ≤ 1e-4")).toBeTruthy();
    expect(within(reference).getByText(/模型估计：最大/)).toBeTruthy();
    expect(screen.queryByText(/满足要求|PASS|验收通过/)).toBeNull();
  });
  it("never replaces missing frozen settings with the 60 bit/s reference", () => {
    render(<ResultAnalysis result={result} experiment={{ ...experiment, version: "999" }} />);
    const reference = screen.getByRole("table", { name: "参考要求与本次记录" });
    const row = within(reference).getByRole("rowheader", { name: "节点发送速率" }).closest("tr")!;
    expect(within(row).getByText("本次配置快照未提供")).toBeTruthy();
    expect(within(row).getAllByText("60 bit/s")).toHaveLength(1);
  });
  it("does not rename a saved fusion window as the full detection period", () => {
    render(<ResultAnalysis result={result} experiment={experiment} />);
    const reference = screen.getByRole("table", { name: "参考要求与本次记录" });
    expect(within(reference).getByText("每轮不少于 5 点")).toBeTruthy();
    expect(within(reference).getByText("第 1 轮：5 条")).toBeTruthy();
    expect(within(reference).getByText(/不要求五个不同节点/)).toBeTruthy();
    const period = within(reference).getByRole("rowheader", { name: "完整探测周期" }).closest("tr")!;
    expect(within(period).getByText("≤ 3 min（180 s）")).toBeTruthy();
    expect(within(period).getByText("尚未单独保存完整探测周期")).toBeTruthy();
    expect(within(period).getByText(/已记录的是融合窗口耗时：第 1 轮 120 s/)).toBeTruthy();
    expect(screen.getByText(/不能把上述时间或相邻结果的时间差冒充探测周期/)).toBeTruthy();
  });
});
