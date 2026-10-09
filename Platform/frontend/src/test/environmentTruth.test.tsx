import { act, fireEvent, render, screen, waitFor } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { expect, it, vi } from "vitest";
import { apiClient } from "../api/client";
import type { AuthoringDraftDto } from "../api/types";
import { DraftEnvironmentProfile } from "../components/EnvironmentProfile";
import { newEnvironmentDraft } from "../domain/workspace";
import { installApi, renderRoute } from "./render";

it("loading terrain cannot clamp or rescale the sound speed profile", () => {
  const draft = { ...newEnvironmentDraft(), soundSpeedProfile: [{ depthMeters: 0, speedMetersPerSecond: 1500 }, { depthMeters: 175, speedMetersPerSecond: 1490 }] };
  const view = render(<DraftEnvironmentProfile draft={draft} />);
  const points = screen.getByRole("img", { name: "草稿声速剖面" }).querySelector("polyline")!.getAttribute("points");
  view.rerender(<DraftEnvironmentProfile draft={{ ...draft, maximumDepthMeters: 110, bathymetry: [{ rangeMeters: 0, depthMeters: 100 }, { rangeMeters: 1000, depthMeters: 110 }] }} />);
  const profile = screen.getByRole("img", { name: "草稿声速剖面" });
  expect(profile.querySelector("polyline")!.getAttribute("points")).toBe(points);
  expect(profile.textContent).toContain("175");
  expect(profile.textContent).toContain("1490");
});

it("offers one saved-profile selector without a duplicate reference-source selector", async () => {
  installApi({ "/authoring/drafts?kind=environment": { body: [] },
    "/authoring/environment-capabilities": { body: { woa_online_available: true, bellhop_available: true } } });
  renderRoute("/workspace/environment");
  await screen.findByLabelText("已保存的水文剖面");
  expect(screen.queryByLabelText("数据来源")).toBeNull();
  expect((screen.getByLabelText("经度（°）") as HTMLInputElement).value).toBe("110");
  expect((screen.getByLabelText("经度（°）") as HTMLInputElement).disabled).toBe(false);
  expect(screen.getByRole("button", { name: "获取 WOA 数据" })).toBeTruthy();
});

it("requires an explicit environment name before saving and focuses its input", async () => {
  const fetch = installApi({ "/authoring/drafts?kind=environment": { body: [] } });
  renderRoute("/workspace/environment");
  await userEvent.click(await screen.findByRole("button", { name: "保存草稿" }));
  expect((await screen.findByRole("alert")).textContent).toContain("请先填写环境名称");
  expect(document.activeElement).toBe(screen.getByLabelText("环境名称"));
  expect(fetch.mock.calls.some(([, init]) => init?.method === "POST")).toBe(false);
});

it("saves explicitly applied uniform depths through the real authoring adapter", async () => {
  installApi({ "/authoring/drafts?kind=environment": { body: [] } });
  const save = vi.spyOn(apiClient, "createDraft").mockImplementation(async (kind, name, payload) => ({
    kind, name, payload, draft_id: "uniform-depths", revision: 1, created_at: "", updated_at: "", validation_issues: [],
  }));
  renderRoute("/workspace/environment");
  fireEvent.change(await screen.findByLabelText("环境名称"), { target: { value: "均分采样海域" } });
  fireEvent.change(screen.getByLabelText("发送深度起点（m）"), { target: { value: "10" } });
  fireEvent.change(screen.getByLabelText("发送深度终点（m）"), { target: { value: "70" } });
  fireEvent.change(screen.getByLabelText("发送深度采样点数"), { target: { value: "4" } });
  fireEvent.click(screen.getAllByRole("button", { name: "应用均分", hidden: true })[0]);
  await userEvent.click(screen.getByRole("button", { name: "保存草稿" }));
  await waitFor(() => expect(save).toHaveBeenCalledTimes(1));
  const bellhop = save.mock.calls[0][2].bellhop as Record<string, unknown>;
  expect(bellhop.source_depths_meters).toEqual([10, 30, 50, 70]);
  expect(bellhop.receiver_depths_meters).toEqual([8, 20, 40, 60, 75]);
  save.mockRestore();
});

