import { fireEvent, screen, waitFor, within } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { expect, it } from "vitest";
import type { AuthoringDraftDto, TrajectoryAssetDto } from "../api/types";
import { environment, experiment, run, scenario } from "./fixtures";
import { installApi, renderRoute } from "./render";

const trajectory: TrajectoryAssetDto = { asset_id: "chosen-track", version: 3, name: "用户航迹", source: "Csv",
  coordinate_system: "LocalCartesianMetersZUp", checksum_sha256: "test", source_notice: null,
  tracks: [{ track_id: "N0", points: [{ time_s: 0, x_m: 234, y_m: 80, z_m: -30 }, { time_s: 120, x_m: 260, y_m: 80, z_m: -30 }] }] };
const fixture = { "/environments": { body: [environment] }, "/scenarios": { body: [scenario] },
  "/authoring/capabilities": { body: { relative_trajectory_placement: true, exact_run_repeat: true } },
  "/experiments": { body: [experiment] }, "/trajectories": { body: [trajectory] },
  "/authoring/drafts?kind=scenario": { body: [] }, "/authoring/drafts?kind=experiment": { body: [] } };

function server() {
  const records = new Map<string, AuthoringDraftDto>();
  const fetchMock = installApi(fixture);
  const base = fetchMock.getMockImplementation()!;
  fetchMock.mockImplementation(async (input, init) => {
    const url = String(input);
    if (url.startsWith("/authoring/drafts?kind=")) return new Response(JSON.stringify([...records.values()].filter(x => x.kind === url.split("=")[1])));
    if ((init?.method === "POST" && url === "/authoring/drafts") || init?.method === "PUT") {
      const write = JSON.parse(String(init.body));
      const old = records.get(write.kind);
      if (init.method === "PUT") expect(new Headers(init.headers).get("If-Match")).toBe(String(old!.revision));
      const draft = { ...write, draft_id: `${write.kind}-exact`, revision: (old?.revision ?? 0) + 1,
        created_at: "2026-09-17T00:00:00Z", updated_at: "2026-09-17T00:00:00Z", validation_issues: [] };
      records.set(write.kind, draft);
      return new Response(JSON.stringify(draft));
    }
    if (url === "/authoring/experiments/experiment-exact/runs") return new Response(JSON.stringify({ ...run, run_id: "unified-run" }));
    if (url === "/runs/unified-run") return new Response(JSON.stringify({ ...run, run_id: "unified-run" }));
    return base(input, init);
  });
  return { records, fetchMock };
}

it("switches node/background inspector without losing protocol settings or using a preset", async () => {
  installApi(fixture); renderRoute("/workspace/experiment");
  const canvas = await screen.findByRole("img", { name: "可编辑场景画布" });
  expect((screen.getByLabelText("实验环境") as HTMLSelectElement).value).toBe("");
  expect((screen.getByLabelText("使用已有节点场景") as HTMLSelectElement).value).toBe("");
  await userEvent.selectOptions(screen.getByLabelText("物理层实现"), "Channel2Waveform");
  await userEvent.selectOptions(screen.getByLabelText("调制方式"), "Bfsk");
  fireEvent.pointerDown(screen.getByRole("button", { name: "选择节点 N0" }));
  expect(screen.queryByLabelText("物理层实现")).toBeNull();
  await userEvent.selectOptions(screen.getByLabelText("绑定轨迹资源"), JSON.stringify(["chosen-track", 3, "N0"]));
  expect((screen.getByLabelText("X（m）") as HTMLInputElement).value).toBe("0");
  expect((screen.getByLabelText("X（m）") as HTMLInputElement).disabled).toBe(false);
  fireEvent.pointerDown(canvas);
  expect((screen.getByLabelText("调制方式") as HTMLSelectElement).value).toBe("Bfsk");
});

