import { fireEvent, render, screen, waitFor, within } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { describe, expect, it, vi } from "vitest";
import type { AuthoringDraftDto, TrajectoryAssetDto } from "../api/types";
import { environment, experiment, run, scenario } from "./fixtures";
import { installApi, renderRoute } from "./render";
import { ScenarioDesigner } from "../components/ScenarioDesigner";
import { ScenarioTopology } from "../components/ScenarioTopology";
import { scenarioDraftFrom } from "../domain/workspace";

const nodes = [0, 1, 2].map(id => ({ ...scenario.nodes[0], node_id: String(id), can_transmit: true, can_receive: true }));
const trajectory: TrajectoryAssetDto = { asset_id: "planning-track", version: 1, name: "规划测试轨迹", source: "Csv", coordinate_system: "LocalCartesianMetersZUp", checksum_sha256: "test", source_notice: null,
  tracks: [{ track_id: "track", points: [{ time_s: 0, x_m: 0, y_m: 0, z_m: -20 }, { time_s: 120, x_m: 1, y_m: 0, z_m: -20 }] }] };
const scene: AuthoringDraftDto = { kind: "scenario", draft_id: "planning-scene", name: "无固定中心场景", revision: 1, created_at: "2026-10-08", updated_at: "2026-10-08", validation_issues: [],
  payload: { nodes, fusion_center_node_id: null, environment_asset_id: environment.environment_asset_id, trajectory_bindings: nodes.map(node => ({ node_id: node.node_id, asset_id: trajectory.asset_id, version: 1, track_id: "track" })) } };

function server(saved?: AuthoringDraftDto, sourceScene = scene, otherScenes: AuthoringDraftDto[] = [], validationIssues: Partial<Record<"scenario" | "experiment", string[]>> = {}) {
  const records = new Map<string, AuthoringDraftDto>([["scenario", structuredClone(sourceScene)]]);
  if (saved) records.set("experiment", saved);
  const fetch = installApi({ "/environments": { body: [environment] }, "/scenarios": { body: [scenario] }, "/experiments": { body: [experiment] }, "/trajectories": { body: [trajectory] }, "/authoring/capabilities": { body: {} } });
  const base = fetch.getMockImplementation()!;
  fetch.mockImplementation(async (input, init) => {
    const url = String(input);
    if (url.startsWith("/authoring/drafts?kind=")) return new Response(JSON.stringify([...records.values(), ...otherScenes].filter(item => item.kind === url.split("=")[1])));
    if (init?.method === "POST" && url === "/authoring/drafts" || init?.method === "PUT") {
      const data = JSON.parse(String(init!.body));
      const kind = data.kind ?? (url.includes("scenario") ? "scenario" : "experiment");
      const old = records.get(kind);
      const draft = { ...data, kind, draft_id: old?.draft_id ?? "planning-experiment", revision: (old?.revision ?? 0) + 1, validation_issues: validationIssues[kind as "scenario" | "experiment"] ?? [], created_at: "2026-10-08", updated_at: "2026-10-08" };
      records.set(kind, draft);
      return new Response(JSON.stringify(draft));
    }
    if (url === "/authoring/experiments/planning-experiment/runs") return new Response(JSON.stringify(run));
    return base(input, init);
  });
  return { records, fetch };
}

async function openNew() {
  const page = renderRoute("/workspace/experiment?scenario=planning-scene");
  await screen.findByDisplayValue("无固定中心场景");
  await userEvent.click(screen.getByRole("button", { name: "网络" }));
  return page;
}

async function addFlow(source: string, destination: string, index: number) {
  await userEvent.click(screen.getByRole("button", { name: "添加单向通信流" }));
  await userEvent.selectOptions(screen.getByLabelText(`源节点 ${index}`), source);
  await userEvent.selectOptions(screen.getByLabelText(`目的节点 ${index}`), destination);
}

