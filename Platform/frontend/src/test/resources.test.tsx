import { screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { describe, expect, it } from "vitest";
import { environment, experiment, run, scenario } from "./fixtures";
import { installApi, renderRoute } from "./render";

describe("real resource routes", () => {
  it("renders Environment catalog and detail", async () => {
    installApi({
      "/environments": { body: [environment] },
      [`/environments/${environment.environment_asset_id}`]: { body: environment },
    });
    const catalog = renderRoute("/environments");
    expect(await screen.findByText(environment.environment_asset_id)).toBeTruthy();
    catalog.unmount();
    renderRoute(`/environments/${environment.environment_asset_id}`);
    expect(
      await screen.findByText((_content, element) =>
        element?.tagName === "DD" &&
        element.textContent?.includes(environment.checksum.value) === true,
      ),
    ).toBeTruthy();
    expect(document.body.textContent).not.toContain("/home/");
  });

  it("renders Scenario catalog and version detail", async () => {
    installApi({
      "/scenarios": { body: [scenario] },
      [`/scenarios/${scenario.scenario_id}/versions/${scenario.version}`]: { body: scenario },
    });
    const catalog = renderRoute("/scenarios");
    expect(await screen.findByText(scenario.scenario_id)).toBeTruthy();
    catalog.unmount();
    renderRoute(`/scenarios/${scenario.scenario_id}/versions/${scenario.version}`);
    expect(await screen.findByText(`Node ${scenario.fusion_center_node_id}`)).toBeTruthy();
    expect(screen.getByText(/TX.*RX/)).toBeTruthy();
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
    await userEvent.click(await screen.findByRole("button", { name: "Run this experiment" }));
    expect(await screen.findByText(/Run Monitor · launched-run/)).toBeTruthy();
    const post = fetchMock.mock.calls.find((call) => call[1]?.method === "POST");
    expect(JSON.parse(String(post?.[1]?.body))).toEqual({
      experiment_id: experiment.experiment_id,
      experiment_version: experiment.version,
    });
  });
});
