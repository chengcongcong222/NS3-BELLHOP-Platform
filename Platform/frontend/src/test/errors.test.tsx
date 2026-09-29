import { screen } from "@testing-library/react";
import { expect, it } from "vitest";
import { installApi, renderRoute } from "./render";

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
