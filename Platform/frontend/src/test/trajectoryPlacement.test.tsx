import { useState } from "react";
import { act, fireEvent, render, screen, waitFor } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { describe, expect, it, vi } from "vitest";
import { ScenarioDesigner } from "../components/ScenarioDesigner";
import { createRecordedSceneView } from "../domain/worldViewTransform";
import type { TrajectoryAssetDto } from "../api/types";
import type { ScenarioDraft } from "../domain/workspace";
import { scenario } from "./fixtures";
import { installApi } from "./render";

const source: TrajectoryAssetDto = {
  asset_id: "survey-original", version: 3, name: "巡测路径", source: "test-fixture",
  source_notice: null, coordinate_system: "LocalCartesianMetersZUp", checksum_sha256: "original-checksum",
  tracks: [{ track_id: "route", points: [
    { time_s: 0, x_m: 10, y_m: 20, z_m: -30, pitch_rad: .1, yaw_rad: .2, u_mps: 1 },
    { time_s: 120, x_m: 130, y_m: 20, z_m: -30, pitch_rad: .1, yaw_rad: .2, u_mps: 1 },
  ] }],
};
const derived: TrajectoryAssetDto = {
  ...source, asset_id: "survey-relocated", version: 1, name: "巡测路径 · N99 布设",
  source: "derived-from:survey-original:3:route", checksum_sha256: "relocated-checksum",
  tracks: [{ track_id: "route", points: source.tracks[0].points.map(point => ({ ...point,
    x_m: point.x_m + 240, y_m: point.y_m - 90, z_m: point.z_m - 15,
  })) }],
};
const initial: ScenarioDraft = {
  kind: "scenario", id: "scene", name: "布设测试", environmentAssetId: "environment", fusionCenterNodeId: "99",
  provenance: { kind: "new", label: "新建" }, updatedAt: "2026-09-17T00:00:00Z",
  nodes: [{ ...scenario.nodes[0], node_id: "99", initial_position: { x_meters: 10, y_meters: 20, z_meters: -30 } }],
  trajectoryBindings: [{ node_id: "99", asset_id: source.asset_id, version: source.version, track_id: "route" }],
};

function Harness({ updated = vi.fn(), created = vi.fn(), start = initial }: { updated?: (value: ScenarioDraft) => void; created?: () => void; start?: ScenarioDraft }) {
  const [draft, setDraft] = useState(start);
  return <><ScenarioDesigner draft={draft} trajectories={[source]} onChange={value => { updated(value); setDraft(value); }} onTrajectoryCreated={created} /><output data-testid="saved-binding">{JSON.stringify(draft)}</output></>;
}

async function enterPosition() {
  await userEvent.click(screen.getByRole("button", { name: "调整轨迹起点" }));
  fireEvent.change(screen.getByLabelText("新起点 X（m）"), { target: { value: "250" } });
  fireEvent.change(screen.getByLabelText("新起点 Y（m）"), { target: { value: "-70" } });
  fireEvent.change(screen.getByLabelText("新起点水深（m）"), { target: { value: "45" } });
}