it("keeps save controls in the left sidebar even when the node list is collapsed or a node is selected", async () => {
  const fetch = installApi(fixture); renderRoute("/workspace/experiment");
  await screen.findByRole("img", { name: "可编辑场景画布" });
  const sidebar = screen.getByRole("complementary", { name: "场景节点工具" });
  const controls = within(sidebar).getByRole("region", { name: "实验操作" });
  expect(within(controls).getByLabelText("实验名称")).toBeTruthy();
  expect(within(controls).getByRole("button", { name: "保存并运行" })).toBeTruthy();
  expect(document.querySelector('.authoring-layout > .unified-experiment-toolbar')).toBeNull();
  await userEvent.click(screen.getByRole("button", { name: "节点列表" }));
  expect(within(sidebar).queryByRole("button", { name: /N0/ })).toBeNull();
  expect(within(controls).getByRole("button", { name: "保存实验草稿" })).toBeTruthy();
  fireEvent.pointerDown(screen.getByRole("button", { name: "选择节点 N0" }));
  expect(within(screen.getByRole("complementary", { name: "节点与实验属性" })).getByLabelText("节点编号")).toBeTruthy();
  expect(within(controls).getByRole("button", { name: "保存并运行" })).toBeTruthy();
  expect(fetch.mock.calls.every(([, init]) => !init?.method || init.method === "GET")).toBe(true);
});

it("preserves absolute binding on an older backend without silently enabling relative placement", async () => {
  const fetch = installApi({ ...fixture, "/authoring/capabilities": { status: 404, body: { detail: "Not Found" } } });
  renderRoute("/workspace/experiment");
  fireEvent.pointerDown(await screen.findByRole("button", { name: "选择节点 N0" }));
  await userEvent.selectOptions(screen.getByLabelText("绑定轨迹资源"), JSON.stringify(["chosen-track", 3, "N0"]));
  expect((screen.getByLabelText("X（m）") as HTMLInputElement).value).toBe("234");
  expect((screen.getByLabelText("X（m）") as HTMLInputElement).disabled).toBe(true);
  expect(screen.getByText(/当前服务尚未支持场景起点布设/)).toBeTruthy();
  expect(screen.queryByRole("button", { name: "改为跟随场景起点" })).toBeNull();
  expect(fetch.mock.calls.some(([, init]) => init?.method === "POST" || init?.method === "PUT")).toBe(false);
});

it("saves scene then experiment and freezes the exact scene revision before a real run request", async () => {
  const { records, fetchMock } = server(); renderRoute("/workspace/experiment");
  await userEvent.selectOptions(await screen.findByLabelText("实验环境"), environment.environment_asset_id);
  fireEvent.pointerDown(screen.getByRole("button", { name: "选择节点 N0" }));
  await userEvent.selectOptions(screen.getByLabelText("绑定轨迹资源"), JSON.stringify(["chosen-track", 3, "N0"]));
  for (let i = 0; i < 2; i++) {
    await userEvent.click(screen.getByRole("button", { name: "＋ 添加节点" }));
    await userEvent.selectOptions(screen.getByLabelText("绑定轨迹资源"), JSON.stringify(["chosen-track", 3, "N0"]));
  }
  await userEvent.click(screen.getByRole("button", { name: "环境与实验参数" }));
  await userEvent.selectOptions(screen.getByLabelText("业务负载"), "CommunicationOnly");
  await userEvent.click(screen.getByRole("button", { name: "保存实验草稿" }));
  await waitFor(() => expect(records.get("experiment")?.payload.scenario_revision).toBe(1));
  expect(records.get("scenario")?.payload.trajectory_bindings).toEqual([0, 1, 2].map(node_id => ({ node_id: String(node_id), asset_id: "chosen-track", version: 3, track_id: "N0", placement_mode: "RelativeToNodeStart" })));
  expect((records.get("scenario")?.payload.nodes as typeof scenario.nodes)[0].initial_position.x_meters).toBe(0);
  await userEvent.selectOptions(screen.getByLabelText("物理层实现"), "Channel2Waveform");
  await userEvent.click(screen.getByRole("button", { name: "保存并运行" }));
  await waitFor(() => expect(fetchMock.mock.calls.some(([url]) => String(url) === "/authoring/experiments/experiment-exact/runs")).toBe(true));
  expect(records.get("experiment")?.payload.scenario_revision).toBe(2);
  expect(records.get("experiment")?.payload.physical_layer).toBe("Channel2Waveform");
  expect(records.get("experiment")?.payload.scenario_id).toBe("scenario-exact");
  expect(fetchMock).toHaveBeenCalledWith("/authoring/experiments/experiment-exact/runs", expect.objectContaining({ headers: { "If-Match": "2" } }));
});

