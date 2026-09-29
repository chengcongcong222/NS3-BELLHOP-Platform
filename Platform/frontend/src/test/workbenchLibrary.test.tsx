import { screen, within } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { expect, it } from "vitest";
import type { AuthoringDraftDto, RunActionsDto, RunSummaryDto } from "../api/types";
import { environment, experiment, runSummary, scenario } from "./fixtures";
import { installApi, renderRoute } from "./render";

const catalog = { "/environments": { body: [environment] }, "/scenarios": { body: [scenario] },
  "/experiments": { body: [experiment] } };

it("keeps the home compact and finds any saved experiment without making a new one", async () => {
  const drafts: AuthoringDraftDto[] = Array.from({ length: 18 }, (_, index) => ({
    kind: "experiment", draft_id: `saved-experiment-${index}`, name: `水声实验 ${index}`,
    payload: { physical_layer: "Channel2Waveform", scenario_id: "shared-scene" },
    revision: 4, created_at: "2026-09-17T00:00:00Z", updated_at: "2026-09-17T01:00:00Z", validation_issues: [],
  }));
  const fetch = installApi({ ...catalog, "/runs": { body: [] }, "/results": { body: [] },
    "/authoring/drafts": { body: drafts } });
  const { container } = renderRoute("/");
  expect(await screen.findByRole("heading", { name: "水声网络数字孪生仿真工作台" })).toBeTruthy();
  const table = screen.getByRole("table");
  expect(within(table).getAllByRole("row")).toHaveLength(7);
  expect(container.querySelector(".hero-panel")).toBeNull();
  expect(screen.queryByText("平台态势")).toBeNull();
  await userEvent.type(screen.getByLabelText("查找我的实验"), "水声实验 17");
  expect(within(table).getAllByRole("row")).toHaveLength(2);
  expect(screen.getByRole("link", { name: "水声实验 17" }).getAttribute("href"))
    .toBe("/workspace/experiment?draft=saved-experiment-17");
  expect(screen.getByText("B · 波形通信")).toBeTruthy();
  expect(fetch.mock.calls.every(([, init]) => (init?.method ?? "GET") === "GET")).toBe(true);
});

it("renders case node coordinates and frozen experiment facts without an unrelated creation action", async () => {
  installApi(catalog);
  renderRoute("/cases/acceptance4-shallow-water");
  expect(await screen.findByRole("heading", { name: "场景与节点" })).toBeTruthy();
  expect(screen.getByRole("table").textContent).toContain(String(scenario.nodes[0].initial_position.x_meters));
  expect(screen.getByText(`${experiment.phy.bit_rate_bits_per_second} bit/s`)).toBeTruthy();
  expect(screen.getByText("声场距离").closest("div")?.textContent)
    .toContain(`${environment.axes.horizontal_range.minimum}–${environment.axes.horizontal_range.maximum} m`);
  expect(screen.queryByRole("link", { name: "新建自定义实验" })).toBeNull();
  expect(screen.getByRole("button", { name: "运行默认实验" })).toBeTruthy();
  expect(screen.queryByText("满足要求")).toBeNull();
});

function actions(run: RunSummaryDto, number: number): RunActionsDto {
  return { run_id: run.run_id, experiment_name: `实验组 ${number}`, scenario_name: "共用的节点场景",
    environment_asset_id: run.environment_asset_id,
    edit: { available: false, experiment_draft_id: `experiment-draft-${number}`, captured_revision: 1,
      current_revision: null, scenario_draft_id: "common-scene", captured_scenario_revision: 1,
      current_scenario_revision: 1, reason: "仅查看历史" },
    rerun: { available: true, reason: null }, timing: null };
}

it("paginates experiment groups and filters statuses across pages without merging a shared scene", async () => {
  const runs: RunSummaryDto[] = Array.from({ length: 8 }, (_, index) => ({ ...runSummary,
    run_id: `library-run-${index}`, experiment_id: `experiment-${index}`, catalog_sequence: String(index + 1),
    lifecycle: index % 2 === 0 ? "Failed" : "Completed", result_available: index % 2 !== 0 }));
  const fetch = installApi({ "/runs": { body: runs }, ...Object.fromEntries(runs.map((run, index) =>
    [`/runs/${run.run_id}/actions`, { body: actions(run, index) }])) });
  renderRoute("/runs");
  expect(await screen.findByRole("heading", { name: "实验组 7" })).toBeTruthy();
  expect(document.querySelectorAll(".experiment-run-group")).toHaveLength(6);
  expect(screen.getByText("8 个实验 · 8 次运行")).toBeTruthy();
  expect(screen.getByRole("button", { name: "上一页" }).hasAttribute("disabled")).toBe(true);
  await userEvent.click(screen.getByRole("button", { name: "下一页" }));
  expect(document.querySelectorAll(".experiment-run-group")).toHaveLength(2);
  expect(screen.getByRole("heading", { name: "实验组 0" })).toBeTruthy();
  expect(screen.getByRole("button", { name: "下一页" }).hasAttribute("disabled")).toBe(true);
  await userEvent.selectOptions(screen.getByLabelText("运行状态"), "Failed");
  expect(screen.getByText("4 个实验 · 4 次运行")).toBeTruthy();
  expect(screen.getByRole("heading", { name: "实验组 6" })).toBeTruthy();
  expect(screen.queryByRole("heading", { name: "实验组 7" })).toBeNull();
  await userEvent.type(screen.getByLabelText("查找实验或运行"), "library-run-0");
  expect(screen.getByText("1 个实验 · 1 次运行")).toBeTruthy();
  expect(screen.getByRole("link", { name: "library-run-0" })).toBeTruthy();
  expect(fetch.mock.calls.every(([, init]) => (init?.method ?? "GET") === "GET")).toBe(true);
});
