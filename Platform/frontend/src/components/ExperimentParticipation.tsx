import type { CommunicationFlowDto, ScenarioDto } from "../api/types";

export interface ExperimentParticipationProps {
  nodes: ReadonlyArray<Pick<ScenarioDto["nodes"][number], "node_id" | "can_transmit" | "can_receive">>;
  workload: "BearingFusion" | "CommunicationOnly";
  fusionReceiverNodeId: string | null;
  simulationCycleCount: string;
  minimumBearingPoints: number;
  flows?: readonly CommunicationFlowDto[];
}

/** Read-only preparation summary, not a planner, validator or predicted result. */
export function ExperimentParticipation({ nodes, workload, fusionReceiverNodeId,
  simulationCycleCount, minimumBearingPoints, flows = [] }: ExperimentParticipationProps) {
  const bearing = workload === "BearingFusion";
  const receiver = nodes.find(node => node.node_id === fusionReceiverNodeId);
  const receiverReady = receiver?.can_receive === true;
  const reporters = receiverReady ? nodes.filter(node => node.can_transmit && node.node_id !== fusionReceiverNodeId) : [];
  const cycles = /^[1-9][0-9]*$/.test(simulationCycleCount) ? BigInt(simulationCycleCount) : null;
  const threshold = Number.isSafeInteger(minimumBearingPoints) && minimumBearingPoints > 0 ? BigInt(minimumBearingPoints) : null;
  const reportLimit = receiverReady && cycles !== null ? BigInt(reporters.length) * cycles : null;
  const minimumCycles = threshold !== null && reporters.length > 0
    ? (threshold + BigInt(reporters.length) - 1n) / BigInt(reporters.length) : null;
  const flowSources = new Set(flows.map(flow => flow.source_node_id));
  const flowDestinations = new Set(flows.map(flow => flow.destination_node_id));
  const names = (ids: readonly string[]) => ids.length ? ids.map(id => `N${id}`).join("、") : "无";
  const participation = (node: ExperimentParticipationProps["nodes"][number]) => {
    if (!bearing) {
      const roles = [flowSources.has(node.node_id) ? "通信流源" : null, flowDestinations.has(node.node_id) ? "通信流目的" : null].filter(Boolean);
      return roles.join("、") || "未配置通信流";
    }
    if (node.node_id === fusionReceiverNodeId) return "固定应用接收点，不生成报告";
    if (!node.can_transmit) return "不产生方位报告";
    return receiverReady ? "每周期 1 份方位报告" : "候选报告源，接收点未就绪";
  };
  return <section className="parameter-detail experiment-participation" aria-label="节点参与与观测准备">
    <h3>本次参与摘要</h3>
    <p>{nodes.length} 个节点 · {nodes.filter(node => node.can_transmit).length} 个可发送 · {nodes.filter(node => node.can_receive).length} 个可接收。</p>
    {bearing ? <>
      {receiverReady && <p>报告源：{names(reporters.map(node => node.node_id))} → 处理节点 N{receiver!.node_id}。</p>}
      {!receiverReady && <p className="property-help" role="status">请在下方“定位算法”中选择可接收节点。</p>}
      {receiverReady && reporters.length === 0 && <p className="property-help" role="status">除应用接收点外，没有可发送的报告源；本配置不会产生方位报告。</p>}
      {reportLimit !== null && <p>理论报告上限：<strong>{String(reportLimit)} 条</strong>（{reporters.length} 源 × {String(cycles)} 周期）；定位至少需 {minimumBearingPoints} 条。</p>}
      {reportLimit !== null && threshold !== null && reportLimit < threshold && <p className="property-help" role="status">数量上限不足：最多 {String(reportLimit)} 条，低于融合门槛 {String(threshold)} 条；即使全部送达也不能形成融合。{minimumCycles !== null && `仅按数量计算至少需要 ${minimumCycles} 个业务周期。`}</p>}
      {(cycles === null || threshold === null) && <p className="property-help" role="status">周期数和最少有效观测数需填写有效正整数；暂不推算是否达到数量门槛。</p>}
    </> : <>
      <p>已配置通信流来源：{names([...flowSources].filter(id => id !== ""))}；目的：{names([...flowDestinations].filter(id => id !== ""))}。</p>
      <p>{flows.length ? "节点业务来自显式通信流；具备发送能力不代表本次一定有包发送。" : "尚未配置通信流；仅具备收发能力不会产生业务。"}纯通信实验不产生方位报告，也不使用融合观测门槛。</p>
    </>}
    <details><summary>查看各节点能力与业务</summary>
      <p>收发能力可以同时具备，不是互斥角色。</p>
      <table aria-label="节点硬件能力与本次业务"><thead><tr><th scope="col">节点</th><th scope="col">发送</th><th scope="col">接收</th><th scope="col">本次业务</th></tr></thead><tbody>
        {nodes.map(node => <tr key={node.node_id}><th scope="row">N{node.node_id}</th><td>{node.can_transmit ? "可发送" : "不可发送"}</td><td>{node.can_receive ? "可接收" : "不可接收"}</td><td>{participation(node)}</td></tr>)}
      </tbody></table>
      {bearing && <>
        {threshold !== null && <p>融合数量门槛：至少 {String(threshold)} 条有效观测，不是 {String(threshold)} 个节点。同一节点在不同周期的报告可以分别计数，重复交付不重复计数。</p>}
        <p>当前应用接收点固定，不是动态选举或多中心融合。请自行调整周期、报告源或门槛，系统不会自动修改。</p>
        <p>这是报告生成的数量上限，不是发送次数、有效交付或融合成功的保证。可用发送机会、传播与解码损失会影响交付；报告足够后仍需方位几何可解，算法只在周期末检查。</p>
      </>}
    </details>
  </section>;
}
