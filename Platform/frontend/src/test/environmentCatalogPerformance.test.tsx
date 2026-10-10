import { screen, waitFor } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { expect, it } from "vitest";
import { environment } from "./fixtures";
import { installApi, renderRoute } from "./render";

const environments = [environment, { ...environment, environment_asset_id: "large-field" }];
const summaries = { [environment.environment_asset_id]: { name: "小环境", available: true, latitude: 18, longitude: 110, month: 4, water_depth_m: 100 },
  "large-field": { name: "大环境2", available: true, latitude: 20, longitude: 120, month: 5, water_depth_m: 175 } };

it("loads one summary batch for all cards, never every acoustic input", async () => {
  const fetch = installApi({ "/environments": { body: environments }, "/environments/authoring-summaries": { body: summaries } });
  renderRoute("/environments");
  expect(await screen.findByRole("link", { name: "大环境2" })).toBeTruthy();
  expect(screen.getByText("175 m")).toBeTruthy();
  expect(fetch.mock.calls.filter(([url]) => String(url).endsWith("authoring-summaries"))).toHaveLength(1);
  expect(fetch.mock.calls.filter(([url]) => String(url).endsWith("authoring-inputs"))).toHaveLength(0);
});

it("only reads full inputs for the selected scene environment", async () => {
  const fetch = installApi({ "/environments": { body: environments }, "/environments/authoring-summaries": { body: summaries },
    "/scenarios": { body: [] }, "/trajectories": { body: [] }, "/authoring/drafts?kind=scenario": { body: [] },
    "/authoring/capabilities": { body: {} }, "/environments/large-field/authoring-inputs": { body: { available: true, payload: {} } } });
  renderRoute("/workspace/experiment");
  await screen.findByRole("option", { name: "大环境2 · v1" });
  expect(fetch.mock.calls.filter(([url]) => String(url).endsWith("authoring-inputs"))).toHaveLength(0);
  await userEvent.selectOptions(screen.getByLabelText("实验环境"), "large-field");
  await waitFor(() => expect(fetch.mock.calls.filter(([url]) => String(url).endsWith("authoring-inputs"))).toHaveLength(1));
  expect(fetch.mock.calls.some(([url]) => String(url) === `/environments/${environment.environment_asset_id}/authoring-inputs`)).toBe(false);
});