describe("PlanningV1 experiment authoring", () => {
  it("saves and launches a two-node planning experiment through the actual authoring API path", async () => {
    const twoNodes = { ...scene, payload: { ...scene.payload, nodes: nodes.slice(0, 2), trajectory_bindings: (scene.payload.trajectory_bindings as Array<{ node_id: string }>).slice(0, 2) } };
    const { records, fetch } = server(undefined, twoNodes); await openNew();
    expect(screen.queryByText(/支持 3～6 个节点/)).toBeNull();
    await userEvent.selectOptions(screen.getByLabelText("业务负载"), "CommunicationOnly");
    await addFlow("0", "1", 1); await addFlow("1", "0", 2);
    await userEvent.click(screen.getByRole("button", { name: "保存并运行" }));
    await waitFor(() => expect(fetch.mock.calls.some(([url]) => String(url) === "/authoring/experiments/planning-experiment/runs")).toBe(true));
    expect(records.get("scenario")!.payload.nodes).toHaveLength(2);
    expect(records.get("experiment")!.payload).toMatchObject({ execution_mode: "PlanningV1", flows: [{ source_node_id: "0", destination_node_id: "1", packets_per_cycle: 1 }, { source_node_id: "1", destination_node_id: "0", packets_per_cycle: 1 }] });
  });

  it("clears business endpoints only on an explicit scene switch, including switching to a new scene", async () => {
    const nativeConfirm = vi.spyOn(window, "confirm");
    const alternate = { ...scene, draft_id: "other-scene", name: "同编号的另一场景" };
    server(undefined, scene, [alternate]); await openNew();
    await userEvent.selectOptions(screen.getByLabelText("当前算法的固定处理节点"), "0");
    await userEvent.selectOptions(screen.getByLabelText("业务负载"), "CommunicationOnly");
    await addFlow("0", "1", 1);
    await userEvent.selectOptions(screen.getByLabelText("使用已有节点场景"), "other-scene");
    expect(screen.getByRole("alertdialog", { name: "切换节点场景？" })).toBeTruthy();
    expect(nativeConfirm).not.toHaveBeenCalled();
    await userEvent.click(screen.getByRole("button", { name: "保留当前场景" }));
    expect(screen.getByLabelText("源节点 1")).toBeTruthy();
    await userEvent.selectOptions(screen.getByLabelText("使用已有节点场景"), "other-scene");
    await userEvent.click(screen.getByRole("button", { name: "确认切换" }));
    await screen.findByDisplayValue("同编号的另一场景");
    expect(screen.queryByLabelText("源节点 1")).toBeNull();
    expect(screen.getByText(/通信流和应用接收点已清空/)).toBeTruthy();
    await userEvent.selectOptions(screen.getByLabelText("业务负载"), "BearingFusion");
    expect((screen.getByLabelText("当前算法的固定处理节点") as HTMLSelectElement).value).toBe("");
    await userEvent.selectOptions(screen.getByLabelText("当前算法的固定处理节点"), "1");
    expect(screen.queryByText(/通信流和应用接收点已清空/)).toBeNull();
    await userEvent.selectOptions(screen.getByLabelText("使用已有节点场景"), "");
    await userEvent.click(screen.getByRole("button", { name: "确认切换" }));
    expect((screen.getByLabelText("当前算法的固定处理节点") as HTMLSelectElement).value).toBe("");
    expect((screen.getByLabelText("场景名称") as HTMLInputElement).value).toBe("未命名节点场景");
  });

  it("opens a new editor on same-mounted navigation from a saved draft and never updates the old draft", async () => {
    const old: AuthoringDraftDto = { ...scene, kind: "experiment", draft_id: "legacy-experiment", name: "旧实验不应被覆盖", payload: { scenario_id: scene.draft_id, routing_mode: "DirectToFusionCenter", mac_mode: "Tdma", workload: "BearingFusion" } };
    const { fetch } = server(old); renderRoute("/workspace/experiment?draft=legacy-experiment");
    await screen.findByDisplayValue("旧实验不应被覆盖");
    await userEvent.click(screen.getByRole("link", { name: /实验工作台/ }));
    await screen.findByDisplayValue("未命名网络实验");
    expect((screen.getByLabelText("使用已有节点场景") as HTMLSelectElement).value).toBe("");
    expect((screen.getByLabelText("MAC") as HTMLSelectElement).value).toBe("AdaptiveTdma");
    await userEvent.click(screen.getByRole("button", { name: "保存实验草稿" }));
    await waitFor(() => expect(fetch.mock.calls.filter(([url, init]) => String(url) === "/authoring/drafts" && init?.method === "POST")).toHaveLength(2));
    expect(fetch.mock.calls.some(([, init]) => init?.method === "PUT")).toBe(false);
  });

  it("keeps an unknown execution mode visible and refuses to run it as a legacy experiment", async () => {
    const unsupported: AuthoringDraftDto = { ...scene, kind: "experiment", draft_id: "future-experiment", payload: { scenario_id: scene.draft_id, execution_mode: "FuturePlanning", routing_mode: "DirectToFusionCenter", mac_mode: "Tdma", workload: "BearingFusion" } };
    const { records, fetch } = server(unsupported); renderRoute("/workspace/experiment?draft=future-experiment");
    expect((await screen.findByRole("alert")).textContent).toContain("不能按历史模式运行");
    await userEvent.click(screen.getByRole("button", { name: "保存并运行" }));
    await waitFor(() => expect(records.get("experiment")?.revision).toBe(2));
    expect(records.get("experiment")!.payload.execution_mode).toBe("FuturePlanning");
    expect(fetch.mock.calls.some(([url]) => String(url).endsWith("/runs"))).toBe(false);
  });

  it("does not assign another node when a legacy application receiver is deleted", async () => {
    const onChange = vi.fn();
    render(<ScenarioDesigner draft={scenarioDraftFrom({ ...scenario, nodes, fusion_center_node_id: "0" })} onChange={onChange} />);
    await userEvent.click(screen.getByRole("button", { name: "删除节点" }));
    expect(onChange).toHaveBeenCalledWith(expect.objectContaining({ fusionCenterNodeId: null, nodes: nodes.slice(1) }));
  });

  it("does not label a centre when a scene has no application receiver", () => {
    render(<ScenarioTopology scenario={{ ...scenario, fusion_center_node_id: null }} />);
    expect(screen.queryByText("案例应用接收节点")).toBeNull();
    expect(screen.queryByText("固定融合中心")).toBeNull();
  });

  it("round-trips explicit bidirectional and multiple-destination flows without a centre or manual slots", async () => {
    const { records } = server(); const page = await openNew();
    expect((screen.getByLabelText("MAC") as HTMLSelectElement).value).toBe("AdaptiveTdma");
    expect(screen.queryByLabelText("每个时隙（s）")).toBeNull();
    expect(screen.queryByLabelText("时隙保护时间（s）")).toBeNull();
    await userEvent.selectOptions(screen.getByLabelText("业务负载"), "CommunicationOnly");
    expect(screen.queryByLabelText("当前算法的固定处理节点")).toBeNull();
    await addFlow("0", "1", 1); await addFlow("1", "0", 2); await addFlow("0", "2", 3);
    fireEvent.change(screen.getByLabelText("每周期包数 3"), { target: { value: "2" } });
    await userEvent.click(screen.getByRole("button", { name: "保存实验草稿" }));
    await waitFor(() => expect(records.get("experiment")?.revision).toBe(1));
    const payload = records.get("experiment")!.payload;
    expect(payload).toMatchObject({ execution_mode: "PlanningV1", routing_mode: "Direct", mac_mode: "AdaptiveTdma", planning_cycle_duration_ns: "30000000000", adaptive_tdma: { safety_margin_ns: "0" }, flows: [
      { source_node_id: "0", destination_node_id: "1", packets_per_cycle: 1 }, { source_node_id: "1", destination_node_id: "0", packets_per_cycle: 1 }, { source_node_id: "0", destination_node_id: "2", packets_per_cycle: 2 },
    ] });
    for (const field of ["slot_duration_ns", "guard_interval_ns", "fusion_receiver_node_id", "target_x_m", "minimum_bearing_points", "network_update_interval_cycles"]) expect(payload).not.toHaveProperty(field);
    expect(records.get("scenario")!.payload.fusion_center_node_id).toBeNull();
    page.unmount(); renderRoute("/workspace/experiment?draft=planning-experiment");
    await waitFor(() => expect((screen.getByLabelText("源节点 1") as HTMLSelectElement).value).toBe("0"));
    expect((screen.getByLabelText("目的节点 2") as HTMLSelectElement).value).toBe("0");
    expect((screen.getByLabelText("每周期包数 3") as HTMLInputElement).value).toBe("2");
    expect(screen.getByText("0～60 s")).toBeTruthy();
  });

  it("removes inactive algorithm and workload parameters when switching a saved draft", async () => {
    const { records } = server(); const page = await openNew();
    await userEvent.selectOptions(screen.getByLabelText("业务负载"), "CommunicationOnly");
    await addFlow("0", "1", 1);
    await userEvent.selectOptions(screen.getByLabelText("MAC"), "Tdma");
    expect(screen.queryByLabelText("时隙安全余量（s）")).toBeNull();
    fireEvent.change(screen.getByLabelText("每个时隙（s）"), { target: { value: "4" } });
    await userEvent.click(screen.getByRole("button", { name: "保存实验草稿" }));
    await waitFor(() => expect(records.get("experiment")?.revision).toBe(1));
    expect(records.get("experiment")!.payload).toMatchObject({ slot_duration_ns: "4000000000", guard_interval_ns: "2000000000" });
    expect(records.get("experiment")!.payload).not.toHaveProperty("adaptive_tdma");
    await userEvent.selectOptions(screen.getByLabelText("MAC"), "AdaptiveTdma");
    fireEvent.change(screen.getByLabelText("时隙安全余量（s）"), { target: { value: "0.25" } });
    await userEvent.selectOptions(screen.getByLabelText("业务负载"), "BearingFusion");
    await userEvent.selectOptions(screen.getByLabelText("当前算法的固定处理节点"), "0");
    await userEvent.click(screen.getByRole("button", { name: "保存实验草稿" }));
    await waitFor(() => expect(records.get("experiment")?.revision).toBe(2));
    expect(records.get("experiment")!.payload).toMatchObject({ fusion_receiver_node_id: "0", adaptive_tdma: { safety_margin_ns: "250000000" } });
    for (const field of ["flows", "slot_duration_ns", "guard_interval_ns"]) expect(records.get("experiment")!.payload).not.toHaveProperty(field);
    expect(records.get("scenario")!.payload.fusion_center_node_id).toBeNull();
    page.unmount(); renderRoute("/workspace/experiment?draft=planning-experiment");
    await waitFor(() => expect((screen.getByLabelText("当前算法的固定处理节点") as HTMLSelectElement).value).toBe("0"));
    expect(screen.queryByText(/通信流和应用接收点已清空/)).toBeNull();
  });

  it("saves incomplete communication drafts but never launches implicit traffic", async () => {
    const { fetch, records } = server(); await openNew();
    await userEvent.selectOptions(screen.getByLabelText("业务负载"), "CommunicationOnly");
    expect(screen.getByText("请先添加通信流。")).toBeTruthy();
    await userEvent.click(screen.getByRole("button", { name: "保存并运行" }));
    expect((await screen.findByRole("alert")).textContent).toContain("至少需要一条显式通信流");
    expect(records.get("experiment")!.payload.flows).toEqual([]);
    expect(fetch.mock.calls.some(([url]) => String(url).endsWith("/runs"))).toBe(false);
  });

  it("shows both server validation lists after a successful draft save and keeps stale checks explicit while editing", async () => {
    const issues = { scenario: ["每个节点必须且只能绑定一条正式轨迹"], experiment: ["普通通信必须配置至少一条显式业务流"] };
    const { fetch, records } = server(undefined, scene, [], issues); await openNew();
    await userEvent.selectOptions(screen.getByLabelText("业务负载"), "CommunicationOnly");
    await userEvent.click(screen.getByRole("button", { name: "保存实验草稿" }));
    const checks = await screen.findByRole("region", { name: "保存校验" });
    expect(within(checks).getByText("节点场景：每个节点必须且只能绑定一条正式轨迹")).toBeTruthy();
    expect(within(checks).getByText("实验配置：普通通信必须配置至少一条显式业务流")).toBeTruthy();
    expect(within(checks).getByText(/暂不能运行/)).toBeTruthy();
    expect(within(checks).getByText(/在“网络”中配置通信流或应用接收节点/)).toBeTruthy();
    expect(screen.getByText(/已保存 · 修订 1 · 尚有待完善项/)).toBeTruthy();
    expect(fetch.mock.calls.some(([url]) => String(url).endsWith("/runs"))).toBe(false);
    await addFlow("0", "1", 1);
    expect(within(checks).getByText(/以上为上次保存的检查结果/)).toBeTruthy();
    issues.scenario = []; issues.experiment = [];
    await userEvent.click(screen.getByRole("button", { name: "保存实验草稿" }));
    await waitFor(() => expect(records.get("experiment")?.revision).toBe(2));
    await waitFor(() => expect(screen.queryByRole("region", { name: "保存校验" })).toBeNull());
    expect(screen.getByText(/保存草稿不代表已通过运行检查/)).toBeTruthy();
  });

  it("shows saved experiment receiver issues on reload without requiring a run attempt", async () => {
    const saved: AuthoringDraftDto = { ...scene, kind: "experiment", draft_id: "receiver-missing", validation_issues: ["融合接收点无效：必须使用规范的十进制整数字符串"], payload: { scenario_id: scene.draft_id, execution_mode: "PlanningV1", workload: "BearingFusion", fusion_receiver_node_id: null } };
    const { fetch } = server(saved); renderRoute("/workspace/experiment?draft=receiver-missing");
    const checks = await screen.findByRole("region", { name: "保存校验" });
    expect(within(checks).getByText(/实验配置：融合接收点无效/)).toBeTruthy();
    expect(fetch.mock.calls.some(([, init]) => init?.method === "POST" || init?.method === "PUT")).toBe(false);
  });

  it("preserves the execution semantics of an old draft with no execution_mode", async () => {
    const old: AuthoringDraftDto = { ...scene, kind: "experiment", draft_id: "legacy-experiment", payload: { scenario_id: scene.draft_id, routing_mode: "DirectToFusionCenter", mac_mode: "Tdma", workload: "CommunicationOnly", slot_duration_ns: "7000000000", guard_interval_ns: "1000000000" } };
    const { records } = server(old); renderRoute("/workspace/experiment?draft=legacy-experiment");
    await screen.findByDisplayValue("7");
    expect(screen.queryByLabelText("业务周期长度（s）")).toBeNull();
    expect(screen.queryByRole("option", { name: /自适应 TDMA/ })).toBeNull();
    await userEvent.click(screen.getByRole("button", { name: "保存实验草稿" }));
    await waitFor(() => expect(records.get("experiment")?.revision).toBe(2));
    expect(records.get("experiment")!.payload).not.toHaveProperty("execution_mode");
    expect(records.get("experiment")!.payload).toMatchObject({ slot_duration_ns: "7000000000", guard_interval_ns: "1000000000" });
  });
});
