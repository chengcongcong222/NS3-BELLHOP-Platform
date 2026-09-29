import { act, screen, waitFor, within } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { describe, expect, it, vi } from "vitest";
import type { RunActionsDto, RunDto, RunSummaryDto } from "../api/types";
import { run, runSummary } from "./fixtures";
import { installApi, renderRoute } from "./render";

const actions = (id: string, draft = "experiment-user-exact"): RunActionsDto => ({
  run_id: id, experiment_name: "用户通信实验", scenario_name: "共同节点场景", environment_asset_id: run.environment_asset_id,
  edit: { available: true, experiment_draft_id: draft, captured_revision: 4, current_revision: 7,
    scenario_draft_id: "scene-user-exact", captured_scenario_revision: 3, current_scenario_revision: 5, reason: null },
  rerun: { available: true, reason: null }, timing: null,
});
const actionPath = (id: string) => `/runs/${id}/actions`;

class ControlledSource {
  static current: ControlledSource | null = null;
  onerror: ((event: Event) => void) | null = null;
  constructor(_url: string) { ControlledSource.current = this; }
  addEventListener() {}
  close() {}
}

describe("run-scoped edit, repeat and experiment history", () => {
  it("repeats the frozen run through POST and navigates to a new Run without writing later drafts", async () => {
    const id = "run-before-repeat", next = "run-exact-repeat";
    const original: RunDto = { ...run, run_id: id };
    const created: RunDto = { ...run, run_id: next, lifecycle: "Created", failure: null };
    const fetch = installApi({
      [`/runs/${id}`]: { body: original }, [actionPath(id)]: { body: actions(id) },
      [`POST /runs/${id}/rerun`]: { body: created },
      [`/runs/${next}`]: { body: created }, [actionPath(next)]: { body: actions(next) },
    });
    renderRoute(`/runs/${id}`);
    const repeat = await screen.findByRole("button", { name: "按原配置重新运行" });
    expect(repeat.getAttribute("title")).toContain("创建新记录，不覆盖当前运行");
    expect(screen.getByText(/重新运行：使用本次冻结配置，不读取后续草稿修改/)).toBeTruthy();
    await userEvent.click(repeat);
    expect(await screen.findByText(next)).toBeTruthy();
    expect(screen.queryByText(id)).toBeNull();
    expect(document.querySelector(".studio-product")?.getAttribute("data-presentation")).toBe("progressive");
    expect(document.querySelector(".studio-product")?.getAttribute("data-visible-event-count")).toBe("0");
    const writes = fetch.mock.calls.filter(([, init]) => init?.method && init.method !== "GET");
    expect(writes).toHaveLength(1);
    expect(writes[0][0]).toBe(`/runs/${id}/rerun`);
    expect(writes[0][1]?.method).toBe("POST");
    expect(writes[0][1]?.body).toBeUndefined();
    expect(fetch.mock.calls.some(([url]) => String(url).startsWith("/authoring/"))).toBe(false);
    expect(original.run_id).toBe(id);
    expect(original.lifecycle).toBe("Completed");
  });

  it("opens only the exact source experiment draft, never an empty editor or a same-scene experiment", async () => {
    const id = "run-original-draft";
    installApi({ [`/runs/${id}`]: { body: { ...run, run_id: id } },
      [actionPath(id)]: { body: actions(id, "experiment-specific-27") } });
    renderRoute(`/runs/${id}`);
    expect((await screen.findByRole("link", { name: "修改本实验" })).getAttribute("href"))
      .toBe("/workspace/experiment?draft=experiment-specific-27");
    expect(screen.queryByRole("link", { name: "新建实验" })).toBeNull();
  });

  it("does not provide a misleading blank edit link when the original draft is unavailable", async () => {
    const id = "run-deleted-source", detail = actions(id);
    detail.edit = { ...detail.edit, available: false, experiment_draft_id: null, reason: "原实验草稿已不存在。" };
    detail.rerun = { available: false, reason: "本次冻结资源尚未完整保存。" };
    installApi({ [`/runs/${id}`]: { body: { ...run, run_id: id } }, [actionPath(id)]: { body: detail } });
    renderRoute(`/runs/${id}`);
    expect(await screen.findByText("原草稿不可编辑")).toBeTruthy();
    expect(screen.queryByRole("link", { name: "修改本实验" })).toBeNull();
    expect((screen.getByRole("button", { name: "按原配置重新运行" }) as HTMLButtonElement).disabled).toBe(true);
    expect(screen.getByRole("button", { name: "按原配置重新运行" }).getAttribute("title")).toBe("本次冻结资源尚未完整保存。");
  });

  it("keeps runs on two experiments separate even when their captured scene is identical", async () => {
    const items: RunSummaryDto[] = [
      { ...runSummary, run_id: "run-A-1", experiment_id: "exp-A-r1", catalog_sequence: "1" },
      { ...runSummary, run_id: "run-B-1", experiment_id: "exp-B-r1", catalog_sequence: "2" },
      { ...runSummary, run_id: "run-A-2", experiment_id: "exp-A-r2", experiment_version: "2", catalog_sequence: "3" },
    ];
    const fetch = installApi({ "/runs": { body: items },
      [actionPath("run-A-1")]: { body: { ...actions("run-A-1", "draft-A"), experiment_name: "实验甲 · 模式 A" } },
      [actionPath("run-A-2")]: { body: { ...actions("run-A-2", "draft-A"), experiment_name: "实验甲 · 模式 A" } },
      [actionPath("run-B-1")]: { body: { ...actions("run-B-1", "draft-B"), experiment_name: "实验乙 · 模式 B" } },
    });
    renderRoute("/runs");
    const groupA = (await screen.findByRole("heading", { name: "实验甲 · 模式 A" })).closest("section")!;
    const groupB = (await screen.findByRole("heading", { name: "实验乙 · 模式 B" })).closest("section")!;
    expect(document.querySelectorAll(".experiment-run-group")).toHaveLength(2);
    expect(within(groupA).getByText("2 次运行")).toBeTruthy();
    expect(within(groupA).getByRole("link", { name: "run-A-1" })).toBeTruthy();
    expect(within(groupA).getByRole("link", { name: "run-A-2" })).toBeTruthy();
    expect(within(groupA).queryByRole("link", { name: "run-B-1" })).toBeNull();
    expect(within(groupB).getByText("1 次运行")).toBeTruthy();
    expect(within(groupB).queryByRole("link", { name: "run-B-1" })).toBeNull();
    await userEvent.click(within(groupB).getByRole("button", { name: "展开 实验乙 · 模式 B" }));
    expect(within(groupB).getByRole("link", { name: "run-B-1" })).toBeTruthy();
    await userEvent.click(within(groupB).getByText("重新运行 / 修改"));
    expect(within(groupB).getByRole("link", { name: "修改本实验" }).getAttribute("href")).toBe("/workspace/experiment?draft=draft-B");
    await userEvent.type(screen.getByLabelText("查找实验或运行"), "实验乙");
    expect(screen.queryByRole("link", { name: "run-A-1" })).toBeNull();
    expect(screen.getByRole("link", { name: "run-B-1" })).toBeTruthy();
    expect(fetch.mock.calls.every(([, init]) => (init?.method ?? "GET") === "GET")).toBe(true);
  });

  it("shows the authoritative TDMA failure and timing diagnostic rather than a later SSE disconnect", async () => {
    ControlledSource.current = null; vi.stubGlobal("EventSource", ControlledSource);
    const id = "run-tdma-failed";
    const failure = { code: "FailedPrecondition", message: "TDMA slot cannot contain payload airtime and guard interval." };
    const failed: RunDto = { ...run, run_id: id, lifecycle: "Failed", failure, event_stream_complete: false };
    const detail = actions(id);
    detail.timing = { minimum_airtime_ns: "2000000000", guard_interval_ns: "2000000000", minimum_slot_duration_ns: "4000000000",
      configured_slot_duration_ns: "1000000000", slot_contains_airtime_and_guard: false, explanation: "发送时长加保护时间" };
    const fetch = installApi({ [`/runs/${id}`]: { body: failed }, [actionPath(id)]: { body: detail } });
    renderRoute(`/runs/${id}`);
    const alert = await screen.findByRole("alert");
    expect(within(alert).getByRole("heading", { name: "本次运行未完成" })).toBeTruthy();
    expect(alert.textContent).toContain("发送时隙太短，放不下当前数据包和保护时间");
    expect(within(alert).getByText(failure.message)).toBeTruthy();
    expect(within(alert).getByText(failure.code)).toBeTruthy();
    expect(await screen.findByText(/本次配置的时隙为 1 s.*至少需要 4 s/)).toBeTruthy();
    await waitFor(() => expect(ControlledSource.current?.onerror).toBeTypeOf("function"));
    act(() => ControlledSource.current?.onerror?.(new Event("error")));
    expect(screen.getByRole("alert").textContent).toContain(failure.message);
    expect(screen.queryByText(/事件连接暂时中断/)).toBeNull();
    expect(screen.queryByText("TransportUnavailable")).toBeNull();
    expect((screen.getByRole("button", { name: "查看结果" }) as HTMLButtonElement).disabled).toBe(true);
    expect(fetch.mock.calls.some(([url]) => String(url) === `/runs/${id}/results`)).toBe(false);
    expect(fetch.mock.calls.every(([, init]) => (init?.method ?? "GET") === "GET")).toBe(true);
  });

  it("keeps the current run visible when exact repeat fails and reports the specific resource failure", async () => {
    const id = "run-repeat-missing-resource";
    const fetch = installApi({ [`/runs/${id}`]: { body: { ...run, run_id: id } }, [actionPath(id)]: { body: actions(id) },
      [`POST /runs/${id}/rerun`]: { status: 404, body: { error: { code: "TrajectoryVersionNotFound", message: "原运行轨迹版本 3 不存在，未创建新运行。" } } },
    });
    renderRoute(`/runs/${id}`);
    await userEvent.click(await screen.findByRole("button", { name: "按原配置重新运行" }));
    expect((await screen.findByText("原运行轨迹版本 3 不存在，未创建新运行。")).closest("[role=alert]")).toBeTruthy();
    expect(screen.getByText(id)).toBeTruthy();
    expect(fetch.mock.calls.filter(([, init]) => init?.method === "POST")).toHaveLength(1);
    expect(fetch.mock.calls.some(([url]) => String(url).startsWith("/authoring/"))).toBe(false);
  });
});
