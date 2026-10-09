import { fireEvent, render, screen, within } from "@testing-library/react";
import { describe, expect, it } from "vitest";
import { ExperimentParticipation, type ExperimentParticipationProps } from "../components/ExperimentParticipation";

const nodes = [
  { node_id: "0", can_transmit: false, can_receive: true },
  ...["1", "2", "3", "4"].map(node_id => ({ node_id, can_transmit: true, can_receive: true })),
];
const props: ExperimentParticipationProps = { nodes, workload: "BearingFusion", fusionReceiverNodeId: "3", simulationCycleCount: "2", minimumBearingPoints: 7 };

describe("experiment participation readiness", () => {
  it("explains the actual three sources in a five-node scene without changing hardware or parameters", () => {
    const original = JSON.stringify(props);
    render(<ExperimentParticipation {...props} />);
    expect(screen.getByText("查看各节点能力与业务").closest("details")?.open).toBe(false);
    fireEvent.click(screen.getByText("查看各节点能力与业务"));
    const table = screen.getByRole("table", { name: "节点硬件能力与本次业务" });
    const n0 = within(table).getByRole("rowheader", { name: "N0" }).closest("tr")!;
    expect(within(n0).getByText("不可发送")).toBeTruthy();
    expect(within(n0).getByText("可接收")).toBeTruthy();
    expect(within(n0).getByText("不产生方位报告")).toBeTruthy();
    const n3 = within(table).getByRole("rowheader", { name: "N3" }).closest("tr")!;
    expect(within(n3).getByText("可发送")).toBeTruthy();
    expect(within(n3).getByText("固定应用接收点，不生成报告")).toBeTruthy();
    expect(screen.getByText(/报告源：N1、N2、N4/)).toBeTruthy();
    expect(screen.getByText("6 条")).toBeTruthy();
    expect(screen.getByText(/数量上限不足：最多 6 条，低于融合门槛 7 条/)).toBeTruthy();
    expect(screen.getByText(/至少需要 3 个业务周期/)).toBeTruthy();
    expect(screen.getByText(/4 个可发送 · 5 个可接收/)).toBeTruthy();
    expect(JSON.stringify(props)).toBe(original);
  });

  it("makes a threshold of nine observation identities distinct from nine nodes", () => {
    render(<ExperimentParticipation {...props} simulationCycleCount="3" minimumBearingPoints={9} />);
    expect(screen.getByText("9 条")).toBeTruthy();
    expect(screen.getByText(/至少 9 条有效观测，不是 9 个节点/)).toBeTruthy();
    expect(screen.getByText(/重复交付不重复计数/)).toBeTruthy();
    expect(screen.queryByText(/数量上限不足/)).toBeNull();
    expect(screen.getByText(/不是发送次数、有效交付或融合成功的保证/)).toBeTruthy();
    expect(screen.getByText(/报告足够后仍需方位几何可解/)).toBeTruthy();
    expect(screen.queryByText(/保证成功|满足验收|通过验收/)).toBeNull();
  });

  it("treats N0 as a valid explicit receiver and never selects it implicitly", () => {
    const { rerender } = render(<ExperimentParticipation {...props} fusionReceiverNodeId="0" />);
    expect(screen.getByText(/报告源：N1、N2、N3、N4/)).toBeTruthy();
    expect(screen.getByText("8 条")).toBeTruthy();
    rerender(<ExperimentParticipation {...props} fusionReceiverNodeId={null} />);
    expect(screen.getByText(/请在下方“定位算法”中选择可接收节点/)).toBeTruthy();
    expect(screen.queryByText(/理论报告上限/)).toBeNull();
  });

  it("does not claim readiness when the configured receiver cannot receive", () => {
    render(<ExperimentParticipation {...props} nodes={nodes.map(node => node.node_id === "3" ? { ...node, can_receive: false } : node)} />);
    expect(screen.getByText(/请在下方“定位算法”中选择可接收节点/)).toBeTruthy();
    expect(screen.queryByText(/理论报告上限/)).toBeNull();
  });

  it("shows zero report sources explicitly and leaves invalid counts unevaluated", () => {
    const { rerender } = render(<ExperimentParticipation {...props} nodes={nodes.map(node => ({ ...node, can_transmit: node.node_id === "3" }))} />);
    expect(screen.getByText(/没有可发送的报告源/)).toBeTruthy();
    expect(screen.getByText("0 条")).toBeTruthy();
    rerender(<ExperimentParticipation {...props} simulationCycleCount="" minimumBearingPoints={Number.NaN} />);
    expect(screen.getByText(/暂不推算是否达到数量门槛/)).toBeTruthy();
    expect(screen.queryByText(/理论报告上限/)).toBeNull();
  });

  it("uses exact integer arithmetic for a theoretical count beyond JavaScript safe integers", () => {
    render(<ExperimentParticipation {...props} simulationCycleCount="9007199254740993" />);
    expect(screen.getByText("27021597764222979 条")).toBeTruthy();
  });

  it("keeps configured communication sources separate from hardware and fusion", () => {
    render(<ExperimentParticipation {...props} workload="CommunicationOnly" flows={[
      { source_node_id: "1", destination_node_id: "0", packets_per_cycle: 1 },
      { source_node_id: "1", destination_node_id: "2", packets_per_cycle: 2 },
    ]} />);
    expect(screen.getByText(/已配置通信流来源：N1；目的：N0、N2/)).toBeTruthy();
    expect(screen.getByText(/纯通信实验不产生方位报告/)).toBeTruthy();
    expect(screen.queryByText(/理论报告上限|融合数量门槛|数量上限不足/)).toBeNull();
    fireEvent.click(screen.getByText("查看各节点能力与业务"));
    const n3 = screen.getByRole("rowheader", { name: "N3" }).closest("tr")!;
    expect(within(n3).getByText("未配置通信流")).toBeTruthy();
  });
});
