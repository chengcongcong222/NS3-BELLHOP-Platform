import { act, screen, waitFor } from "@testing-library/react";
import { describe, expect, it, vi } from "vitest";
import { mergePositionSamples, observedRunScene } from "../domain/runScene";
import type { ResultDto, RunDefinitionContextDto, ScenarioDto } from "../api/types";
import { experiment, result, run, scenario } from "./fixtures";
import { installApi, renderRoute } from "./render";

const capturedRun = { ...run, run_id: "authored-context-run", scenario_id: "user-scene-r4", scenario_version: "4", experiment_id: "user-experiment-r2", experiment_version: "2" };
const capturedScenario: ScenarioDto = {
  ...scenario, scenario_id: capturedRun.scenario_id, version: capturedRun.scenario_version,
  name: "用户保存的场景", fusion_center_node_id: "91",
  nodes: [71, 91].map(id => ({ ...scenario.nodes[0], node_id: String(id), initial_position: { x_meters: id, y_meters: 21, z_meters: -32 } })),
};
const context: RunDefinitionContextDto = {
  run_id: capturedRun.run_id, source: "AuthoredDefinition", definition_sha256: "d".repeat(64),
  scenario: capturedScenario,
  experiment: { ...experiment, experiment_id: capturedRun.experiment_id, version: capturedRun.experiment_version, name: "冻结的用户实验", scenario: { scenario_id: capturedScenario.scenario_id, version: capturedScenario.version }, phy: { ...experiment.phy, physical_layer: "Channel2Waveform" } },
  execution: { physical_layer: "Channel2Waveform" }, limitations: [],
};
const capturedResult: ResultDto = {
  ...result, run_id: capturedRun.run_id, scenario_id: capturedRun.scenario_id, scenario_version: capturedRun.scenario_version,
  experiment_id: capturedRun.experiment_id, experiment_version: capturedRun.experiment_version,
  nodes: capturedScenario.nodes.map(node => ({ ...node.initial_position, node_id: node.node_id, is_fusion_center: node.node_id === "91" })),
  visualization: { positions_complete: true, generated_feature_count: "0", waveform: [], fusion_sources: [], positions: [{ node_id: "71", at_ns: "0", x_meters: 71, y_meters: 21, z_meters: -32 }, { node_id: "91", at_ns: "0", x_meters: 91, y_meters: 21, z_meters: -32 }] },
};
const paths = {
  run: `/runs/${capturedRun.run_id}`,
  context: `/runs/${capturedRun.run_id}/definition-context`,
  result: `/runs/${capturedRun.run_id}/results`,
};

class RecordedEventSource {
  static current: RecordedEventSource | null = null;
  listener: ((event: MessageEvent<string>) => void) | null = null;
  onerror: ((event: Event) => void) | null = null;
  constructor(_url: string) { RecordedEventSource.current = this; }
  addEventListener(_type: string, listener: (event: MessageEvent<string>) => void) { if (_type === "run-event") this.listener = listener; }
  close() {}
  emitTransmission() { this.listener?.({ lastEventId: "1", data: JSON.stringify({ run_id: capturedRun.run_id, sequence: "1", trace: { kind: "Transmission", occurred_at_ns: "0", payload: { transmission_id: "7", packet_id: "4", sender_node_id: "71", target: { type: "Unicast", node_id: "91" }, started_at_ns: "0", ended_at_ns: "1000000000" } } }) } as MessageEvent<string>); }
}