describe("explicit immutable trajectory placement", () => {
  it("binds a reusable path relative to the scene start and keeps coordinates directly editable", async () => {
    const fetch = installApi({}), before = JSON.stringify(source);
    const start: ScenarioDraft = { ...initial, trajectoryBindings: [], nodes: [{ ...initial.nodes[0],
      initial_position: { x_meters: 123, y_meters: 456, z_meters: -45 } }] };
    render(<Harness start={start} />);
    await userEvent.selectOptions(screen.getByLabelText("绑定轨迹资源"), JSON.stringify([source.asset_id, source.version, "route"]));
    const bound = JSON.parse(screen.getByTestId("saved-binding").textContent!);
    expect(bound.nodes[0].initial_position).toEqual(start.nodes[0].initial_position);
    expect(bound.trajectoryBindings).toEqual([{ node_id: "99", asset_id: source.asset_id, version: source.version,
      track_id: "route", placement_mode: "RelativeToNodeStart" }]);
    expect((screen.getByLabelText("X（m）") as HTMLInputElement).disabled).toBe(false);
    expect((screen.getByLabelText("Y（m）") as HTMLInputElement).disabled).toBe(false);
    expect((screen.getByLabelText("深度（m）") as HTMLInputElement).disabled).toBe(false);
    fireEvent.change(screen.getByLabelText("X（m）"), { target: { value: "777" } });
    fireEvent.change(screen.getByLabelText("Y（m）"), { target: { value: "-12" } });
    fireEvent.change(screen.getByLabelText("深度（m）"), { target: { value: "25" } });
    const changed = JSON.parse(screen.getByTestId("saved-binding").textContent!);
    expect(changed.nodes[0].initial_position).toEqual({ x_meters: 777, y_meters: -12, z_meters: -25 });
    expect(changed.trajectoryBindings).toEqual(bound.trajectoryBindings);
    expect(JSON.stringify(source)).toBe(before);
    expect(fetch).not.toHaveBeenCalled();
  });

  it("drags a relative-bound node without changing the original trajectory or its time coverage", () => {
    let resize: ResizeObserverCallback | undefined;
    vi.stubGlobal("ResizeObserver", class {
      constructor(callback: ResizeObserverCallback) { resize = callback; }
      observe() {}
      disconnect() {}
    });
    const fetch = installApi({}), before = JSON.stringify(source);
    vi.stubGlobal("PointerEvent", MouseEvent);
    vi.stubGlobal("DOMPoint", class {
      constructor(public x: number, public y: number) {}
      matrixTransform() { return this; }
    });
    const start: ScenarioDraft = { ...initial, trajectoryBindings: [{ ...initial.trajectoryBindings![0], placement_mode: "RelativeToNodeStart" }] };
    render(<Harness start={start} />);
    const canvas = screen.getByRole("img", { name: "可编辑场景画布" });
    act(() => resize!([{ contentRect: { width: 1100, height: 480 } } as ResizeObserverEntry], {} as ResizeObserver));
    Object.defineProperty(canvas, "getScreenCTM", { value: () => ({ inverse: () => ({}) }), configurable: true });
    const [,, width, height] = canvas.getAttribute("viewBox")!.split(" ").map(Number);
    const view = createRecordedSceneView(start.nodes.map(n => n.initial_position), { width, height, padding: 50 });
    const target = view.point({ x_meters: -5, y_meters: -10 });
    fireEvent.pointerDown(screen.getByRole("button", { name: "选择节点 N99" }), { buttons: 1, pointerId: 1 });
    fireEvent.pointerMove(canvas, { buttons: 1, clientX: target.x, clientY: target.y, pointerId: 1 });
    fireEvent.pointerUp(canvas, { buttons: 0, pointerId: 1 });
    const changed = JSON.parse(screen.getByTestId("saved-binding").textContent!);
    expect(changed.nodes[0].initial_position).toEqual({ x_meters: -5, y_meters: -10, z_meters: -30 });
    expect(changed.trajectoryBindings).toEqual(start.trajectoryBindings);
    expect(JSON.stringify(source)).toBe(before);
    expect(source.tracks[0].points.map(point => point.time_s)).toEqual([0, 120]);
    expect(fetch).not.toHaveBeenCalled();
  });

  it("saves a derived trajectory through the API before changing binding or initial position", async () => {
    const fetch = installApi({ "POST /authoring/trajectories/translate": { body: derived } });
    const created = vi.fn(), updated = vi.fn(), before = JSON.stringify(source);
    render(<Harness created={created} updated={updated} />);
    expect((screen.getByLabelText("X（m）") as HTMLInputElement).disabled).toBe(true);
    await enterPosition();
    expect(updated).not.toHaveBeenCalled();
    await userEvent.click(screen.getByRole("button", { name: "另存新轨迹并绑定" }));
    await waitFor(() => expect(created).toHaveBeenCalledOnce());
    const post = fetch.mock.calls.find(([, init]) => init?.method === "POST")!;
    expect(post[0]).toBe("/authoring/trajectories/translate");
    const payload = JSON.parse(String(post[1]?.body));
    expect(payload).toMatchObject({ source_asset_id: "survey-original", source_version: 3, source_track_id: "route", x_m: 250, y_m: -70, z_m: -45 });
    expect(payload.asset_id).toMatch(/^placement-/);
    expect(payload).not.toHaveProperty("duration_s");
    expect(payload).not.toHaveProperty("dt_s");
    const changed = JSON.parse(screen.getByTestId("saved-binding").textContent!);
    expect(changed.trajectoryBindings).toEqual([{ node_id: "99", asset_id: "survey-relocated", version: 1, track_id: "route" }]);
    expect(changed.nodes[0].initial_position).toEqual({ x_meters: 250, y_meters: -70, z_meters: -45 });
    expect(JSON.stringify(source)).toBe(before);
    expect((screen.getByLabelText("绑定轨迹资源") as HTMLSelectElement).value).toBe(JSON.stringify(["survey-relocated", 1, "route"]));
    expect(derived.tracks[0].points.map(p => p.time_s)).toEqual(source.tracks[0].points.map(p => p.time_s));
  });

  it("preserves original binding and coordinates on a failed derived save", async () => {
    installApi({ "POST /authoring/trajectories/translate": { status: 422, body: { error: { code: "InvalidTrajectory", message: "平移请求校验失败" } } } });
    const created = vi.fn(), updated = vi.fn(); render(<Harness created={created} updated={updated} />);
    await enterPosition();
    await userEvent.click(screen.getByRole("button", { name: "另存新轨迹并绑定" }));
    expect((await screen.findByRole("alert")).textContent).toContain("平移请求校验失败");
    expect(updated).not.toHaveBeenCalled(); expect(created).not.toHaveBeenCalled();
    expect(JSON.parse(screen.getByTestId("saved-binding").textContent!)).toEqual(initial);
  });

  it("rejects negative depth without invoking the API or altering the source", async () => {
    const fetch = installApi({}); const updated = vi.fn(); render(<Harness updated={updated} />);
    await enterPosition(); fireEvent.change(screen.getByLabelText("新起点水深（m）"), { target: { value: "-1" } });
    await userEvent.click(screen.getByRole("button", { name: "另存新轨迹并绑定" }));
    expect((await screen.findByRole("alert")).textContent).toContain("水深不能为负数");
    expect(fetch).not.toHaveBeenCalled(); expect(updated).not.toHaveBeenCalled();
  });

  it("does not overwrite a binding the user changed while the save was in flight", async () => {
    const fetch = installApi({});
    let complete: ((value: Response) => void) | undefined;
    fetch.mockImplementation(async () => new Promise(resolve => { complete = resolve; }));
    render(<Harness />); await enterPosition();
    await userEvent.click(screen.getByRole("button", { name: "另存新轨迹并绑定" }));
    await waitFor(() => expect(complete).toBeTypeOf("function"));
    await userEvent.selectOptions(screen.getByLabelText("绑定轨迹资源"), "");
    complete!(new Response(JSON.stringify(derived), { headers: { "Content-Type": "application/json" } }));
    expect((await screen.findByRole("alert")).textContent).toContain("未覆盖当前设置");
    const changed = JSON.parse(screen.getByTestId("saved-binding").textContent!);
    expect(changed.trajectoryBindings).toEqual([]);
    expect(changed.nodes[0].initial_position).toEqual(initial.nodes[0].initial_position);
  });
});
