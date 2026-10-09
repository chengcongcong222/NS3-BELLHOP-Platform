import { act, fireEvent, screen, waitFor, within } from "@testing-library/react";
import { afterEach, describe, expect, it, vi } from "vitest";
import { experiment, result, run, scenario } from "./fixtures";
import { installApi, renderRoute } from "./render";

class ObservedStream {
  static current: ObservedStream | null = null;
  private listeners = new Map<string, Array<(event: MessageEvent<string>) => void>>();
  onerror: ((event: Event) => void) | null = null;
  closed = false;
  constructor(_url: string) { ObservedStream.current = this; }
  addEventListener(type: string, listener: (event: MessageEvent<string>) => void) {
    this.listeners.set(type, [...this.listeners.get(type) ?? [], listener]);
  }
  close() { this.closed = true; }
  emit(sequence: string, kind: string, at: string, payload: object) {
    const event = { lastEventId: sequence, data: JSON.stringify({ run_id: run.run_id, sequence,
      trace: { kind, occurred_at_ns: at, payload } }) } as MessageEvent<string>;
    this.listeners.get("run-event")?.forEach(listener => listener(event));
  }
  end(sequence: string) {
    this.listeners.get("stream-end")?.forEach(listener => listener({ data: JSON.stringify({ last_sequence: sequence }) } as MessageEvent<string>));
  }
}
const tx = { transmission_id: "7", packet_id: "8", sender_node_id: "0", target: { type: "Unicast", node_id: "2" },
  started_at_ns: "0", ended_at_ns: "1000000000" };
const arrival = { transmission_id: "7", receiver_node_id: "2" };
const reception = { reception_id: "9", transmission_id: "7", packet_id: "8", receiver_node_id: "2", disposition: "LocalDelivery", quality: null };
const commit = { cycle_id: "1", base_snapshot_version: "0", committed_snapshot_version: "1", committed_at_ns: "2000000000" };
const visibleCount = () => document.querySelector(".studio-product")?.getAttribute("data-visible-event-count");
const eventDots = () => document.querySelectorAll(".event-lane button");

async function prepare(lifecycle: "Completed" | "Running" | "Failed" = "Completed", overrides: Parameters<typeof installApi>[0] = {}) {
  ObservedStream.current = null;
  vi.stubGlobal("EventSource", ObservedStream);
  const record = { ...run, lifecycle, failure: lifecycle === "Failed"
    ? { code: "FailedPrecondition", message: "TDMA slot cannot contain payload airtime and guard interval." } : null };
  const fetch = installApi({ [`/runs/${run.run_id}`]: { body: record },
    [`/runs/${run.run_id}/results`]: { status: 404, body: { error: { code: "ResultNotFound", message: "本测试仅观察事件。" } } },
    ...overrides,
  });
  renderRoute(`/runs/${run.run_id}?presentation=live`);
  await screen.findByRole("img", { name: "运行画布" });
  await waitFor(() => expect(ObservedStream.current).not.toBeNull());
  // Explicit diagnostic display for exhaustive playback tests.
  for (const name of ["开始发送", "信道计算", "周期完成"]) fireEvent.click(screen.getByRole("button", { name }));
  return fetch;
}

afterEach(() => vi.useRealTimers());