it("cannot launch or save an experiment when the scene save fails", async () => {
  const fetchMock = installApi({ ...fixture, "POST /authoring/drafts": { status: 409, body: { error: { code: "DraftConflict", message: "场景保存冲突" } } } });
  renderRoute("/workspace/experiment");
  await userEvent.click(await screen.findByRole("button", { name: "保存并运行" }));
  expect((await screen.findByRole("alert")).textContent).toContain("场景保存冲突");
  expect(fetchMock.mock.calls.filter(([, init]) => init?.method === "POST")).toHaveLength(1);
  expect(fetchMock.mock.calls.some(([url]) => String(url).endsWith("/runs"))).toBe(false);
});

it("rejects a missing requested experiment instead of silently editing a default", async () => {
  installApi(fixture); renderRoute("/workspace/experiment?draft=missing");
  expect((await screen.findByRole("alert")).textContent).toContain("不存在");
  expect(screen.queryByRole("button", { name: "保存并运行" })).toBeNull();
});

it("saves unsupported node counts but cannot quietly replace them with a runnable preset", async () => {
  const { records, fetchMock } = server(); renderRoute("/workspace/experiment");
  await userEvent.click(await screen.findByRole("button", { name: "保存并运行" }));
  expect((await screen.findByRole("alert")).textContent).toContain("仅支持 3～6 个节点");
  expect(records.get("scenario")?.payload.nodes).toHaveLength(1);
  expect(fetchMock.mock.calls.some(([url]) => String(url).endsWith("/runs"))).toBe(false);
  // A successful draft save must not keep an error from an earlier run attempt.
  await userEvent.click(screen.getByRole("button", { name: "保存实验草稿" }));
  await waitFor(() => expect(screen.queryByRole("alert")).toBeNull());
  expect(fetchMock.mock.calls.some(([url]) => String(url).endsWith("/runs"))).toBe(false);
});

