import { act, screen, waitFor } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { describe, expect, it, vi } from "vitest";
import { experiment, result, resultSummary, run, runSummary } from "./fixtures";
import { acceptance4Result, acceptance4Run } from "./acceptance4Golden";
import { installApi, renderRoute } from "./render";

describe("authoritative Run and Result views", () => {
  it("uses GET /runs as catalog authority and renders lifecycle", async () => {
    installApi({ "/runs": { body: [runSummary] } });
    renderRoute("/runs");
    expect(await screen.findByText(runSummary.run_id)).toBeTruthy();
    expect(screen.getByText("Completed")).toBeTruthy();
    expect(screen.getByText("查看结果")).toBeTruthy();
  });

  it("renders a basic Run monitor with captured identities", async () => {
    installApi({ [`/runs/${run.run_id}`]: { body: run } });
    renderRoute(`/runs/${run.run_id}`);
    expect(await screen.findByText("网络运行画布")).toBeTruthy();
    expect(document.body.textContent).toContain(run.run_id);
    expect(screen.getAllByText(experiment.name).length).toBeGreaterThanOrEqual(1);
    expect(screen.getByText(new RegExp(run.environment_asset_id))).toBeTruthy();
  });

  it("keeps Run lifecycle authoritative when SSE disagrees or fails", async () => {
    class ControlledEventSource {
      static current: ControlledEventSource | null = null;
      listener: ((event: MessageEvent<string>) => void) | null = null;
      onerror: ((event: Event) => void) | null = null;
      constructor(_url: string) { ControlledEventSource.current = this; }
      addEventListener(_type: string, listener: (event: MessageEvent<string>) => void) { this.listener = listener; }
      close() {}
    }
    vi.stubGlobal("EventSource", ControlledEventSource);
    const running = { ...run, lifecycle: "Running", event_stream_complete: false };
    installApi({ [`/runs/${run.run_id}`]: { body: running } });
    renderRoute(`/runs/${run.run_id}`);
    expect(await screen.findByText("Running")).toBeTruthy();
    await waitFor(() => expect(ControlledEventSource.current).not.toBeNull());
    act(() => {
      ControlledEventSource.current?.listener?.({
        lastEventId: "1",
        data: JSON.stringify({
          run_id: run.run_id,
          sequence: "1",
          lifecycle: "Completed",
          trace: {
            occurred_at_ns: "1",
            kind: "CycleCommit",
            payload: {
              cycle_id: "0",
              base_snapshot_version: "0",
              committed_snapshot_version: "1",
              committed_at_ns: "1",
            },
          },
        }),
      } as MessageEvent<string>);
      ControlledEventSource.current?.onerror?.(new Event("error"));
    });
    expect(screen.getByText("Running")).toBeTruthy();
    expect(screen.queryByText("Completed")).toBeNull();
    expect(document.querySelector(".metrics-hierarchy")?.textContent).toContain("1完成周期1已记录事件");
    expect(screen.getByText("后端连接不可用")).toBeTruthy();
  });

  it("renders Signal and NoArrival from formal ChannelOutcome events", async () => {
    class ControlledEventSource {
      static current: ControlledEventSource | null = null;
      listener: ((event: MessageEvent<string>) => void) | null = null;
      onerror: ((event: Event) => void) | null = null;
      constructor(_url: string) { ControlledEventSource.current = this; }
      addEventListener(_type: string, listener: (event: MessageEvent<string>) => void) { this.listener = listener; }
      close() {}
      emit(sequence: string, trace: object) {
        this.listener?.({ lastEventId: sequence, data: JSON.stringify({ run_id: run.run_id, sequence, trace }) } as MessageEvent<string>);
      }
    }
    vi.stubGlobal("EventSource", ControlledEventSource);
    installApi({ [`/runs/${run.run_id}`]: { body: { ...run, lifecycle: "Running", event_stream_complete: false } } });
    renderRoute(`/runs/${run.run_id}`);
    await screen.findByText("Running");
    await waitFor(() => expect(ControlledEventSource.current).not.toBeNull());
    act(() => {
      ControlledEventSource.current?.emit("1", {
        occurred_at_ns: "20",
        kind: "Transmission",
        payload: { transmission_id: "7", packet_id: "3", sender_node_id: "0", target: { type: "Broadcast" }, started_at_ns: "20", ended_at_ns: "30" },
      });
      ControlledEventSource.current?.emit("2", {
        occurred_at_ns: "10",
        kind: "ChannelOutcome",
        payload: { transmission_id: "7", receiver_node_id: "2", outcome: { type: "NoArrival" } },
      });
    });
    expect(screen.getByText("节点 2 未获得有效声学到达")).toBeTruthy();
    expect(screen.getAllByText(/无有效到达/).length).toBeGreaterThan(0);
    expect(document.querySelector(".link-inspector")?.textContent).toContain("N0");
    expect(document.querySelector(".link-inspector")?.textContent).toContain("N2");
    expect(screen.getAllByText("0.00000001 s (10 ns)").length).toBeGreaterThanOrEqual(1);
    expect(screen.getByText("序列 2")).toBeTruthy();
  });

  it("replays completed formal events without changing Run lifecycle", async () => {
    class ControlledEventSource {
      static current: ControlledEventSource | null = null;
      listener: ((event: MessageEvent<string>) => void) | null = null;
      onerror: ((event: Event) => void) | null = null;
      constructor(_url: string) { ControlledEventSource.current = this; }
      addEventListener(_type: string, listener: (event: MessageEvent<string>) => void) { this.listener = listener; }
      close() {}
      emit(sequence: string, trace: object) { this.listener?.({ lastEventId: sequence, data: JSON.stringify({ run_id: run.run_id, sequence, trace }) } as MessageEvent<string>); }
    }
    vi.stubGlobal("EventSource", ControlledEventSource);
    installApi({ [`/runs/${run.run_id}`]: { body: run } });
    renderRoute(`/runs/${run.run_id}`);
    await screen.findByText("Completed");
    await waitFor(() => expect(ControlledEventSource.current).not.toBeNull());
    act(() => {
      ControlledEventSource.current?.emit("1", { occurred_at_ns: "20", kind: "Transmission", payload: { transmission_id: "7", packet_id: "3", sender_node_id: "0", target: { type: "Unicast", node_id: "2" }, started_at_ns: "20", ended_at_ns: "30" } });
      ControlledEventSource.current?.emit("2", { occurred_at_ns: "30", kind: "ChannelOutcome", payload: { transmission_id: "7", receiver_node_id: "2", outcome: { type: "NoArrival" } } });
    });
    const slider = screen.getByLabelText("事件回放位置") as HTMLInputElement;
    expect(slider.value).toBe("2");
    await userEvent.click(screen.getByRole("button", { name: /节点 0 开始发送/ }));
    expect(slider.value).toBe("1");
    expect(screen.getByText("Completed")).toBeTruthy();
    expect(document.querySelector(".run-topology")?.classList.contains("phase-transmission")).toBe(true);
  });

  it("renders Result catalog and PASS detail without losing large integers", async () => {
    const large = "900719925474099312345";
    installApi({
      "/results": { body: [{ ...resultSummary, simulation_duration_ns: large }] },
      [`/runs/${result.run_id}/results`]: { body: { ...result, projection: { ...result.projection, simulation_duration_ns: large } } },
      [`/experiments/${experiment.experiment_id}/versions/${experiment.version}`]: { body: experiment },
    });
    const catalog = renderRoute("/results");
    expect(await screen.findByText(`900719925474.099312345 s (${large} ns)`)).toBeTruthy();
    catalog.unmount();
    renderRoute(`/results/${result.run_id}`);
    expect((await screen.findAllByText("Pass")).length).toBeGreaterThan(0);
    expect(document.body.textContent).toContain(`900719925474.099312345 s (${large} ns)`);
    expect(await screen.findByText(/不是硬件实测/)).toBeTruthy();
  });

  it("renders acceptance Fail and BER NotEvaluated distinctly", async () => {
    const notEvaluated = {
      ...result,
      acceptance_report: {
        ...result.acceptance_report!,
        overall: "Fail",
        bit_error_rate: "NotEvaluated",
        maximum_ber: null,
        mean_ber: null,
        ber_reason: "No target Reception contained BER quality evidence.",
      },
    };
    installApi({
      [`/runs/${result.run_id}/results`]: { body: notEvaluated },
      [`/experiments/${experiment.experiment_id}/versions/${experiment.version}`]: { body: { ...experiment, phy: { ...experiment.phy, rx_quality_mode: "None" } } },
    });
    renderRoute(`/results/${result.run_id}`);
    expect((await screen.findAllByText("Fail")).length).toBeGreaterThan(0);
    expect(screen.getAllByText("NotEvaluated").length).toBeGreaterThan(0);
    expect(screen.getByText(/未配置 BER quality model/)).toBeTruthy();
    expect(screen.getByText("No target Reception contained BER quality evidence.")).toBeTruthy();
  });

  it("renders contradictory backend acceptance verdicts without recomputation", async () => {
    const contradictory = {
      ...result,
      projection: { ...result.projection, node_count: "1" },
      acceptance_report: {
        ...result.acceptance_report!,
        overall: "Pass",
        network_node_count: "Pass",
        communication_rate: "Pass",
        bit_error_rate: "Pass",
        feature_level_fusion: "Pass",
        bearing_point_count: "Pass",
        fusion_period: "Pass",
        maximum_ber: 0.9,
        mean_ber: 0.8,
        minimum_bearing_points: "1",
        maximum_fusion_period_ns: "999999999999",
      },
    };
    installApi({
      [`/runs/${result.run_id}/results`]: { body: contradictory },
      [`/experiments/${experiment.experiment_id}/versions/${experiment.version}`]: {
        body: {
          ...experiment,
          phy: { ...experiment.phy, bit_rate_bits_per_second: "1" },
        },
      },
    });
    renderRoute(`/results/${result.run_id}`);
    expect((await screen.findAllByText("1 bit/s")).length).toBeGreaterThan(0);
    expect(screen.getByText(/max 0.9（无量纲） · mean 0.8/)).toBeTruthy();
    expect(screen.getAllByText("999.999999999 s (999999999999 ns)").length).toBeGreaterThanOrEqual(1);
    expect(screen.getAllByText("Pass").length).toBeGreaterThan(0);
    expect(screen.queryByText("Fail")).toBeNull();
  });

  it("renders the fixed Acceptance4Node golden screen and resource traceability", async () => {
    installApi({
      [`/runs/${acceptance4Result.run_id}/results`]: { body: acceptance4Result },
      [`/experiments/${experiment.experiment_id}/versions/${experiment.version}`]: { body: experiment },
    });
    renderRoute(`/results/${acceptance4Result.run_id}`);
    expect(await screen.findByText(/Acceptance4Node · 第三方验收/)).toBeTruthy();
    expect(screen.getByText("3–4 nodes（third-party requirement）")).toBeTruthy();
    expect(screen.getAllByText("4 nodes").length).toBeGreaterThan(0);
    expect(screen.getAllByText("60 bit/s").length).toBeGreaterThan(0);
    expect(screen.getByText("5 points")).toBeTruthy();
    expect(screen.getAllByText("Pass").length).toBeGreaterThanOrEqual(7);
    expect(screen.getAllByText("120 s (120000000000 ns)").length).toBeGreaterThan(0);
    expect(screen.getByText(/仿真模型 BER.*不是硬件实测/)).toBeTruthy();
    expect(screen.getByText(/浮点数值表示下限/)).toBeTruthy();
    expect(await screen.findByText("Reference / modeled")).toBeTruthy();
    expect(screen.getByText("Bellhop-derived")).toBeTruthy();
    expect(screen.getByRole("link", { name: acceptance4Result.run_id }).getAttribute("href")).toBe(`/runs/${acceptance4Result.run_id}`);
    expect(screen.getByRole("link", { name: new RegExp(experiment.experiment_id) })).toBeTruthy();
    expect(document.body.textContent).not.toContain("NotDecoded");
  });

  it("keeps Extended6Node Result outside the Acceptance4Node baseline", async () => {
    const extendedExperiment = {
      ...experiment,
      fusion: { ...experiment.fusion, acceptance_profile: "Extended6Node" },
    };
    installApi({
      [`/runs/${result.run_id}/results`]: { body: { ...result, acceptance_report: null } },
      [`/experiments/${experiment.experiment_id}/versions/${experiment.version}`]: { body: extendedExperiment },
    });
    renderRoute(`/results/${result.run_id}`);
    expect(await screen.findByText("Extended6Node 仅为扩展示例，不混入 Acceptance4Node 第三方验收要求。")).toBeTruthy();
    expect(screen.queryByRole("columnheader", { name: "Requirement" })).toBeNull();
  });

  it("supports the complete Experiment to Run to Result to Acceptance demo path", async () => {
    installApi({
      [`/experiments/${experiment.experiment_id}/versions/${experiment.version}`]: { body: experiment },
      "POST /runs": { body: acceptance4Run },
      [`/runs/${acceptance4Run.run_id}`]: { body: acceptance4Run },
      [`/runs/${acceptance4Run.run_id}/results`]: { body: acceptance4Result },
    });
    renderRoute(`/experiments/${experiment.experiment_id}/versions/${experiment.version}`);
    await userEvent.click(await screen.findByRole("button", { name: "开始仿真" }));
    expect(await screen.findByText("网络运行画布")).toBeTruthy();
    expect(document.body.textContent).toContain(acceptance4Run.run_id);
    await userEvent.click(screen.getByRole("link", { name: "查看结果 →" }));
    expect(await screen.findByText(/Acceptance4Node · 第三方验收/)).toBeTruthy();
    expect(screen.getByText("Frontend does not recompute this verdict")).toBeTruthy();
  });
});