it("keeps failed save feedback visible and clears the pending message", async () => {
  installApi({ "/authoring/drafts?kind=environment": { body: [] } });
  const save = vi.spyOn(apiClient, "createDraft").mockRejectedValue(new Error("保存服务暂不可用"));
  renderRoute("/workspace/environment");
  fireEvent.change(await screen.findByLabelText("环境名称"), { target: { value: "测试保存" } });
  await userEvent.click(screen.getByRole("button", { name: "保存草稿" }));
  expect((await screen.findByRole("alert")).textContent).toContain("保存服务暂不可用");
  expect(screen.queryByText("正在保存…")).toBeNull();
  expect(document.querySelector(".environment-action-feedback [role=alert]")).toBeTruthy();
  save.mockRestore();
});

it("has one publish action with pending and success feedback above the result tab", async () => {
  const draft: AuthoringDraftDto = { kind: "environment", draft_id: "water", revision: 1,
    name: "测试海域", created_at: "", updated_at: "", validation_issues: [], payload: {
      maximum_depth_meters: 100, maximum_range_meters: 1000,
      sound_speed_profile: [{ depth_meters: 0, speed_meters_per_second: 1500 }, { depth_meters: 100, speed_meters_per_second: 1490 }],
      sources: { bottom_boundary: { kind: "ideal-flat-bottom" } },
    } };
  installApi({ "/authoring/drafts?kind=environment": { body: [draft] }, "/environments": { body: [] },
    "/authoring/environment-builds/build-test/acoustic-plots?source_index=0&receiver_index=0&range_index=0": { body: { available: false, reason: "测试仅验证发布交互" } } });
  const update = vi.spyOn(apiClient, "updateDraft").mockImplementation(async (_old, name, payload) => ({ ...draft, name, payload, revision: 2 }));
  const build = vi.spyOn(apiClient, "createEnvironmentBuild").mockResolvedValue({ build_id: "build-test", draft_id: "water", state: "Completed", steps: [], created_at: "", updated_at: "", artifact: {}, failure: null });
  let complete!: (value: Record<string, unknown>) => void;
  const publish = vi.spyOn(apiClient, "publishEnvironmentBuild").mockImplementation(() => new Promise(resolve => { complete = resolve; }));
  renderRoute("/workspace/environment?draft=water");
  await userEvent.click(await screen.findByRole("button", { name: "开始离线声场构建" }));
  const button = await screen.findByRole("button", { name: "发布为正式环境" });
  expect(screen.getAllByRole("button", { name: "发布为正式环境" })).toHaveLength(1);
  expect(button.closest(".workspace-toolbar")).toBeTruthy();
  await userEvent.click(button);
  expect(await screen.findByRole("button", { name: "正在发布…" })).toBeTruthy();
  await act(async () => complete({ environment_asset_id: "published-test" }));
  await waitFor(() => expect(screen.getByRole("button", { name: "已发布" }).hasAttribute("disabled")).toBe(true));
  expect(document.querySelector(".environment-action-feedback")?.textContent).toContain("已发布，可在实验工作台选择");
  expect(publish).toHaveBeenCalledTimes(1);
  update.mockRestore(); build.mockRestore(); publish.mockRestore();
});

it("reopens exact environment inputs including bottom, beams, location and month", async () => {
  installApi({ "/authoring/drafts?kind=environment": { body: [{ kind: "environment", draft_id: "water", revision: 7,
    name: "保存的海域", created_at: "", updated_at: "", validation_issues: [], payload: {
      acquisition: { latitudeDeg: 20, longitudeDeg: 120, month: 9, useReferenceCache: false },
      bottom: { sound_speed_meters_per_second: 1720, density_grams_per_cubic_centimeter: 1.9, attenuation_db_per_wavelength: .7 },
      bellhop: { beam_count: 301, source_depths_meters: [10, 20], receiver_depths_meters: [15, 25], range_sample_count: 30 },
    } }] } });
  renderRoute("/workspace/environment?draft=water");
  expect((await screen.findByLabelText("经度（°）") as HTMLInputElement).value).toBe("120");
  expect((screen.getByLabelText("月份") as HTMLInputElement).value).toBe("9");
  expect((screen.getByLabelText("海底声速（m/s）") as HTMLInputElement).value).toBe("1720");
  expect((screen.getByLabelText("波束数") as HTMLInputElement).value).toBe("301");
});

