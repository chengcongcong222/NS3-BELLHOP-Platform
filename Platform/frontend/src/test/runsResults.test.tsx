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
    expect(await screen.findByText("场景初始几何 · 当前通信事件")).toBeTruthy();
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
    expect(document.body.textContent).toContain("周期 1/");
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
    expect(screen.getByRole("button", { name: /信道查询 · N2 · 无有效路径/ })).toBeTruthy();
    expect(screen.getByText(/不播放虚构到达动画/)).toBeTruthy();
    expect(screen.queryByTestId("communication-arrow")).toBeNull();
    expect(document.querySelector(".link-inspector")?.textContent).toContain("N2");
    expect(document.body.textContent).toContain("0.00000001 s (10 ns)");
    expect(screen.getByText("原始序列 2")).toBeTruthy();
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
    const slider = screen.getByLabelText("按仿真时间定位") as HTMLInputElement;
    expect(Number(slider.value)).toBeGreaterThanOrEqual(0);
    await userEvent.click(screen.getByRole("button", { name: /节点 0 开始发送/ }));
    expect(Number(slider.value)).toBeGreaterThanOrEqual(0);
    expect(screen.getByText("Completed")).toBeTruthy();
    expect(document.querySelector(".run-topology")?.classList.contains("phase-transmission")).toBe(true);
  });


  it("preserves exact duration and removes verdicts from result catalog and detail", async () => {
    const large = "900719925474099312345";
    installApi({ "/results": { body: [{ ...resultSummary, simulation_duration_ns: large }] },
      [`/runs/${result.run_id}/results`]: { body: { ...result, projection: { ...result.projection, simulation_duration_ns: large } } } });
    const catalog = renderRoute("/results");
    expect(await screen.findByText(`900719925474.099312345 s (${large} ns)`)).toBeTruthy();
    expect(screen.queryByText("Pass")).toBeNull(); catalog.unmount();
    renderRoute(`/results/${result.run_id}`);
    expect(await screen.findByText("运行概览", { selector: "h2" })).toBeTruthy();
    expect(document.body.textContent).toContain(large);
    expect(screen.queryByText("Pass")).toBeNull();
    expect(screen.queryByText("Fail")).toBeNull();
  });

  it("displays missing physical evidence without interpreting acceptance status", async () => {
    installApi({ [`/runs/${result.run_id}/results`]: { body: { ...result,
      acceptance_report: { ...result.acceptance_report!, overall: "Fail", maximum_ber: null } } } });
    renderRoute(`/results/${result.run_id}`);
    expect(await screen.findByText(/本次运行没有正式波形接收记录/)).toBeTruthy();
    expect(screen.queryByText("Fail")).toBeNull();
    expect(screen.getByText(/不等于 Channel2 实际误码/)).toBeTruthy();
  });

  it("shows quantitative values, not backend verdicts even when contradictory", async () => {
    installApi({ [`/runs/${result.run_id}/results`]: { body: { ...result,
      acceptance_report: { ...result.acceptance_report!, overall: "Pass", maximum_ber: 0.9 } } } });
    renderRoute(`/results/${result.run_id}`);
    expect(await screen.findByText("0.9")).toBeTruthy();
    expect(screen.queryByText("Pass")).toBeNull();
    expect(document.body.textContent).not.toContain("满足要求");
  });

  it("organizes the golden result into five scientific sections", async () => {
    installApi({ [`/runs/${acceptance4Result.run_id}/results`]: { body: acceptance4Result } });
    renderRoute(`/results/${acceptance4Result.run_id}`);
    for (const name of ["运行概览", "网络性能", "水声链路", "波形通信", "探测与融合"])
      expect(await screen.findByRole("heading", { name })).toBeTruthy();
    expect(screen.getByText(/不是硬件实测/)).toBeTruthy();
    expect(screen.getByText(/逐条方位值.*不从发送端报告补造/)).toBeTruthy();
    expect(document.querySelector(".metric-card")).toBeNull();
  });

  it("does not add acceptance requirements to a result without a report", async () => {
    installApi({ [`/runs/${result.run_id}/results`]: { body: { ...result, acceptance_report: null } } });
    renderRoute(`/results/${result.run_id}`);
    await screen.findByRole("heading", { name: "运行概览" });
    expect(screen.queryByText("参考要求与本次量化值")).toBeNull();
  });

  it("supports the published Experiment to Run to scientific Result path", async () => {
    installApi({ [`/experiments/${experiment.experiment_id}/versions/${experiment.version}`]: { body: experiment },
      "POST /runs": { body: acceptance4Run }, [`/runs/${acceptance4Run.run_id}`]: { body: acceptance4Run },
      [`/runs/${acceptance4Run.run_id}/results`]: { body: acceptance4Result } });
    renderRoute(`/experiments/${experiment.experiment_id}/versions/${experiment.version}`);
    await userEvent.click(await screen.findByRole("button", { name: "开始仿真" }));
    expect(await screen.findByText("场景初始几何 · 当前通信事件")).toBeTruthy();
    await userEvent.click(screen.getByRole("link", { name: "查看结果 →" }));
    expect(await screen.findByRole("heading", { name: "探测与融合" })).toBeTruthy();
    expect(screen.queryByText("Pass")).toBeNull();
  });
});