describe("Run-scoped frozen definition and safe historical degradation", () => {
  it("merges a partial live position prefix without discarding the complete saved positions", () => {
    const recorded = capturedResult.visualization!.positions;
    const updated = { ...recorded[0], x_meters: 72 };
    const merged = mergePositionSamples(recorded, [updated]);
    expect(merged).toHaveLength(2);
    expect(merged.find(point => point.node_id === "71")?.x_meters).toBe(72);
    expect(merged.find(point => point.node_id === "91")?.x_meters).toBe(91);
    expect(recorded[0].x_meters).toBe(71);
  });
  it("renders authored context without consulting the static Scenario or Experiment catalogs", async () => {
    const fetchMock = installApi({
      [paths.run]: { body: capturedRun }, [paths.context]: { body: context }, [paths.result]: { body: capturedResult },
      [`/scenarios/${capturedRun.scenario_id}/versions/${capturedRun.scenario_version}`]: { status: 404, body: { error: { code: "ScenarioNotFound", message: "Scenario was not found." } } },
      [`/experiments/${capturedRun.experiment_id}/versions/${capturedRun.experiment_version}`]: { status: 404, body: { error: { code: "ExperimentNotFound", message: "Experiment was not found." } } },
    });
    renderRoute(paths.run);
    expect(await screen.findByRole("img", { name: "运行画布" })).toBeTruthy();
    expect(screen.getByRole("heading", { name: "冻结的用户实验" })).toBeTruthy();
    expect(screen.getByRole("button", { name: "节点 71" })).toBeTruthy();
    expect(screen.getByRole("button", { name: "节点 91" })).toBeTruthy();
    expect(screen.queryByText("Scenario was not found.")).toBeNull();
    expect(fetchMock.mock.calls.some(call => String(call[0]).startsWith("/scenarios/") || String(call[0]).startsWith("/experiments/"))).toBe(false);
    expect(fetchMock.mock.calls.every(call => (call[1]?.method ?? "GET") === "GET")).toBe(true);
  });

  it("fails explicitly on a rejected definition even when actual positions are available", async () => {
    installApi({ [paths.run]: { body: capturedRun }, [paths.result]: { body: capturedResult }, [paths.context]: { status: 409, body: { error: { code: "DefinitionIntegrityFailure", message: "本次冻结配置的校验失败。" } } } });
    renderRoute(paths.run);
    expect((await screen.findByRole("alert")).textContent).toContain("本次冻结配置的校验失败。");
    expect(screen.getByText("DefinitionIntegrityFailure")).toBeTruthy();
    expect(screen.queryByRole("img", { name: "运行画布" })).toBeNull();
    expect(screen.queryByText(/旧服务的历史回放/)).toBeNull();
  });

  it("supports an older absent endpoint using only actual recorded node identities and positions", async () => {
    RecordedEventSource.current = null;
    vi.stubGlobal("EventSource", RecordedEventSource);
    const fetchMock = installApi({ [paths.run]: { body: capturedRun }, [paths.result]: { body: capturedResult }, [paths.context]: { status: 404, body: { detail: "Not Found" } } });
    renderRoute(paths.run);
    expect(await screen.findByRole("img", { name: "运行画布" })).toBeTruthy();
    expect(screen.getByText(/旧服务的历史回放/)).toBeTruthy();
    await waitFor(() => expect(RecordedEventSource.current?.listener).toBeTruthy());
    act(() => RecordedEventSource.current?.emitTransmission());
    expect(screen.getByRole("button", { name: "节点 71" })).toBeTruthy();
    expect(screen.getByRole("button", { name: "节点 91" })).toBeTruthy();
    expect(screen.queryByRole("button", { name: "节点 0" })).toBeNull();
    expect(document.body.textContent).not.toContain("Acceptance4Node");
    expect(document.body.textContent).not.toContain("ConstantVelocity");
    expect(fetchMock.mock.calls.some(call => String(call[0]).startsWith("/scenarios/") || String(call[0]).startsWith("/experiments/"))).toBe(false);
  });

  it("does not invent a scene from final positions when history has no timestamped observations", async () => {
    installApi({ [paths.run]: { body: capturedRun }, [paths.result]: { body: { ...capturedResult, visualization: null } }, [paths.context]: { status: 404, body: { detail: "Not Found" } } });
    renderRoute(paths.run);
    expect(await screen.findByText(/等待真实位置记录后可查看回放/)).toBeTruthy();
    expect(screen.queryByRole("img", { name: "运行画布" })).toBeNull();
    expect(screen.getByRole("link", { name: "查看已保存结果" })).toBeTruthy();
    expect(observedRunScene([], capturedResult)).toBeNull();
  });

  it("does not misclassify a missing saved definition as an absent old endpoint", async () => {
    installApi({ [paths.run]: { body: capturedRun }, [paths.result]: { body: capturedResult }, [paths.context]: { status: 404, body: { error: { code: "RunDefinitionNotFound", message: "冻结配置不存在。" } } } });
    renderRoute(paths.run);
    expect((await screen.findByRole("alert")).textContent).toContain("冻结配置不存在。");
    expect(screen.queryByRole("img", { name: "运行画布" })).toBeNull();
  });

  it("does not backfill a later observed position into an earlier replay time", async () => {
    RecordedEventSource.current = null;
    vi.stubGlobal("EventSource", RecordedEventSource);
    const later = { ...capturedResult, visualization: { ...capturedResult.visualization!, positions: capturedResult.visualization!.positions.map(point => point.node_id === "71" ? { ...point, at_ns: "10000000000" } : point) } };
    installApi({ [paths.run]: { body: capturedRun }, [paths.result]: { body: later }, [paths.context]: { status: 404, body: { detail: "Not Found" } } });
    renderRoute(paths.run);
    await screen.findByRole("img", { name: "运行画布" });
    await waitFor(() => expect(RecordedEventSource.current?.listener).toBeTruthy());
    act(() => RecordedEventSource.current?.emitTransmission());
    expect(screen.queryByRole("button", { name: "节点 71" })).toBeNull();
    expect(screen.getByRole("button", { name: "节点 91" })).toBeTruthy();
    expect(screen.queryByTestId("communication-arrow")).toBeNull();
  });
});
