import { act, fireEvent, render, screen, within } from "@testing-library/react";
import { describe, expect, it, vi } from "vitest";
import { SceneDepthProfile, environmentSampleProfile, type SceneDepthNode } from "../components/SceneDepthProfile";
import { UuvNodeGlyph } from "../components/UuvNodeGlyph";
import { ScenarioDesigner } from "../components/ScenarioDesigner";
import type { ScenarioDraft } from "../domain/workspace";
import { scenario } from "./fixtures";
import { createBoundedSceneView } from "../domain/worldViewTransform";

const nodes: SceneDepthNode[] = [
  { node_id: "1", position: { x_meters: 100, y_meters: 20, z_meters: -30 } },
  { node_id: "2", position: { x_meters: 400, y_meters: 420, z_meters: -80 } },
];
const environment = { available: true, name: "测线南区", payload: {
  bathymetry: [{ range_meters: 0, depth_meters: 90 }, { range_meters: 600, depth_meters: 120 }],
  acquisition: { latitudeDeg: 18, longitudeDeg: 110, bearingDeg: 90, useReferenceCache: false },
} };

describe("paired scene depth view", () => {
  it("resizes the editor in display pixels, keeping plan/section X aligned without changing saved nodes", () => {
    let resize: ResizeObserverCallback | undefined;
    vi.stubGlobal("ResizeObserver", class {
      constructor(callback: ResizeObserverCallback) { resize = callback; }
      observe() {}
      disconnect() {}
    });
    const draft: ScenarioDraft = { kind: "scenario", id: "resizable-scene", name: "可调布局", environmentAssetId: "env",
      provenance: { kind: "new", label: "新建" }, updatedAt: "2026-09-18T00:00:00Z", fusionCenterNodeId: "1",
      nodes: nodes.map(node => ({ ...scenario.nodes[0], node_id: node.node_id, initial_position: node.position })),
      spatialDomain: { model: "ProfileExtrusion", x_min_m: 0, x_max_m: 600, y_min_m: -100, y_max_m: 500, surface_clearance_m: 1, bottom_clearance_m: 5 } };
    const change = vi.fn();
    render(<ScenarioDesigner draft={draft} onChange={change} globalInspector={<p>参数</p>}
      workspaceControls={<p>实验操作</p>} environmentInputs={environment} />);
    for (const [width, height] of [[1380, 780], [740, 480]]) {
      act(() => resize!([{ contentRect: { width, height } } as ResizeObserverEntry], {} as ResizeObserver));
      expect(screen.getByRole("img", { name: "可编辑场景画布" }).getAttribute("viewBox")).toBe(`0 0 ${width} ${height}`);
      const section = screen.getByRole("img", { name: "节点深度分布（X 坐标）" });
      expect(section.getAttribute("viewBox")).toBe(`0 0 ${width} 190`);
      const expected = createBoundedSceneView(draft.spatialDomain!, { width, height, padding: 50 });
      const planX = Number(screen.getByRole("button", { name: "选择节点 N1" }).getAttribute("transform")!.match(/translate\(([^ ]+)/)![1]);
      const depthX = Number(section.querySelector('[data-node-id="1"]')!.getAttribute("transform")!.match(/translate\(([^,]+)/)![1]);
      expect(planX).toBeCloseTo(expected.point(nodes[0].position).x, 10);
      expect(depthX).toBeCloseTo(planX, 10);
      expect(screen.getByRole("button", { name: "选择节点 N1" }).querySelector('.uuv-node-glyph')?.getAttribute('transform')).toBe('rotate(0) scale(1)');
    }
    expect(change).not.toHaveBeenCalled();
  });

  it("keeps shared X pixels while removing simplified terrain", () => {
    const domain = { model: "ProfileExtrusion" as const, x_min_m: 0, x_max_m: 600,
      y_min_m: -100, y_max_m: 500, surface_clearance_m: 1, bottom_clearance_m: 5 };
    const view = createBoundedSceneView(domain, { width: 980, height: 320, padding: 45 });
    render(<SceneDepthProfile nodes={nodes} selectedNodeIds={["1", "2"]} environmentInputs={environment}
      spatialDomain={domain} xDomain={view.xDomain} plotWidth={980} plotPadding={45} />);
    const svg = screen.getByRole("img", { name: "节点深度分布（X 坐标）" });
    expect(screen.queryByRole("img", { name: "独立环境采样剖面" })).toBeNull();
    expect(screen.queryByRole("img", { name: "链路剖面" })).toBeNull();
    expect(svg.querySelector(".depth-terrain-line")).toBeNull();
    const actualX = Number(svg.querySelector(".depth-node")!.getAttribute("transform")!.match(/translate\(([^,]+)/)![1]);
    expect(actualX).toBeCloseTo(view.point(nodes[0].position).x, 10);
    expect(svg.getAttribute("preserveAspectRatio")).toBe("none");
    expect(screen.getByText(/按 X 展示深度/)).toBeTruthy();
  });
  it("accepts real Runtime samples with string identity/time metadata alongside coordinates", () => {
    const sample = { node_id: "1", at_ns: "2000000000", x_meters: 130, y_meters: 0, z_meters: -60 };
    render(<SceneDepthProfile nodes={[{ node_id: sample.node_id, position: sample }]} simulationTimeNs={sample.at_ns} />);
    expect(screen.getByRole("img", { name: "深度节点 N1 · 60 m" }).getAttribute("data-x-meters")).toBe("130");
    expect(screen.queryByText("尚无当前位置数据")).toBeNull();
  });
  it("uses the same supplied positions and simulation timestamp, never a second sampling clock", () => {
    const { rerender } = render(<SceneDepthProfile nodes={nodes} simulationTimeNs="1000000001" xDomain={[-500, 500]} />);
    const first = screen.getByRole("img", { name: "深度节点 N1 · 30 m" });
    expect(first.getAttribute("data-x-meters")).toBe("100");
    expect(first.getAttribute("data-y-meters")).toBe("20");
    expect(screen.getByText("仿真时间 1 s · 与上方节点位置同步")).toBeTruthy();
    expect(screen.getByLabelText("场景深度与环境剖面").getAttribute("data-simulation-time-ns")).toBe("1000000001");
    rerender(<SceneDepthProfile nodes={[{ ...nodes[0], position: { x_meters: 140, y_meters: 25, z_meters: -45 } }]} simulationTimeNs="5000000000" xDomain={[-500, 500]} />);
    expect(screen.getByRole("img", { name: "深度节点 N1 · 45 m" }).getAttribute("data-x-meters")).toBe("140");
    expect(screen.queryByRole("img", { name: "深度节点 N1 · 30 m" })).toBeNull();
    expect(screen.getByLabelText("场景深度与环境剖面").getAttribute("data-simulation-time-ns")).toBe("5000000000");
  });

  it("removes the standalone simplified terrain display", () => {
    render(<SceneDepthProfile nodes={nodes} environmentInputs={environment} />);
    const nodePlot = screen.getByRole("img", { name: "节点深度分布（X 坐标）" });
    expect(nodePlot.querySelector(".depth-terrain-line")).toBeNull();
    expect(screen.queryByRole("img", { name: "独立环境采样剖面" })).toBeNull();
  });

  it("does not infer a sea floor from environment coverage, malformed samples, or geography alone", () => {
    for (const input of [undefined, { available: false, payload: environment.payload },
      { available: true, payload: { maximum_depth_meters: 500, maximum_range_meters: 3000 } },
      { ...environment, payload: { bathymetry: [{ range_meters: 0, depth_meters: 90 }, { range_meters: 0, depth_meters: 100 }] } },
      { ...environment, payload: { bathymetry: [{ range_meters: 0, depth_meters: 90 }, { range_meters: 600, depth_meters: NaN }] } },
    ]) expect(environmentSampleProfile(input)).toBeUndefined();
    render(<SceneDepthProfile nodes={nodes} environmentInputs={{ available: false }} />);
    expect(screen.queryByRole("img", { name: "独立环境采样剖面" })).toBeNull();
    expect(document.querySelector(".depth-terrain-line")).toBeNull();
  });

  it("shows actual paired depth and 3-4-5 horizontal distance without calling it a ray", () => {
    const onSelect = vi.fn();
    render(<SceneDepthProfile nodes={nodes} selectedNodeIds={["1", "2"]} onSelectNode={onSelect} environmentInputs={environment} />);
    const plot = screen.getByRole("img", { name: "链路剖面" });
    expect(screen.getByText(/水平距离 500.0 m/)).toBeTruthy();
    expect(plot.querySelector(".depth-measure-line")).toBeTruthy();
    expect(plot.querySelector(".depth-terrain-line")).toBeNull();
    expect(screen.getByText(/测距连线不是声线/)).toBeTruthy();
    fireEvent.click(within(plot).getByRole("button", { name: "深度节点 N2 · 80 m" }), { shiftKey: true });
    expect(onSelect).toHaveBeenCalledWith("2", true);
  });

  it("collapses only the lower panel and preserves node selection data", () => {
    render(<SceneDepthProfile nodes={nodes} />);
    fireEvent.click(screen.getByRole("button", { name: "收起深度剖面" }));
    expect(screen.queryByRole("img", { name: "节点深度分布（X 坐标）" })).toBeNull();
    fireEvent.click(screen.getByRole("button", { name: "展开深度剖面" }));
    expect(screen.getByRole("img", { name: "深度节点 N1 · 30 m" })).toBeTruthy();
  });

  it("marks reference samples and preserves negative above-surface depth instead of abs/clamp", () => {
    render(<SceneDepthProfile nodes={[{ node_id: "7", position: { x_meters: 0, y_meters: 0, z_meters: 5 } }]}
      environmentInputs={{ ...environment, payload: { ...environment.payload, acquisition: { useReferenceCache: true } } }} />);
    expect(screen.getByRole("img", { name: "深度节点 N7 · -5 m" })).toBeTruthy();
    expect(screen.queryByText("参考环境地形样本")).toBeNull();
  });

  it("retains upper plan, lower depth, and selection linkage in the actual ScenarioDesigner", () => {
    const draft: ScenarioDraft = { kind: "scenario", id: "depth-scene", name: "上下视图", environmentAssetId: "env",
      provenance: { kind: "new", label: "新建" },
      fusionCenterNodeId: "1", updatedAt: "2026-09-17T00:00:00Z", nodes: nodes.map(node => ({ ...scenario.nodes[0], node_id: node.node_id, initial_position: node.position })) };
    render(<ScenarioDesigner draft={draft} onChange={vi.fn()} environmentInputs={environment} globalInspector={<p>网络参数</p>} />);
    expect(screen.getByRole("img", { name: "可编辑场景画布" })).toBeTruthy();
    expect(screen.getByRole("img", { name: "节点深度分布（X 坐标）" })).toBeTruthy();
    fireEvent.click(screen.getByRole("button", { name: "深度节点 N2 · 80 m" }));
    expect((screen.getByLabelText("节点编号") as HTMLInputElement).value).toBe("2");
    fireEvent.click(screen.getByRole("button", { name: "收起深度剖面" }));
    expect(screen.getByRole("img", { name: "可编辑场景画布" })).toBeTruthy();
    expect(document.querySelectorAll(".draggable-node .uuv-node-glyph")).toHaveLength(2);
  });

  it("uses a neutral UUV schematic without data and rotates only supplied trajectory heading", () => {
    const { rerender, container } = render(<svg><UuvNodeGlyph /></svg>);
    expect(container.querySelector(".uuv-node-glyph")?.getAttribute("data-heading-source")).toBe("schematic");
    expect(container.textContent).toContain("朝向不代表姿态");
    rerender(<svg><UuvNodeGlyph yawRadians={Math.PI / 2} tone="tx" /></svg>);
    expect(container.querySelector(".uuv-node-glyph")?.getAttribute("transform")).toBe("rotate(-90) scale(1)");
    expect(container.querySelector(".uuv-node-glyph")?.getAttribute("data-heading-source")).toBe("trajectory");
  });
});
