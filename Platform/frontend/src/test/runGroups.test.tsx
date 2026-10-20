import { screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { test, expect } from "vitest";
import { installApi, renderRoute } from "./render";
import { result } from "./fixtures";

const group = {
  rerun_available: true,
  group_id: "group-test", state: "PartialFailed", archived: true,
  common_snapshot_sha256: "common-hash", workload_sha256: "packet-hash", workload: { count: "8" },
  members: [
    { member: "A", run_id: "run-a", state: "Completed", result, failure: null },
    { member: "B", run_id: "run-b", state: "Interrupted", result: null, failure: { code: "Interrupted", message: "服务重启，B 未完成" } },
  ],
};

test("repeat archived pair navigates to a new group without editing old drafts", async () => {
  const fetch = installApi({
    "/run-groups/group-test": { body: group },
    "POST /run-groups/group-test/rerun": { body: { ...group, group_id: "group-new" } },
    "/run-groups/group-new": { body: { ...group, group_id: "group-new" } },
  });
  renderRoute("/run-groups/group-test");
  await userEvent.click(await screen.findByRole("button", { name: "按原配置重跑 A+B" }));
  expect(fetch.mock.calls.some(([url, init]) => url === "/run-groups/group-test/rerun" && init?.method === "POST")).toBe(true);
  expect(fetch.mock.calls.some(([url]) => String(url).includes("/authoring/drafts"))).toBe(false);
});

test("archived comparison keeps A result without pretending interrupted B succeeded or replay survived", async () => {
  installApi({ "/run-groups/group-test": { body: group } });
  renderRoute("/run-groups/group-test");
  expect(await screen.findByText("部分完成")).toBeTruthy();
  expect(screen.getByText("服务重启，B 未完成")).toBeTruthy();
  expect(screen.getAllByText("尚无结果").length).toBe(3);
  expect(screen.queryByText("查看运行与回放")).toBeNull();
  await userEvent.click(screen.getByRole("button", { name: "A 结果" }));
  expect(screen.getByText("运行概览", { selector: "a" })).toBeTruthy();
  await userEvent.click(screen.getByRole("button", { name: "B 结果" }));
  expect(screen.getByText("该成员尚无可用结果。请查看上方运行状态。")).toBeTruthy();
  await userEvent.click(screen.getByText("冻结输入与结果导出"));
  expect(screen.getByRole("link", { name: "下载对照组归档" }).getAttribute("href")).toBe("/run-groups/group-test/export");
});

test("pending B can be canceled without canceling A", async () => {
  const fetch = installApi({
    "/run-groups/group-test": { body: { ...group, archived: false, state: "RunningA", members: group.members.map((m, i) => ({ ...m, state: i ? "Queued" : "Running", result: null, failure: null })) } },
    "POST /run-groups/group-test/cancel-pending": { body: group },
  });
  renderRoute("/run-groups/group-test");
  await userEvent.click(await screen.findByRole("button", { name: "取消尚未开始的 B" }));
  expect(fetch.mock.calls.some(([url, init]) => url === "/run-groups/group-test/cancel-pending" && init?.method === "POST")).toBe(true);
});