it("persists advanced execution parameters, uses the chosen slot for coverage and reopens without preset replacement", async () => {
  const { records, fetchMock } = server(); const page = renderRoute("/workspace/experiment");
  await userEvent.selectOptions(await screen.findByLabelText("实验环境"), environment.environment_asset_id);
  fireEvent.pointerDown(screen.getByRole("button", { name: "选择节点 N0" }));
  await userEvent.selectOptions(screen.getByLabelText("绑定轨迹资源"), JSON.stringify(["chosen-track", 3, "N0"]));
  for (let i = 0; i < 2; i++) {
    await userEvent.click(screen.getByRole("button", { name: "＋ 添加节点" }));
    await userEvent.selectOptions(screen.getByLabelText("绑定轨迹资源"), JSON.stringify(["chosen-track", 3, "N0"]));
  }
  await userEvent.click(screen.getByRole("button", { name: "环境与实验参数" }));
  await userEvent.click(screen.getByRole("button", { name: "网络" }));
  await userEvent.selectOptions(screen.getByLabelText("业务负载"), "BearingFusion");
  expect(screen.getByText(/观测数不等于节点数/)).toBeTruthy();
  expect(screen.getByText(/不是说最小二乘数学上必然需要 5 个节点/)).toBeTruthy();
  const minimum = screen.getByLabelText("算法最少有效观测数");
  expect(minimum.getAttribute("min")).toBe("5");
  expect(minimum.closest("details")?.open).toBe(false);
  await userEvent.click(screen.getByText(/融合算法设置 · 当前至少/));
  fireEvent.change(minimum, { target: { value: "6" } });
  await userEvent.click(screen.getByText("结果对照条件 · 不控制仿真"));
  expect(screen.getByText(/不是 CPU 计算耗时、定时器或自动终止条件/)).toBeTruthy();
  fireEvent.change(screen.getByLabelText("报告收集与融合耗时参考（s）"), { target: { value: "45" } });
  await userEvent.click(screen.getByText("探测目标 · 合成观测的参考位置"));
  fireEvent.change(screen.getByLabelText("目标 X（m）"), { target: { value: "520" } });
  fireEvent.change(screen.getByLabelText("目标 Y（m）"), { target: { value: "-190" } });
  await userEvent.click(screen.getByText(/网络刷新策略 · 每/));
  fireEvent.change(screen.getByLabelText("重新规划间隔（周期）"), { target: { value: "3" } });
  await userEvent.click(screen.getByRole("button", { name: "物理模式" }));
  await userEvent.click(screen.getByText("通信设备参数 · 高级设置"));
  fireEvent.change(screen.getByLabelText("发送比特率（bit/s）"), { target: { value: "120" } });
  fireEvent.change(screen.getByLabelText("中心频率（Hz）"), { target: { value: "26000" } });
  fireEvent.change(screen.getByLabelText("占用带宽（Hz）"), { target: { value: "2400" } });
  fireEvent.change(screen.getByLabelText("声源级（dB re 1 μPa @ 1 m）"), { target: { value: "185" } });
  fireEvent.change(screen.getByLabelText(/等效接收噪声功率/), { target: { value: "58" } });
  await userEvent.click(screen.getByRole("button", { name: "运行" }));
  expect(screen.getByText("0～24 s")).toBeTruthy();
  await userEvent.click(screen.getByText("TDMA 时序 · 高级设置"));
  fireEvent.change(screen.getByLabelText("每个时隙（s）"), { target: { value: "40" } });
  expect(screen.getByText("0～160 s")).toBeTruthy();
  expect(screen.getByText("轨迹覆盖待检查 · 3 项")).toBeTruthy();
  fireEvent.change(screen.getByLabelText("每个时隙（s）"), { target: { value: "8" } });
  fireEvent.change(screen.getByLabelText("时隙保护时间（s）"), { target: { value: "1.2" } });
  expect(screen.getByText("0～32 s")).toBeTruthy();
  expect(screen.queryByText(/轨迹覆盖待检查/)).toBeNull();
  await userEvent.click(screen.getByRole("button", { name: "保存实验草稿" }));
  await waitFor(() => expect(records.get("experiment")?.payload.slot_duration_ns).toBe("8000000000"));
  expect(records.get("experiment")?.payload).toMatchObject({
    scenario_id: "scenario-exact", scenario_revision: 1, workload: "BearingFusion", minimum_bearing_points: "6",
    maximum_fusion_period_ns: "45000000000", network_update_interval_cycles: "3",
    bit_rate_bits_per_second: "120", center_frequency_hz: 26000, occupied_bandwidth_hz: 2400,
    source_level_db: 185, equivalent_noise_power_db_re_1upa2: 58, guard_interval_ns: "1200000000",
    slot_duration_ns: "8000000000", target_x_m: 520, target_y_m: -190,
  });
  expect(fetchMock.mock.calls.some(([url]) => String(url).endsWith("/runs"))).toBe(false);
  page.unmount(); renderRoute("/workspace/experiment?draft=experiment-exact");
  await waitFor(() => expect((screen.getByLabelText("使用已有节点场景") as HTMLSelectElement).value).toBe("scenario-exact"));
  expect((screen.getByLabelText("每个时隙（s）") as HTMLInputElement).value).toBe("8");
  expect((screen.getByLabelText("时隙保护时间（s）") as HTMLInputElement).value).toBe("1.2");
  expect((screen.getByLabelText(/等效接收噪声功率/) as HTMLInputElement).value).toBe("58");
  expect((screen.getByLabelText("目标 X（m）") as HTMLInputElement).value).toBe("520");
  expect((screen.getByLabelText("目标 Y（m）") as HTMLInputElement).value).toBe("-190");
  expect((screen.getByLabelText("报告收集与融合耗时参考（s）") as HTMLInputElement).value).toBe("45");
  expect(screen.getByText("0～32 s")).toBeTruthy();
});
