import { screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { describe, expect, it } from "vitest";
import { acceptance4Scenario } from "./acceptance4Golden";
import { environment, experiment, resultSummary, run, runSummary, scenario } from "./fixtures";
import { installApi, renderRoute } from "./render";

describe("real resource routes", () => {
  it("renders the latest run without attaching a different run's latest result", async () => {
    const latestRun = { ...runSummary, catalog_sequence: "2", run_id: "a-latest-run", lifecycle: "Failed", result_available: false };
    const latestResult = { ...resultSummary, catalog_sequence: "3", run_id: "a-latest-result" };
    installApi({
      "/environments": { body: [environment] },
      "/scenarios": { body: [scenario] },
      "/experiments": { body: [experiment] },
      "/authoring/drafts": { body: [] },
      "/runs": { body: [runSummary, latestRun] },
      "/results": { body: [resultSummary, latestResult] },
    });
    renderRoute("/");
    expect(await screen.findByTitle("a-latest-run")).toBeTruthy();
    expect(screen.queryByText("a-latest-run")).toBeNull();
    expect(screen.getByText("运行失败")).toBeTruthy();
    expect(screen.getByRole("link", { name: "打开运行工作台" }).getAttribute("href")).toBe("/runs/a-latest-run");
    expect(screen.queryByRole("link", { name: "查看最近结果" })).toBeNull();
    expect(screen.getByText("水声网络数字孪生仿真工作台")).toBeTruthy();
  });

  it("links the latest run only to its own result", async () => {
    const matchingRun = { ...runSummary, run_id: "same-run", catalog_sequence: "2" };
    installApi({ "/environments": { body: [environment] }, "/scenarios": { body: [scenario] },
      "/experiments": { body: [experiment] }, "/authoring/drafts": { body: [] },
      "/runs": { body: [runSummary, matchingRun] },
      "/results": { body: [{ ...resultSummary, run_id: "same-run" }, { ...resultSummary, run_id: "unrelated-latest-result", catalog_sequence: "3" }] },
    });
    renderRoute("/");
    expect((await screen.findByRole("link", { name: "查看最近结果" })).getAttribute("href")).toBe("/results/same-run");
  });

  it("renders Environment catalog and detail", async () => {
    const referenceEnvironment = {
      ...environment,
      environment_asset_id: "reference-shallow-water-v1",
      cell_count: "650",
      signal_cell_count: "625",
      no_arrival_cell_count: "25",
      payload_bytes: "231056",
      checksum: { algorithm: "FNV1A64", value: "fb64e543f9042c52" },
      provenance: {
        producer: "BellhopRawImport",
        created_by_build_version: "P0-S5-02",
        source_description: "Reference/proxy modeled environment; Bellhop-derived, not field-measured",
        raw_source_logical_name: "reference_shallow_water_v1.arr",
        normalization_policy_version: "bellhop-raw-arrival-normalizer-v1",
      },
      axes: {
        ...environment.axes,
        horizontal_range: { unit: "m", count: "26", minimum: 0, maximum: 2500 },
        source_depth: { unit: "m", count: "5", minimum: 8, maximum: 75 },
        receiver_depth: { unit: "m", count: "5", minimum: 8, maximum: 75 },
      },
    } as typeof environment;
    installApi({
      "/environments": { body: [referenceEnvironment] },
      "/authoring/drafts?kind=environment": { body: [] },
      [`/environments/${referenceEnvironment.environment_asset_id}/authoring-inputs`]: { body: {
        available: true, name: "南海浅水参考环境", limitations: [], payload: {
          sound_speed_profile: [{ depth_meters: 0, speed_meters_per_second: 1500 }, { depth_meters: 175, speed_meters_per_second: 1490 }],
          bathymetry: [{ range_meters: 0, depth_meters: 100 }, { range_meters: 1000, depth_meters: 110 }],
          acquisition: { latitudeDeg: 18, longitudeDeg: 110, month: 4 },
        },
      } },
      [`/environments/${referenceEnvironment.environment_asset_id}`]: { body: referenceEnvironment },
    });
    const catalog = renderRoute("/environments");
    expect(await screen.findByText("南海浅水参考环境")).toBeTruthy();
    expect(screen.queryByText("Producer")).toBeNull();
    catalog.unmount();
    renderRoute(`/environments/${referenceEnvironment.environment_asset_id}`);
    expect(await screen.findByRole("img", { name: "草稿声速剖面" })).toBeTruthy();
    expect(screen.getByText("已发布环境的输入 · 修改后另存新版本")).toBeTruthy();
    expect((screen.getByLabelText("环境名称") as HTMLInputElement).value).toBe("南海浅水参考环境");
    expect(screen.queryByText("正式 AcousticFieldAsset 覆盖范围")).toBeNull();
    expect(document.body.textContent).not.toContain("/home/");
  });

  it("replaces the Scenario catalog with unified editing while preserving versioned read-only links", async () => {
    installApi({
      "/scenarios": { body: [scenario] },
      "/environments": { body: [environment] },
      "/trajectories": { body: [] },
      "/authoring/drafts?kind=scenario": { body: [] },
      "/authoring/drafts?kind=experiment": { body: [] },
      [`/scenarios/${scenario.scenario_id}/versions/${scenario.version}`]: { body: scenario },
    });
    const catalog = renderRoute("/scenarios");
    expect(await screen.findByRole("heading", { name: "实验工作台" })).toBeTruthy();
    expect(screen.getByRole("img", { name: "可编辑场景画布" })).toBeTruthy();
    expect(screen.queryByRole("link", { name: "场景设计" })).toBeNull();
    catalog.unmount();
    renderRoute(`/scenarios/${scenario.scenario_id}/versions/${scenario.version}`);
    expect(await screen.findByText(`Node ${scenario.fusion_center_node_id}`)).toBeTruthy();
    expect(screen.getByText(/TX.*RX/)).toBeTruthy();
  });

  it("renders only Scenario DTO initial geometry and movement classification", async () => {
    installApi({
      [`/scenarios/${acceptance4Scenario.scenario_id}/versions/${acceptance4Scenario.version}`]: { body: acceptance4Scenario },
    });
    renderRoute(`/scenarios/${acceptance4Scenario.scenario_id}/versions/${acceptance4Scenario.version}`);
    expect(await screen.findByRole("img", { name: "Scenario initial topology" })).toBeTruthy();
    expect(screen.getByText("仅显示 Scenario DTO 的初始 x/y 几何；不表示实时轨迹。深度见节点表（m）。")).toBeTruthy();
    expect(screen.getAllByText("Moving")).toHaveLength(3);
    expect(screen.getByText("Fixed")).toBeTruthy();
  });

  it("renders Experiment detail and launches only by identity/version", async () => {
    const fetchMock = installApi({
      "/experiments": { body: [experiment] },
      [`/experiments/${experiment.experiment_id}/versions/${experiment.version}`]: { body: experiment },
      "POST /runs": { body: { ...run, run_id: "launched-run", lifecycle: "Created" } },
      "/runs/launched-run": { body: { ...run, run_id: "launched-run", lifecycle: "Running", event_stream_complete: false } },
    });
    const catalog = renderRoute("/experiments");
    expect(await screen.findByText(experiment.name)).toBeTruthy();
    catalog.unmount();
    renderRoute(`/experiments/${experiment.experiment_id}/versions/${experiment.version}`);
    await userEvent.click(await screen.findByRole("button", { name: "开始仿真" }));
    expect(await screen.findByText("场景初始几何 · 当前通信事件")).toBeTruthy();
    expect(document.body.textContent).toContain("launched-run");
    const post = fetchMock.mock.calls.find((call) => call[1]?.method === "POST");
    expect(JSON.parse(String(post?.[1]?.body))).toEqual({
      experiment_id: experiment.experiment_id,
      experiment_version: experiment.version,
    });
  });

  it("labels Extended6Node as an extension rather than third-party acceptance", async () => {
    const extended = {
      ...experiment,
      experiment_id: "extended6-experiment",
      name: "Extended 6-Node Experiment",
      fusion: { ...experiment.fusion, acceptance_profile: "Extended6Node" },
    };
    installApi({
      [`/experiments/${extended.experiment_id}/versions/${extended.version}`]: { body: extended },
    });
    renderRoute(`/experiments/${extended.experiment_id}/versions/${extended.version}`);
    expect(await screen.findByText("Extended6Node · 扩展示例（非第三方验收）")).toBeTruthy();
  });
});