describe("new run progressive observation", () => {
  it("retains a permanent communication slot and distinct statistics/log shells", async () => {
    await prepare();
    const slot = screen.getByLabelText("当前通信过程");
    expect(slot.textContent).toContain("当前无选中通信事件");
    expect(screen.getByRole("region", {name:"运行统计面板"})).toBeTruthy();
    expect(screen.getByRole("region", {name:"事件日志面板"})).toBeTruthy();
    act(() => ObservedStream.current!.emit("1", "Transmission", "0", tx));
    fireEvent.click(screen.getByRole("button", {name:"实时跟随"}));
    expect(screen.getByLabelText("当前通信过程")).toBe(slot);
  });
  it("leaves historical node capabilities unknown when the frozen definition is unavailable", async () => {
    await prepare("Completed", {
      [`/runs/${run.run_id}/definition-context`]: { status: 404, body: { detail: "Not Found" } },
      [`/runs/${run.run_id}/results`]: { body: { ...result, visualization: { positions_complete: true,
        generated_feature_count: "0", fusion_sources: [], waveform: [], positions: [
          { node_id: "9", at_ns: "0", x_meters: 0, y_meters: 0, z_meters: -20 },
        ] } } },
    });
    const list = document.querySelector<HTMLElement>(".runtime-node-list")!;
    expect(within(list).getByText("能力未记录 · 已发送 0 次")).toBeTruthy();
    expect(within(list).queryByText(/^收发|^仅接收|^仅发送|^停用/)).toBeNull();
  });

  it("shows frozen hardware capabilities and prefix send counts independently of the display filters", async () => {
    const capabilities = [[false, true], [true, false], [true, true], [false, false]];
    const frozenScene = { ...scenario, fusion_center_node_id: "0", nodes: capabilities.map(([can_transmit, can_receive], i) => ({
      ...scenario.nodes[0], node_id: String(i), can_transmit, can_receive,
      initial_position: { x_meters: i * 100, y_meters: 0, z_meters: -20 },
    })) };
    await prepare("Completed", {
      [`/runs/${run.run_id}/definition-context`]: { body: { run_id: run.run_id, source: "AuthoredDefinition", definition_sha256: "d".repeat(64),
        scenario: frozenScene, experiment: { ...experiment, execution_mode: "PlanningV1", fusion_receiver_node_id: "2", fusion: { ...experiment.fusion, workload: "BearingFusion" } }, execution: {}, limitations: [] } },
    });
    vi.useFakeTimers();
    act(() => {
      ObservedStream.current!.emit("1", "Transmission", "0", { ...tx, sender_node_id: "1" });
      ObservedStream.current!.emit("2", "Transmission", "1000000000", { ...tx, transmission_id: "8", sender_node_id: "2", target: { type: "Unicast", node_id: "0" }, started_at_ns: "1000000000", ended_at_ns: "2000000000" });
      ObservedStream.current!.emit("3", "CycleCommit", "2000000000", commit);
      ObservedStream.current!.end("3");
    });
    const list = document.querySelector<HTMLElement>(".runtime-node-list")!;
    const nodeButton = (id: string) => within(list).getByText(`N${id}`, { selector: "strong" }).closest("button")!;
    expect(within(nodeButton("0")).getByText("仅接收 · 已发送 0 次")).toBeTruthy();
    expect(within(nodeButton("1")).getByText("仅发送 · 已发送 0 次")).toBeTruthy();
    expect(within(nodeButton("2")).getByText("收发 · 已发送 0 次")).toBeTruthy();
    expect(within(nodeButton("3")).getByText("停用 · 已发送 0 次")).toBeTruthy();
    expect(within(nodeButton("2")).getByText("当前融合处理节点")).toBeTruthy();
    expect(within(nodeButton("0")).queryByText("当前融合处理节点")).toBeNull();
    fireEvent.click(screen.getByRole("button", { name: "完整回放" }));
    expect(within(nodeButton("1")).getByText("仅发送 · 已发送 1 次")).toBeTruthy();
    expect(within(nodeButton("2")).getByText("收发 · 已发送 0 次")).toBeTruthy();
    fireEvent.click(screen.getByRole("button", { name: "实时跟随" }));
    expect(within(nodeButton("2")).getByText("收发 · 已发送 1 次")).toBeTruthy();
    fireEvent.click(screen.getByRole("button", { name: "开始发送" }));
    fireEvent.change(screen.getByLabelText("通信观测层"), { target: { value: "physical" } });
    expect(within(nodeButton("1")).getByText("仅发送 · 已发送 1 次")).toBeTruthy();
    expect(within(nodeButton("2")).getByText("收发 · 已发送 1 次")).toBeTruthy();
    expect(within(nodeButton("0")).getByText("仅接收 · 已发送 0 次")).toBeTruthy();
  });

  it("explains a completed NoArrival-only run without enabling hidden kinds or inventing arrivals", async () => {
    await prepare();
    for (const name of ["开始发送", "信道计算", "周期完成"]) fireEvent.click(screen.getByRole("button", { name }));
    act(() => {
      ObservedStream.current!.emit("1", "Transmission", "0", tx);
      ObservedStream.current!.emit("2", "ChannelOutcome", "0", { transmission_id: "7", receiver_node_id: "2", outcome: { type: "NoArrival" } });
      ObservedStream.current!.emit("3", "CycleCommit", "2000000000", commit);
    });
    fireEvent.click(screen.getByRole("button", { name: "实时跟随" }));
    expect(screen.getByText("等待首个事件，或使用下方时间轴开始观看。")).toBeTruthy();
    act(() => ObservedStream.current!.end("3"));
    expect(screen.getByText(/当前显示类型没有事件；可在下方显示设置中启用/)).toBeTruthy();
    expect(screen.getByText(/当前观看进度已记录 1 次无传播路径/)).toBeTruthy();
    expect(screen.queryByText("等待首个事件，或使用下方时间轴开始观看。")).toBeNull();
    expect(screen.getByRole("button", { name: "开始发送" }).getAttribute("aria-pressed")).toBe("false");
    expect(screen.getByRole("button", { name: "信道计算" }).getAttribute("aria-pressed")).toBe("false");
    expect(eventDots()).toHaveLength(0);
    expect(screen.queryAllByTestId("communication-arrow")).toHaveLength(0);
    const metrics = screen.getByRole("region", { name: "当前展示进度统计" });
    expect(within(within(metrics).getByText("累计发送").parentElement!).getByText("1")).toBeTruthy();
    expect(within(within(metrics).getByText("解码失败").parentElement!).getByText("0")).toBeTruthy();
    fireEvent.click(screen.getByRole("button", { name: "信道计算" }));
    expect(screen.getByRole("button", { name: "检查事件序列 2" })).toBeTruthy();
    expect(screen.queryByText(/当前显示类型没有事件/)).toBeNull();
  });

  it("keeps the waiting state for a genuinely Running run even if its current event stream ended", async () => {
    await prepare("Running");
    act(() => ObservedStream.current!.end("0"));
    expect(screen.getByText("等待首个事件，或使用下方时间轴开始观看。")).toBeTruthy();
    expect(screen.queryByText(/当前显示类型没有事件/)).toBeNull();
    expect(screen.getByText("Running")).toBeTruthy();
  });

  it("plays and seeks only displayed events while retaining hidden transmissions and final cycle statistics", async () => {
    await prepare();
    for (const name of ["开始发送", "信道计算", "周期完成"]) fireEvent.click(screen.getByRole("button", { name }));
    vi.useFakeTimers();
    act(() => {
      ObservedStream.current!.emit("1", "Transmission", "0", tx);
      ObservedStream.current!.emit("2", "SignalArrival", "500000000", arrival);
      ObservedStream.current!.emit("3", "SignalArrival", "750000000", { ...arrival, receiver_node_id: "3" });
      ObservedStream.current!.emit("4", "Reception", "1000000000", reception);
      ObservedStream.current!.emit("5", "CycleCommit", "2000000000", commit);
    });
    await act(async () => { await vi.advanceTimersByTimeAsync(100); });
    expect(visibleCount()).toBe("2");
    expect(document.querySelector(".run-topology")?.classList.contains("phase-signalarrival")).toBe(true);
    expect(eventDots()).toHaveLength(1);
    const metrics = screen.getByRole("region", { name: "当前展示进度统计" });
    expect(within(within(metrics).getByText("累计发送").parentElement!).getByText("1")).toBeTruthy();
    await act(async () => { await vi.advanceTimersByTimeAsync(500); });
    expect(visibleCount()).toBe("4");
    expect(document.querySelector(".run-topology")?.classList.contains("phase-reception")).toBe(true);
    expect(eventDots()).toHaveLength(2);
    expect(screen.queryByRole("button", { name: "检查事件序列 3" })).toBeNull();
    act(() => ObservedStream.current!.end("5"));
    await act(async () => { await vi.advanceTimersByTimeAsync(1000); });
    expect(visibleCount()).toBe("5");
    expect(screen.getByText("已展示周期").parentElement?.textContent).toContain("1");
    expect(screen.getByRole("button", { name: "播放" })).toBeTruthy();
    fireEvent.change(screen.getByLabelText("按仿真时间定位"), { target: { value: "0" } });
    expect(visibleCount()).toBe("2");
    expect(document.querySelector(".run-topology")?.classList.contains("phase-signalarrival")).toBe(true);
    fireEvent.click(screen.getByRole("button", { name: "完整回放" }));
    expect(visibleCount()).toBe("2");
  });

  it("applies the fusion display switch to the canvas, log and event details together", async () => {
    await prepare(); vi.useFakeTimers();
    const center = scenario.nodes[0].node_id;
    act(() => {
      ObservedStream.current!.emit("1", "Fusion", "1000000000", { fusion_sequence: "1", fusion_center_node_id: center,
        started_at_ns: "0", completed_at_ns: "1000000000", observation_count: "6", estimated_target_x_meters: 10, estimated_target_y_meters: 20 });
      ObservedStream.current!.end("1");
    });
    fireEvent.click(screen.getByRole("button", { name: "实时跟随" }));
    expect(document.querySelector(".fusion-summary")).not.toBeNull();
    expect(screen.getByRole("img", { name: "运行画布" }).textContent).toContain(`N${center} · 融合完成`);
    const metrics = screen.getByRole("region", { name: "当前展示进度统计" }).textContent;
    fireEvent.click(screen.getByRole("button", { name: "融合结果" }));
    expect(document.querySelector(".fusion-summary, .event-technical, .lane-fusion")).toBeNull();
    expect(screen.getByRole("img", { name: "运行画布" }).textContent).not.toContain("融合完成");
    expect(screen.getByRole("region", { name: "当前展示进度统计" }).textContent).toBe(metrics);
  });

  it("uses captured historical waveform mode consistently and reports actual bit sample coverage", async () => {
    await prepare("Completed", {
      [`/runs/${run.run_id}/definition-context`]: { body: { run_id: run.run_id, source: "Preset", definition_sha256: null,
        scenario, experiment: { ...experiment, phy: { ...experiment.phy, physical_layer: undefined } }, execution: {}, limitations: [] } },
      [`/runs/${run.run_id}/results`]: { body: { ...result, provenance: { physical_layer: "Channel2Waveform" } } },
    });
    vi.useFakeTimers();
    act(() => {
      ObservedStream.current!.emit("1", "Transmission", "0", tx);
      ObservedStream.current!.emit("2", "SignalArrival", "500000000", arrival);
      ObservedStream.current!.emit("3", "Reception", "1000000000", { ...reception, waveform: { payload_bit_count: "100", bit_error_count: "2", crc_ok: false, signal_power_upa2: 100, noise_power_upa2: 1 } });
      ObservedStream.current!.end("3");
    });
    fireEvent.click(screen.getByRole("button", { name: "实时跟随" }));
    expect(screen.getByText("实际误码率 BER")).toBeTruthy();
    expect(screen.queryByText("理论误码率 BER")).toBeNull();
    expect(screen.getByText("0.02")).toBeTruthy();
    expect(screen.getByText(/错误比特 2 \/ 已比较比特 100/)).toBeTruthy();
    expect(screen.getByText(/1\/1 次已处理目标接收有质量记录/)).toBeTruthy();
    const progress = screen.getByRole("region", { name: "当前发送的接收进度" });
    expect(within(progress).getByText("0.5 s")).toBeTruthy();
    expect(within(progress).getByText("数据已交付")).toBeTruthy();
    fireEvent.click(screen.getByRole("button", { name: "接收完成" }));
    expect(screen.queryByRole("region", { name: "当前发送的接收进度" })).toBeNull();
    expect(screen.getByText("0.02")).toBeTruthy();
  });

  it("keeps experiment controls and observation tools in the sidebars, leaving the canvas and timeline as main regions", async () => {
    await prepare("Completed", { [`/runs/${run.run_id}/actions`]: { body: {
      run_id: run.run_id, edit: { available: true, experiment_draft_id: "original-draft" },
      rerun: { available: true, reason: null }, timing: null,
    } } });
    const left = screen.getByRole("complementary", { name: "实验与节点" });
    const right = screen.getByRole("complementary", { name: "通信观察与统计" });
    expect(within(left).getByRole("heading", { name: experiment.name })).toBeTruthy();
    expect(await within(left).findByRole("button", { name: "按原配置重新运行" })).toBeTruthy();
    expect(within(right).getByLabelText("通信观测层")).toBeTruthy();
    expect(within(right).getByRole("button", { name: "查看结果" })).toBeTruthy();
    expect(within(right).getByRole("region", { name: "当前展示进度统计" })).toBeTruthy();
    const root = document.querySelector(".runtime-workspace")!;
    expect([...root.children].map(child => child.className)).toEqual(["r4-studio-layout", "studio-history"]);
    fireEvent.click(screen.getByRole("button", { name: `节点 ${scenario.nodes[0].node_id}` }));
    expect(within(right).getByRole("region", { name: "节点检查器" })).toBeTruthy();
    expect(within(right).getByRole("heading", { name: "实时事件堆栈" })).toBeTruthy();
    fireEvent.click(within(right).getByRole("button", { name: "返回事件" }));
    expect(within(right).queryByRole("region", { name: "节点检查器" })).toBeNull();
  });

  it("starts with no displayed events even after the backend completed, then reveals only recorded events", async () => {
    const fetch = await prepare();
    expect(visibleCount()).toBe("0");
    expect(eventDots()).toHaveLength(0);
    expect(screen.getByText("Completed")).toBeTruthy();
    expect(screen.getByText(/本次真实运行采用逐步展示/)).toBeTruthy();
    vi.useFakeTimers();
    act(() => {
      ObservedStream.current!.emit("1", "Transmission", "0", tx);
      ObservedStream.current!.emit("2", "SignalArrival", "500000000", arrival);
      ObservedStream.current!.emit("3", "Reception", "1000000000", reception);
      ObservedStream.current!.emit("4", "CycleCommit", "2000000000", commit);
      ObservedStream.current!.end("4");
    });
    expect(visibleCount()).toBe("0"); expect(eventDots()).toHaveLength(0);
    await act(async () => { await vi.advanceTimersByTimeAsync(100); });
    expect(visibleCount()).toBe("1"); expect(eventDots()).toHaveLength(1);
    expect(document.querySelector(".run-topology")?.classList.contains("phase-transmission")).toBe(true);
    expect(screen.getByText("Completed")).toBeTruthy();
    fireEvent.click(screen.getByRole("button", { name: "暂停" }));
    await act(async () => { await vi.advanceTimersByTimeAsync(5000); });
    expect(visibleCount()).toBe("1");
    fireEvent.click(screen.getByRole("button", { name: "下一事件" }));
    expect(visibleCount()).toBe("2"); expect(eventDots()).toHaveLength(2);
    expect(document.querySelector(".run-topology")?.classList.contains("phase-signalarrival")).toBe(true);
    fireEvent.click(screen.getByRole("button", { name: "完整回放" }));
    expect(eventDots()).toHaveLength(4);
    expect(document.querySelector(".studio-product")?.getAttribute("data-presentation")).toBe("replay");
    expect(screen.getByText("Completed")).toBeTruthy();
    expect(fetch.mock.calls.every(([, init]) => (init?.method ?? "GET") === "GET")).toBe(true);
  });

  it("waits through a live stream gap and resumes when another real event arrives", async () => {
    const fetch = await prepare("Running"); vi.useFakeTimers();
    act(() => ObservedStream.current!.emit("1", "Transmission", "0", tx));
    await act(async () => { await vi.advanceTimersByTimeAsync(100); });
    expect(visibleCount()).toBe("1");
    await act(async () => { await vi.advanceTimersByTimeAsync(5000); });
    expect(visibleCount()).toBe("1"); expect(eventDots()).toHaveLength(1);
    act(() => ObservedStream.current!.emit("2", "SignalArrival", "500000000", arrival));
    await act(async () => { await vi.advanceTimersByTimeAsync(499); });
    expect(visibleCount()).toBe("1");
    await act(async () => { await vi.advanceTimersByTimeAsync(1); });
    expect(visibleCount()).toBe("2");
    act(() => ObservedStream.current!.end("2"));
    expect(ObservedStream.current!.closed).toBe(true);
    expect(screen.getByRole("button", { name: "播放" })).toBeTruthy();
    // Reading the end of the log cannot manufacture Completed lifecycle.
    expect(screen.getByText("Running")).toBeTruthy();
    expect(screen.queryByText("Completed")).toBeNull();
    expect(fetch.mock.calls.every(([, init]) => (init?.method ?? "GET") === "GET")).toBe(true);
  });

  it("uses one timeline plot domain for the slider, ticks and every event lane", async () => {
    await prepare(); vi.useFakeTimers();
    act(() => {
      ObservedStream.current!.emit("1", "Transmission", "0", tx);
      ObservedStream.current!.emit("2", "SignalArrival", "500000000", arrival);
      ObservedStream.current!.emit("3", "Reception", "1000000000", reception);
      ObservedStream.current!.emit("4", "CycleCommit", "2000000000", commit);
    });
    fireEvent.click(screen.getByRole("button", { name: "完整回放" }));
    const plots = document.querySelectorAll(".timeline-coordinate-grid .timeline-row > .timeline-plot");
    expect(plots).toHaveLength(8);
    expect(screen.getByLabelText("按仿真时间定位").parentElement).toBe(plots[0]);
    expect(document.querySelector(".timeline-axis")).toBe(plots[1]);
    expect(document.querySelector(".timeline-coordinate-grid")?.getAttribute("data-time-domain")).toBe("0:2000000000");
    expect((document.querySelector(".lane-signalarrival button") as HTMLElement).style.left).toBe("25%");
    expect((document.querySelector(".lane-reception button") as HTMLElement).style.left).toBe("50%");
    expect((document.querySelector(".timeline-axis .tick-1") as HTMLElement).style.left).toBe("50%");
    fireEvent.change(screen.getByLabelText("按仿真时间定位"), { target: { value: "50" } });
    expect((screen.getByLabelText("按仿真时间定位") as HTMLInputElement).value).toBe("50");
    expect([...document.querySelectorAll(".timeline-playhead")].every(element => (element as HTMLElement).style.left === "50%")).toBe(true);
  });

  it("shows a true Failed status immediately instead of waiting for presentation or hiding it as a stream failure", async () => {
    await prepare("Failed");
    expect(visibleCount()).toBe("0"); expect(eventDots()).toHaveLength(0);
    expect(screen.getByRole("alert").textContent).toContain("TDMA slot cannot contain");
    act(() => ObservedStream.current!.onerror?.(new Event("error")));
    expect(screen.getByRole("alert").textContent).toContain("TDMA slot cannot contain");
    expect(screen.queryByText(/事件连接暂时中断/)).toBeNull();
  });

  it("retains the actual displayed journal boundary when switching physical and protocol filters", async () => {
    await prepare(); vi.useFakeTimers();
    act(() => {
      ObservedStream.current!.emit("1", "Transmission", "0", tx);
      ObservedStream.current!.emit("2", "SignalArrival", "400000000", { ...arrival, receiver_node_id: "3" });
      ObservedStream.current!.emit("3", "SignalArrival", "500000000", arrival);
      ObservedStream.current!.emit("4", "Reception", "1000000000", reception);
    });
    fireEvent.change(screen.getByLabelText("通信观测层"), { target: { value: "physical" } });
    expect(visibleCount()).toBe("0"); expect(eventDots()).toHaveLength(0);
    await act(async () => { await vi.advanceTimersByTimeAsync(100); });
    fireEvent.click(screen.getByRole("button", { name: "暂停" }));
    fireEvent.click(screen.getByRole("button", { name: "下一事件" }));
    expect(visibleCount()).toBe("2"); expect(eventDots()).toHaveLength(2);
    fireEvent.change(screen.getByLabelText("通信观测层"), { target: { value: "protocol" } });
    expect(visibleCount()).toBe("2"); expect(eventDots()).toHaveLength(1);
    expect(document.querySelector(".run-topology")?.classList.contains("phase-idle")).toBe(true);
  });

  it("uses the frozen spatial domain and matching plan/profile X coordinates even after a displayed resize", async () => {
    const resizeCallbacks: Array<() => void> = [];
    vi.stubGlobal("ResizeObserver", class {
      constructor(callback: () => void) { resizeCallbacks.push(callback); }
      observe() {}
      disconnect() {}
    });
    const domain = { model: "ProfileExtrusion", x_min_m: 0, x_max_m: 1000, y_min_m: 0, y_max_m: 100,
      surface_clearance_m: 1, bottom_clearance_m: 1 };
    const frozenScene = { ...scenario, spatial_domain: domain,
      nodes: [{ ...scenario.nodes[0], initial_position: { x_meters: 250, y_meters: 50, z_meters: -25 } }] };
    await prepare("Completed", {
      [`/runs/${run.run_id}/definition-context`]: { body: { run_id: run.run_id, source: "AuthoredDefinition",
        definition_sha256: "d".repeat(64), scenario: frozenScene, experiment, execution: {}, limitations: [] } },
      [`/environments/${run.environment_asset_id}/authoring-inputs`]: { body: { available: true, name: "正式地形",
        payload: { bathymetry: [{ range_meters: 0, depth_meters: 100 }, { range_meters: 1000, depth_meters: 150 }] } } },
    });
    const plan = screen.getByRole("img", { name: "运行画布" });
    vi.spyOn(plan, "getBoundingClientRect").mockReturnValue({ width: 980, height: 240 } as DOMRect);
    act(() => resizeCallbacks.forEach(callback => callback()));
    expect(plan.getAttribute("viewBox")).toBe("0 0 980 240");
    const boundary = screen.getByTestId("runtime-spatial-domain");
    expect(Number(boundary.getAttribute("x"))).toBe(45);
    expect(Number(boundary.getAttribute("width"))).toBe(890);
    expect(Number(boundary.getAttribute("height"))).toBeCloseTo(89, 10);
    const nodeId = frozenScene.nodes[0].node_id;
    const planX = Number(screen.getByRole("button", { name: `节点 ${nodeId}` }).getAttribute("transform")!.match(/translate\(([^ ]+)/)![1]);
    const depthNode = document.querySelector(`.scene-node-depth [data-node-id="${nodeId}"]`)!;
    const depthX = Number(depthNode.getAttribute("transform")!.match(/translate\(([^,]+)/)![1]);
    expect(planX).toBeCloseTo(267.5, 10);
    expect(depthX).toBeCloseTo(planX, 10);
    expect(document.querySelector(".scene-node-depth svg")?.getAttribute("viewBox")).toBe("0 0 980 180");
    expect(screen.getByText(/固定试验域/)).toBeTruthy();
  });
});
