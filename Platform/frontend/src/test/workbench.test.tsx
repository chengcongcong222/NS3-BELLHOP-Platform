import { fireEvent, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { describe, expect, it } from "vitest";
import { environment, experiment, run, scenario } from "./fixtures";
import { installApi, renderRoute } from "./render";
import type { TrajectoryAssetDto } from "../api/types";

const catalog = {
  "/environments": { body: [environment] },
  "/scenarios": { body: [scenario] },
  "/experiments": { body: [experiment] },
  "/trajectories": { body: [] },
  "/authoring/drafts?kind=scenario": { body: [] },
  "/authoring/drafts?kind=experiment": { body: [] },
};

describe("simulation workbench user journeys", () => {
  it("goes from workbench to case, creates a real Run, and enters monitor", async () => {
    const launched = { ...run, run_id: "case-launched-run", lifecycle: "Running", event_stream_complete: false };
    const fetchMock = installApi({
      ...catalog,
      "/authoring/drafts": { body: [] },
      "/runs": { body: [] },
      "/results": { body: [] },
      "POST /runs": { body: launched },
      "/runs/case-launched-run": { body: launched },
    });
    renderRoute("/");
    expect(await screen.findByText("水声网络数字孪生仿真工作台")).toBeTruthy();
    expect(screen.queryByText("从资源到一次可复现实验")).toBeNull();
    expect(screen.queryByRole("link", { name: "新建向导" })).toBeNull();
    expect(screen.getByRole("link", { name: "＋ 新建实验" }).getAttribute("href")).toBe("/workspace/experiment");
    await userEvent.click(screen.getAllByRole("link", { name: "浅水四节点协同通信与融合" })[0]);
    expect(await screen.findByText("这个案例将如何执行")).toBeTruthy();
    expect(screen.queryByRole("link", { name: "新建自定义实验" })).toBeNull();
    expect(screen.getByRole("heading", { name: "场景与节点" })).toBeTruthy();
    expect(screen.getByRole("heading", { name: "绑定环境" })).toBeTruthy();
    expect(screen.getByRole("heading", { name: "通信与运行参数" })).toBeTruthy();
    expect(screen.queryByRole("button", { name: "基于此案例创建实验" })).toBeNull();
    await userEvent.click(screen.getByRole("button", { name: "运行默认实验" }));
    expect(await screen.findByText("场景初始几何 · 当前通信事件")).toBeTruthy();
    expect(document.body.textContent).toContain("case-launched-run");
    const post = fetchMock.mock.calls.find((call) => call[1]?.method === "POST");
    expect(JSON.parse(String(post?.[1]?.body))).toEqual({ experiment_id: experiment.experiment_id, experiment_version: experiment.version });
  });

  it("builds and persists an environment draft through the backend workspace", async () => {
    const serverDraft = { kind: "environment", draft_id: "environment-a", name: "南海试验环境", payload: { center_frequency_hz: 25000 }, revision: 1, created_at: "2026-01-01T00:00:00Z", updated_at: "2026-01-01T00:00:00Z", validation_issues: [] };
    const fetchMock = installApi({ ...catalog,
      "/authoring/drafts?kind=environment": { body: [] },
      "POST /authoring/acquisition/woa/stream": { stream: true, body: { provider: "reference cache", sound_speed_profile: [{ depth_meters: 0, speed_meters_per_second: 1500 }, { depth_meters: 100, speed_meters_per_second: 1490 }] } },
      "POST /authoring/acquisition/terrain": { body: { provider: "reference cache", bathymetry: [{ range_meters: 0, depth_meters: 100 }, { range_meters: 1000, depth_meters: 110 }] } },
      "POST /authoring/drafts": { body: serverDraft },
    });
    renderRoute("/workspace/environment");
    expect(await screen.findByRole("img", { name: "草稿声速剖面" })).toBeTruthy();
    expect(screen.queryByText("高级手工编辑")).toBeNull();
    expect(screen.getByLabelText("请求 SSP 深度上限（m）")).toBeTruthy();
    expect(screen.getByLabelText("请求剖面距离（m）")).toBeTruthy();
    const name = screen.getByLabelText("环境名称");
    await userEvent.clear(name); await userEvent.type(name, "南海试验环境");
    await userEvent.click(screen.getByRole("button", { name: "获取 WOA 数据" }));
    await userEvent.click(screen.getByRole("button", { name: "获取 GEBCO 地形" }));
    await userEvent.click(screen.getByRole("button", { name: "保存草稿" }));
    expect(await screen.findByText("草稿已保存到服务器工作区")).toBeTruthy();
    const write = fetchMock.mock.calls.find((call) => call[1]?.method === "POST" && String(call[0]) === "/authoring/drafts");
    expect(JSON.parse(String(write?.[1]?.body)).payload.sound_speed_profile).toHaveLength(2);
  });

  it("redirects legacy scene editing into the unified canvas and keeps exact coordinate input", async () => {
    installApi({ ...catalog, "/authoring/drafts?kind=scenario": { body: [] } });
    renderRoute("/workspace/scenario");
    expect(await screen.findByRole("img", { name: "可编辑场景画布" })).toBeTruthy();
    expect(screen.getByRole("heading", { name: "实验工作台" })).toBeTruthy();
    expect(screen.queryByRole("link", { name: "场景设计" })).toBeNull();
    const before = screen.getAllByRole("button", { name: /^选择节点 N/ }).length;
    await userEvent.click(screen.getByRole("button", { name: "＋ 添加节点" }));
    expect(screen.getAllByRole("button", { name: /^选择节点 N/ })).toHaveLength(before + 1);
    const x = screen.getByLabelText("X（m）"); await userEvent.clear(x); await userEvent.type(x, "321");
    expect((x as HTMLInputElement).value).toBe("321");
    await userEvent.click(screen.getByRole("button", { name: "深度" }));
    expect(screen.queryByLabelText("节点深度分布（X 坐标）")).toBeNull();
    await userEvent.click(screen.getByRole("button", { name: "深度" }));
    expect(screen.getByLabelText("节点深度分布（X 坐标）")).toBeTruthy();
    expect(screen.getByLabelText("绑定轨迹资源")).toBeTruthy();
  });

  it("renders the real unified workbench from the obsolete new-simulation link", async () => {
    installApi(catalog); renderRoute("/new-simulation");
    expect(await screen.findByRole("img", { name: "可编辑场景画布" })).toBeTruthy();
    expect(screen.getByRole("button", { name: "保存并运行" })).toBeTruthy();
    expect(screen.queryByText("从资源到一次可复现实验")).toBeNull();
  });

  it("keeps an old scenario draft link bound to that exact draft in the unified workbench", async () => {
    const node = { ...scenario.nodes[0], initial_position: { x_meters: 123, y_meters: 24, z_meters: -30 } };
    const savedScene = { kind: "scenario", draft_id: "legacy-scene", name: "已有节点布设", payload: {
      environment_asset_id: environment.environment_asset_id, fusion_center_node_id: node.node_id,
      nodes: [node], trajectory_bindings: [],
    }, revision: 4, validation_issues: [], created_at: "2026-09-17T00:00:00Z", updated_at: "2026-09-17T00:00:00Z" };
    const fetch = installApi({ ...catalog, "/authoring/drafts?kind=scenario": { body: [savedScene] } });
    renderRoute("/workspace/scenario?draft=legacy-scene");
    expect(await screen.findByDisplayValue("已有节点布设")).toBeTruthy();
    expect((screen.getByLabelText("使用已有节点场景") as HTMLSelectElement).value).toBe("legacy-scene");
    fireEvent.pointerDown(screen.getByRole("button", { name: `选择节点 N${node.node_id}` }));
    expect((screen.getByLabelText("X（m）") as HTMLInputElement).value).toBe("123");
    expect(fetch.mock.calls.some(([, init]) => init?.method === "POST" || init?.method === "PUT")).toBe(false);
  });

  it("persists Channel2 configuration and launches it through the authoring run API", async () => {
    const saved = { kind: "experiment", draft_id: "experiment-waveform", name: "未命名网络实验", payload: {}, revision: 1, created_at: "2026-01-01T00:00:00Z", updated_at: "2026-01-01T00:00:00Z", validation_issues: [] };
    const launched = { ...run, run_id: "channel2-run", lifecycle: "Created" };
    const nodes = [...scenario.nodes, ...["998", "999"].map(node_id => ({ ...scenario.nodes[0], node_id }))];
    const paths: TrajectoryAssetDto = { asset_id: "channel-config-tracks", version: 1, name: "配置回归轨迹", source: "test-fixture", source_notice: null, checksum_sha256: "test", coordinate_system: "LocalCartesianMetersZUp", tracks: nodes.map(node => ({ track_id: `N${node.node_id}`, points: [0, 120].map(time_s => ({ time_s, x_m: node.initial_position.x_meters, y_m: node.initial_position.y_meters, z_m: node.initial_position.z_meters })) })) };
    const savedScene = { ...saved, kind: "scenario", draft_id: "scene-a", name: "节点布设", payload: {
      environment_asset_id: environment.environment_asset_id, nodes, fusion_center_node_id: scenario.fusion_center_node_id,
      trajectory_bindings: nodes.map(node => ({ node_id: node.node_id, asset_id: paths.asset_id, version: paths.version, track_id: `N${node.node_id}` })),
    } };
    const fetchMock = installApi({ ...catalog,
      "/trajectories": { body: [paths] },
      "/authoring/drafts?kind=scenario": { body: [savedScene] },
      "PUT /authoring/drafts/scenario/scene-a": { body: { ...savedScene, revision: 2 } },
      "POST /authoring/drafts": { body: saved },
      "PUT /authoring/drafts/experiment/experiment-waveform": { body: { ...saved, revision: 2 } },
      "POST /authoring/experiments/experiment-waveform/runs": { body: launched },
      "/runs/channel2-run": { body: launched },
    });
    renderRoute("/workspace/experiment");
    expect(await screen.findByLabelText("物理层实现")).toBeTruthy();
    await userEvent.selectOptions(screen.getByLabelText("使用已有节点场景"), "scene-a");
    await userEvent.click(screen.getByRole("button", { name: "物理模式" }));
    expect(screen.getByRole("option", { name: "模式 A · 快速网络仿真" })).toBeTruthy();
    expect(screen.getByRole("option", { name: "模式 B · 波形通信仿真" })).toBeTruthy();
    expect(screen.queryByLabelText("通信速率（bit/s）")).toBeNull();
    await userEvent.click(screen.getByText("通信设备参数 · 高级设置"));
    expect(screen.getByText("发送比特率")).toBeTruthy();
    await userEvent.click(screen.getByRole("button", { name: "网络" }));
    await userEvent.selectOptions(screen.getByLabelText("业务负载"), "CommunicationOnly");
    expect(screen.queryByLabelText("算法最少有效观测数")).toBeNull();
    await userEvent.selectOptions(screen.getByLabelText("路由"), "ShortestPath");
    await userEvent.click(screen.getByRole("button", { name: "物理模式" }));
    await userEvent.selectOptions(screen.getByLabelText("物理层实现"), "Channel2Waveform");
    await userEvent.selectOptions(screen.getByLabelText("接收波形噪声"), "Awgn");
    fireEvent.change(screen.getByLabelText(/噪声功率谱密度/), { target: { value: "75" } });
    await userEvent.click(screen.getByRole("button", { name: "保存实验草稿" }));
    const write = fetchMock.mock.calls.find((call) => call[1]?.method === "POST" && String(call[0]) === "/authoring/drafts");
    const payload = JSON.parse(String(write?.[1]?.body)).payload;
    expect(payload.physical_layer).toBe("Channel2Waveform");
    expect(payload.waveform_noise_psd_db_re_1upa2_per_hz).toBe(75);
    expect(payload.workload).toBe("CommunicationOnly");
    expect(payload.routing_mode).toBe("ShortestPath");
    expect(payload.scenario_id).toBe("scene-a");
    expect(payload.scenario_revision).toBe(2);
    // Changing configuration after the explicit save must be saved again before launch.
    await userEvent.selectOptions(screen.getByLabelText("调制方式"), "Bfsk");
    await userEvent.click(await screen.findByRole("button", { name: "保存并运行" }));
    expect(await screen.findByText("场景初始几何 · 当前通信事件")).toBeTruthy();
    const update = fetchMock.mock.calls.find(call => call[1]?.method === "PUT" && String(call[0]).includes("/experiment/"));
    expect(JSON.parse(String(update?.[1]?.body)).payload.waveform_modulation).toBe("Bfsk");
    expect(fetchMock).toHaveBeenCalledWith("/authoring/experiments/experiment-waveform/runs", expect.objectContaining({ method: "POST", headers: { "If-Match": "2" } }));
  });

  it("reopens a server experiment draft with its exact Channel2 selection", async () => {
    const saved = {
      kind: "experiment", draft_id: "experiment-waveform", name: "波形链路实验",
      payload: {
        scenario_id: "scenario-draft-a", routing_mode: "ShortestPath", mac_mode: "Tdma",
        workload: "CommunicationOnly", bit_rate_bits_per_second: "60",
        simulation_cycle_count: "3", deterministic_seed: "7",
        physical_layer: "Channel2Waveform", waveform_modulation: "Bfsk",
        waveform_noise: "Awgn", waveform_noise_psd_db_re_1upa2_per_hz: 72,
      },
      revision: 2, created_at: "2026-01-01T00:00:00Z", updated_at: "2026-01-02T00:00:00Z",
      validation_issues: [],
    };
    installApi({ ...catalog,
      "/authoring/drafts?kind=scenario": { body: [] },
      "/authoring/drafts?kind=experiment": { body: [saved] },
    });
    renderRoute("/workspace/experiment?draft=experiment-waveform");
    expect(await screen.findByDisplayValue("波形链路实验")).toBeTruthy();
    expect((screen.getByLabelText("物理层实现") as HTMLSelectElement).value).toBe("Channel2Waveform");
    expect((screen.getByLabelText("业务负载") as HTMLSelectElement).value).toBe("CommunicationOnly");
    expect((screen.getByLabelText("接收波形噪声") as HTMLSelectElement).value).toBe("Awgn");
    expect((screen.getByLabelText(/噪声功率谱密度/) as HTMLInputElement).value).toBe("72");
  });
});