it.each([false, true])("preserves edits made during %s existing environment save and updates the next revision", async existing => {
  const previous: AuthoringDraftDto = { kind: "environment", draft_id: "water", revision: 7,
    name: "保存前海域", created_at: "", updated_at: "", validation_issues: [], payload: { center_frequency_hz: 25000 } };
  installApi({ "/authoring/drafts?kind=environment": { body: existing ? [previous] : [] },
    "/authoring/environment-capabilities": { body: {} } });
  let complete: ((draft: AuthoringDraftDto) => void) | undefined;
  let sent: AuthoringDraftDto | undefined;
  const pending = new Promise<AuthoringDraftDto>(resolve => { complete = resolve; });
  const create = vi.spyOn(apiClient, "createDraft").mockImplementation(async (_kind, name, payload) => {
    sent = { ...previous, name, payload, revision: 1 }; return pending;
  });
  const update = vi.spyOn(apiClient, "updateDraft").mockImplementation(async (draft, name, payload) => {
    const next = { ...draft, name, payload, revision: draft.revision + 1 };
    if (!sent) { sent = next; return pending; }
    return next;
  });
  renderRoute(`/workspace/environment${existing ? "?draft=water" : ""}`);
  const name = await screen.findByLabelText("环境名称");
  fireEvent.change(name, { target: { value: "第一次提交" } });
  await userEvent.click(screen.getByRole("button", { name: "保存草稿" }));
  await waitFor(() => expect(sent).toBeTruthy());
  fireEvent.change(name, { target: { value: "请求期间改名" } });
  fireEvent.change(screen.getByLabelText("说明"), { target: { value: "不能丢失的新说明" } });
  fireEvent.change(screen.getByLabelText("波束数"), { target: { value: "701" } });
  fireEvent.change(screen.getByLabelText("月份"), { target: { value: "8" } });
  await act(async () => complete!(sent!));
  expect(await screen.findByText("上次提交的内容已保存；保存期间的新修改仍保留在页面，请再次保存。")).toBeTruthy();
  expect((name as HTMLInputElement).value).toBe("请求期间改名");
  expect((screen.getByLabelText("说明") as HTMLTextAreaElement).value).toBe("不能丢失的新说明");
  expect((screen.getByLabelText("波束数") as HTMLInputElement).value).toBe("701");
  expect((screen.getByLabelText("月份") as HTMLInputElement).value).toBe("8");
  await userEvent.click(screen.getByRole("button", { name: "保存草稿" }));
  expect(await screen.findByText("草稿已保存到服务器工作区")).toBeTruthy();
  expect(update.mock.calls.at(-1)).toEqual([sent, "请求期间改名", expect.objectContaining({
    description: "不能丢失的新说明", bellhop: expect.objectContaining({ beam_count: 701 }),
    acquisition: expect.objectContaining({ month: 8 }),
  })]);
  expect(screen.getByText(`当前修订 ${existing ? 9 : 2}`)).toBeTruthy();
  create.mockRestore(); update.mockRestore();
});

