import { render, screen, within } from "@testing-library/react";
import { expect, it } from "vitest";
import { installApi, renderRoute } from "./render";
import { ApiFailure } from "../api/client";
import { ErrorState } from "../components/common";
import { RunFailure, runFailureExplanation } from "../components/RunFailure";

it("distinguishes backend NotFound from transport unavailable", async () => {
  installApi({
    "/authoring/drafts?kind=environment": { body: [] },
    "/environments/missing/authoring-inputs": {
      status: 404,
      body: { error: { code: "EnvironmentNotFound", message: "missing" } },
    },
  });
  const notFound = renderRoute("/environments/missing");
  expect(await screen.findByText("资源不存在")).toBeTruthy();
  expect(screen.getByText("EnvironmentNotFound")).toBeTruthy();
  notFound.unmount();

  installApi({ "/authoring/drafts?kind=environment": { body: [] }, "/environments/offline/authoring-inputs": new Error("connection refused") });
  renderRoute("/environments/offline");
  expect(await screen.findByText("后端连接不可用")).toBeTruthy();
  expect(screen.getByText("TransportUnavailable")).toBeTruthy();
});

it("offers Chinese recovery guidance and keeps API diagnostics collapsed by default", () => {
  render(<ErrorState error={new ApiFailure("NotFound", "ScenarioNotFound", "Scenario was not found.")} />);
  const alert = screen.getByRole("alert");
  expect(within(alert).getByText(/请返回资源目录，确认所选资源及版本仍然存在/)).toBeTruthy();
  const details = alert.querySelector("details")!;
  expect(details.open).toBe(false);
  expect(within(details).getByText("ScenarioNotFound")).toBeTruthy();
  expect(within(details).getByText("Scenario was not found.")).toBeTruthy();
  expect(alert.querySelector("span")!.textContent).not.toContain("Scenario");
});

it("shows actionable proxy binding errors without requiring technical details", () => {
  render(<ErrorState error={new ApiFailure("ApiFailure","RecordedProxyInvalid",
    "缺少物理接收链路 N1 → N2，包含旁听链路也须绑定；未推断反向信道。")} />);
  expect(screen.getByRole("alert").querySelector("span")!.textContent).toContain("缺少物理接收链路 N1 → N2");
  expect(screen.getByRole("alert").querySelector("details")!.open).toBe(false);
});

it("preserves actionable local validation but never exposes raw English as the default instruction", () => {
  const view = render(<ErrorState error={new Error("保护时间必须非负且小于一个时隙。")} />);
  expect(screen.getByText("保护时间必须非负且小于一个时隙。")).toBeTruthy();
  expect(screen.queryByText("技术详情")).toBeNull();
  view.rerender(<ErrorState error={new Error("Unexpected JSON payload at /internal/file")} />);
  expect(screen.getByRole("alert").querySelector("span")!.textContent).toContain("请检查输入和资源绑定后重试");
  expect(screen.getByText("Unexpected JSON payload at /internal/file").closest("details")?.open).toBe(false);
});

it("retains the original API failure only in details when reporting a partial save", () => {
  render(<ErrorState error={new Error("场景已保存；实验保存失败，请重试。", {
    cause: new ApiFailure("ApiFailure", "DraftConflict", "Expected revision 3, received 2."),
  })} />);
  expect(screen.getByRole("alert").querySelector("span")!.textContent).toBe("场景已保存；实验保存失败，请重试。");
  expect(screen.getByText("Expected revision 3, received 2.").closest("details")?.open).toBe(false);
  expect(screen.getByText("DraftConflict").closest("details")?.open).toBe(false);
});

it("keeps unknown runtime failure diagnostics out of the visible explanation and run-list summary", () => {
  const failure = { code: "InternalError", message: "RunExecutionDefinition failed at Worker::Execute" };
  render(<RunFailure failure={failure} />);
  expect(runFailureExplanation(failure)).toContain("请先检查资源绑定与实验参数");
  expect(runFailureExplanation(failure)).not.toContain("RunExecutionDefinition");
  expect(screen.getByText(failure.message).closest("details")?.open).toBe(false);
  expect(screen.getByText(failure.code).closest("details")?.open).toBe(false);
});
