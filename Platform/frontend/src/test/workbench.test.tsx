import { screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { describe, expect, it } from "vitest";
import { environment, experiment, run, scenario } from "./fixtures";
import { installApi, renderRoute } from "./render";

const catalog = {
  "/environments": { body: [environment] },
  "/scenarios": { body: [scenario] },
  "/experiments": { body: [experiment] },
  "/trajectories": { body: [] },
};

describe("simulation workbench user journeys", () => {
  it("goes from workbench to case, creates a real Run, and enters monitor", async () => {
    const launched = { ...run, run_id: "case-launched-run", lifecycle: "Running", event_stream_complete: false };
    const fetchMock = installApi({
      ...catalog,
      "/runs": { body: [] },
      "/results": { body: [] },
      "POST /runs": { body: launched },
      "/runs/case-launched-run": { body: launched },
    });
    renderRoute("/");
    expect(await screen.findByText("水声网络数字孪生仿真工作台")).toBeTruthy();
    await userEvent.click(screen.getAllByRole("link", { name: "浅水四节点协同通信与融合" })[0]);
    expect(await screen.findByText("这个案例将如何执行")).toBeTruthy();
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
      "POST /authoring/acquisition/woa": { body: { provider: "reference cache", sound_speed_profile: [{ depth_meters: 0, speed_meters_per_second: 1500 }, { depth_meters: 100, speed_meters_per_second: 1490 }] } },
      "POST /authoring/acquisition/terrain": { body: { provider: "reference cache", bathymetry: [{ range_meters: 0, depth_meters: 100 }, { range_meters: 1000, depth_meters: 110 }] } },
      "POST /authoring/drafts": { body: serverDraft },
    });
    renderRoute("/workspace/environment");
    expect(await screen.findByRole("img", { name: "草稿声速剖面" })).toBeTruthy();
    const name = screen.getByLabelText("环境名称");
    await userEvent.clear(name); await userEvent.type(name, "南海试验环境");
    await userEvent.click(screen.getByRole("button", { name: "获取 WOA 数据" }));
    await userEvent.click(screen.getByRole("button", { name: "获取 GEBCO 地形" }));
    await userEvent.click(screen.getByRole("button", { name: "保存草稿" }));
    expect(await screen.findByText("草稿已保存到服务器工作区")).toBeTruthy();
    const write = fetchMock.mock.calls.find((call) => call[1]?.method === "POST" && String(call[0]) === "/authoring/drafts");
    expect(JSON.parse(String(write?.[1]?.body)).payload.sound_speed_profile).toHaveLength(2);
  });

  it("supports N-node scene editing with canvas and exact coordinate input", async () => {
    installApi({ ...catalog, "/authoring/drafts?kind=scenario": { body: [] } });
    renderRoute("/workspace/scenario");
    expect(await screen.findByRole("img", { name: "可编辑场景画布" })).toBeTruthy();
    const before = scenario.nodes.length;
    await userEvent.click(screen.getByRole("button", { name: "＋ 添加节点" }));
    expect(screen.getAllByText(/^N[0-9]+$/).length).toBeGreaterThanOrEqual(before + 1);
    const x = screen.getByLabelText("X（m）"); await userEvent.clear(x); await userEvent.type(x, "321");
    expect((x as HTMLInputElement).value).toBe("321");
    expect(screen.getByLabelText("节点深度分布（X 坐标）")).toBeTruthy();
  });

  it("persists Channel2 configuration and launches it through the authoring run API", async () => {
    const saved = { kind: "experiment", draft_id: "experiment-waveform", name: "未命名网络实验", payload: {}, revision: 1, created_at: "2026-01-01T00:00:00Z", updated_at: "2026-01-01T00:00:00Z", validation_issues: [] };
    const launched = { ...run, run_id: "channel2-run", lifecycle: "Created" };
    const fetchMock = installApi({ ...catalog,
      "/authoring/drafts?kind=scenario": { body: [] },
      "POST /authoring/drafts": { body: saved },
      "POST /authoring/experiments/experiment-waveform/runs": { body: launched },
      "/runs/channel2-run": { body: launched },
    });
    renderRoute("/workspace/experiment");
    expect(await screen.findByText("通信与物理层")).toBeTruthy();
    expect((screen.getByLabelText("通信速率（bit/s）") as HTMLInputElement).disabled).toBe(true);
    await userEvent.selectOptions(screen.getByLabelText("业务负载"), "CommunicationOnly");
    expect(screen.queryByText("最小方位观测数")).toBeNull();
    await userEvent.selectOptions(screen.getByLabelText("路由"), "ShortestPath");
    await userEvent.selectOptions(screen.getByLabelText("物理层实现"), "Channel2Waveform");
    await userEvent.selectOptions(screen.getByLabelText("波形噪声"), "Awgn");
    await userEvent.clear(screen.getByLabelText("噪声 PSD（dB re 1 μPa²/Hz）"));
    await userEvent.type(screen.getByLabelText("噪声 PSD（dB re 1 μPa²/Hz）"), "75");
    await userEvent.click(screen.getByRole("button", { name: "保存实验草稿" }));
    const write = fetchMock.mock.calls.find((call) => call[1]?.method === "POST" && String(call[0]) === "/authoring/drafts");
    const payload = JSON.parse(String(write?.[1]?.body)).payload;
    expect(payload.physical_layer).toBe("Channel2Waveform");
    expect(payload.waveform_noise_psd_db_re_1upa2_per_hz).toBe(75);
    expect(payload.workload).toBe("CommunicationOnly");
    expect(payload.routing_mode).toBe("ShortestPath");
    await userEvent.click(await screen.findByRole("button", { name: "使用 Channel2 运行已保存草稿" }));
    expect(fetchMock).toHaveBeenCalledWith("/authoring/experiments/experiment-waveform/runs", expect.objectContaining({ method: "POST" }));
  });
});