it("shows real download progress and cancellation beside the acquisition button", async () => {
  installApi({ "/authoring/drafts?kind=environment": { body: [] },
    "/authoring/environment-capabilities": { body: { woa_online_available: true } } });
  const spy = vi.spyOn(apiClient, "acquireWoaProgress").mockImplementation((_request, progress, signal) => {
    progress({ stage: "downloading", message: "正在下载 NOAA 月度数据", dataset: "温度", file_index: 1, file_count: 2,
      received_bytes: 1048576, total_bytes: 2097152 });
    return new Promise((_resolve, reject) => signal.addEventListener("abort", () => reject(new DOMException("cancel", "AbortError"))));
  });
  renderRoute("/workspace/environment");
  await userEvent.click(await screen.findByRole("button", { name: "获取 WOA 数据" }));
  expect(await screen.findByText("1.0 MB / 2.0 MB")).toBeTruthy();
  expect(screen.getByRole("progressbar").getAttribute("value")).toBe("1048576");
  await userEvent.click(screen.getByRole("button", { name: "取消获取" }));
  expect((await screen.findByRole("alert")).textContent).toContain("已取消获取");
  expect(screen.getByRole("button", { name: "获取 WOA 数据" }).hasAttribute("disabled")).toBe(false);
  spy.mockRestore();
});

it("reuses a saved profile with its exact query and does not fetch online or expose terrain", async () => {
  const cacheId = "a".repeat(64);
  const query = { latitude_deg: 23, longitude_deg: 119, month: 9, maximum_depth_meters: 100, use_reference_cache: false };
  const points = [{ depth_meters: 0, speed_meters_per_second: 1500 }, { depth_meters: 100, speed_meters_per_second: 1490 }];
  const fetchMock = installApi({ "/authoring/drafts?kind=environment": { body: [] },
    "/authoring/environment-capabilities": { body: { woa_online_available: true, hydrology_cache_available: true } },
    "/authoring/environment/hydrology-cache": { body: [{ cache_id: cacheId, provider: "NOAA WOA23", point_count: 2, maximum_depth_meters: 100, selection: query }] },
    [`/authoring/environment/hydrology-cache/${cacheId}`]: { body: { cache_id: cacheId, selection: query,
      result: { source: "online-real-data", provider: "NOAA WOA23", sound_speed_profile: points } } } });
  renderRoute("/workspace/environment");
  await screen.findByRole("option", { name: /NOAA WOA23/ });
  await userEvent.selectOptions(screen.getByLabelText("已保存的水文剖面"), cacheId);
  await userEvent.click(screen.getByRole("button", { name: "使用所选水文" }));
  expect(await screen.findByText(/已复用服务器保存的水文剖面/)).toBeTruthy();
  expect((screen.getByLabelText("纬度（°）") as HTMLInputElement).value).toBe("23");
  expect((screen.getByLabelText("月份") as HTMLInputElement).value).toBe("9");
  expect(screen.queryByText("地形来源")).toBeNull();
  expect(fetchMock.mock.calls.some(call => String(call[0]).includes("/acquisition/"))).toBe(false);
  expect(screen.queryByLabelText("采用平坦海底计算边界（非实测地形）")).toBeNull();
  expect(screen.queryByRole("button", { name: "确认计算水深与范围" })).toBeNull();
  expect(screen.getByRole("button", { name: "开始离线声场构建" }).hasAttribute("disabled")).toBe(false);
  await userEvent.click(screen.getByRole("button", { name: "下一步：计算参数" }));
  expect(document.querySelector(".environment-builder")?.getAttribute("data-step")).toBe("4");
  fireEvent.change(screen.getByLabelText("计算水深（m，不超过声速剖面）"), { target: { value: "101" } });
  expect(screen.getByRole("button", { name: "开始离线声场构建" }).hasAttribute("disabled")).toBe(true);
});

it("shows rejected hydrology imports instead of losing errors in an async file callback", async () => {
  installApi({ "/authoring/drafts?kind=environment": { body: [] } });
  const spy = vi.spyOn(apiClient, "importSsp").mockRejectedValue(new Error("水文文件深度重复"));
  renderRoute("/workspace/environment");
  await userEvent.click(await screen.findByRole("button", { name: /导入水文文件/ }));
  const file = new File(["0,1500\n0,1501"], "invalid.csv", { type: "text/csv" });
  Object.defineProperty(file, "text", { value: async () => "0,1500\n0,1501" });
  await userEvent.upload(screen.getByLabelText("选择水文文件"), file);
  expect((await screen.findByRole("alert")).textContent).toContain("水文文件深度重复");
  spy.mockRestore();
});
