import { act, screen, waitFor } from "@testing-library/react";
import { describe, expect, it, vi } from "vitest";
import { experiment, result, resultSummary, run, runSummary } from "./fixtures";
import { installApi, renderRoute } from "./render";

describe("authoritative Run and Result views", () => {
  it("uses GET /runs as catalog authority and renders lifecycle", async () => {
    installApi({ "/runs": { body: [runSummary] } });
    renderRoute("/runs");
    expect(await screen.findByText(runSummary.run_id)).toBeTruthy();
    expect(screen.getByText("Completed")).toBeTruthy();
    expect(screen.getByText("Available")).toBeTruthy();
  });

  it("renders a basic Run monitor with captured identities", async () => {
    installApi({ [`/runs/${run.run_id}`]: { body: run } });
    renderRoute(`/runs/${run.run_id}`);
    expect(await screen.findByText(`Run Monitor · ${run.run_id}`)).toBeTruthy();
    expect(screen.getByText(new RegExp(run.experiment_id))).toBeTruthy();
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
            payload: {},
          },
        }),
      } as MessageEvent<string>);
      ControlledEventSource.current?.onerror?.(new Event("error"));
    });
    expect(screen.getByText("Running")).toBeTruthy();
    expect(screen.queryByText("Completed")).toBeNull();
    expect(screen.getAllByText("1", { selector: ".metric-card strong" })).toHaveLength(2);
    expect(screen.getByText("后端连接不可用")).toBeTruthy();
  });

  it("renders Result catalog and PASS detail without losing large integers", async () => {
    const large = "900719925474099312345";
    installApi({
      "/results": { body: [{ ...resultSummary, simulation_duration_ns: large }] },
      [`/runs/${result.run_id}/results`]: { body: { ...result, projection: { ...result.projection, simulation_duration_ns: large } } },
      [`/experiments/${experiment.experiment_id}/versions/${experiment.version}`]: { body: experiment },
    });
    const catalog = renderRoute("/results");
    expect(await screen.findByText(`${large} ns`)).toBeTruthy();
    catalog.unmount();
    renderRoute(`/results/${result.run_id}`);
    expect((await screen.findAllByText("Pass")).length).toBeGreaterThan(0);
    expect(screen.getByText(`${large} ns`)).toBeTruthy();
    expect(await screen.findByText(/不是硬件测量/)).toBeTruthy();
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
      },
    };
    installApi({
      [`/runs/${result.run_id}/results`]: { body: notEvaluated },
      [`/experiments/${experiment.experiment_id}/versions/${experiment.version}`]: { body: { ...experiment, phy: { ...experiment.phy, rx_quality_mode: "None" } } },
    });
    renderRoute(`/results/${result.run_id}`);
    expect(await screen.findByText("Fail")).toBeTruthy();
    expect(screen.getAllByText("NotEvaluated").length).toBeGreaterThan(0);
    expect(screen.getByText(/未提供 BER evidence/)).toBeTruthy();
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
    expect(await screen.findByText("1 bit/s")).toBeTruthy();
    expect(screen.getByText("0.9 / 0.8")).toBeTruthy();
    expect(screen.getByText("999999999999 ns")).toBeTruthy();
    expect(screen.getAllByText("Pass").length).toBeGreaterThan(0);
    expect(screen.queryByText("Fail")).toBeNull();
  });
});
