import { act, fireEvent, screen, waitFor, within } from "@testing-library/react";
import { afterEach, describe, expect, it, vi } from "vitest";
import { experiment, run, scenario } from "./fixtures";
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
  return fetch;
}

afterEach(() => vi.useRealTimers());

describe("new run progressive observation", () => {
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
    expect(visibleCount()).toBe("1"); expect(eventDots()).toHaveLength(1);
    expect(document.querySelector(".run-topology")?.classList.contains("phase-transmission")).toBe(true);
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
