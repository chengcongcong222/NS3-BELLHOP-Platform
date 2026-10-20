import { useState } from "react";
import type { RecordedProxyResolved, ReceptionWaveformDto, ReceptionTraceDto } from "../api/types";
import { formatBer } from "../domain/format";

export interface ProxyReceptionEvidence {
  transmission_id: string;
  sender_node_id: string | null;
  receiver_node_id: string;
  at_ns: string;
  disposition: ReceptionTraceDto["payload"]["disposition"];
  mixture: ReceptionTraceDto["payload"]["mixture"] | null;
  waveform: ReceptionWaveformDto | null;
}

/** Terminal observations only. Filtering cannot change simulation or bindings. */
export function ProxyResultEvidence({ source, receptions }: {
  source: RecordedProxyResolved; receptions?: ProxyReceptionEvidence[];
}) {
  const [tx, setTx] = useState(""), [rx, setRx] = useState("");
  const links = source.resolved_links.filter(l => (!tx || l.tx_id === tx) && (!rx || l.rx_id === rx));
  const rows = receptions?.filter(r => (!tx || r.sender_node_id === tx) && (!rx || r.receiver_node_id === rx));
  const decisions = rows?.flatMap(r => r.waveform ? [r.waveform] : []) ?? [];
  const bits = decisions.reduce((sum, r) => sum + BigInt(r.payload_bit_count), 0n);
  const errors = decisions.reduce((sum, r) => sum + BigInt(r.bit_error_count), 0n);
  return <div className="proxy-result-evidence">
    <p>实录样本驱动的代理网络。链路共时性、建模时延和噪声分配是仿真假设，不代表实测多发一收海试。</p>
    {!!source.transmitter_modulations?.length && <p>逐节点混合调制：
      {source.transmitter_modulations.map(m => `N${m.node_id} ${m.modulation.toUpperCase()}${m.modulation !== "Osdm" && source.receiver_clock ? ` ${source.receiver_clock.baseband_rate_hz/100} bit/s` : ""}`).join("；")}。
      其他发送节点继承全局 OSDM；接收机使用冻结的发送节点调制配置，不进行盲识别。</p>}
    <div className="form-grid">
      <label>发送节点<select aria-label="筛选发送节点" value={tx} onChange={e => setTx(e.target.value)}>
        <option value="">全部</option>{source.nodes.filter(n => n.can_transmit).map(n => <option key={n.node_id} value={n.node_id}>N{n.node_id}</option>)}
      </select></label>
      <label>接收节点<select aria-label="筛选接收节点" value={rx} onChange={e => setRx(e.target.value)}>
        <option value="">全部</option>{source.nodes.filter(n => n.can_receive).map(n => <option key={n.node_id} value={n.node_id}>N{n.node_id}</option>)}
      </select></label>
    </div>
    <p>当前筛选：{rows ? `${rows.length} 次接收，${decisions.length} 次产生比特判决` : "未保存逐链路接收记录"}。
      实际 BER：{bits ? formatBer(Number(errors) / Number(bits)) : "不可用"}；错误 / 比较比特：{String(errors)} / {String(bits)}。</p>
    {rows && <div className="analysis-scroll"><table><thead><tr><th>时间 / s</th><th>发送 → 接收</th><th>叠加信号数</th><th>接收结果</th><th>CRC</th></tr></thead>
      <tbody>{rows.map(r => <tr key={`${r.transmission_id}:${r.receiver_node_id}`}>
        <td>{(Number(r.at_ns) / 1e9).toFixed(3)}</td><td>{r.sender_node_id === null ? "未知发送端" : `N${r.sender_node_id}`} → N{r.receiver_node_id}</td>
        <td>{r.mixture?.signal_count ?? "未记录"}</td>
        <td>{({NotDecoded:"未解码",Overheard:"旁听",LocalDelivery:"业务交付",RelayEnqueue:"等待转发"})[r.disposition]}</td>
        <td>{r.waveform ? r.waveform.crc_ok ? "通过" : "失败" : "未产生判决"}</td>
      </tr>)}</tbody></table></div>}
    <details><summary>冻结的有向信道与接收噪声</summary>
      <div className="analysis-scroll"><table><thead><tr><th>方向</th><th>CIR / 版本 / 阵元</th><th>起始样本</th><th>建模时延 / s</th><th>接收端噪声</th></tr></thead>
        <tbody>{links.map(l => {
          const noise = source.rx_noise.find(n => n.rx_id === l.rx_id);
          return <tr key={`${l.tx_id}:${l.rx_id}`}><td>N{l.tx_id} → N{l.rx_id}</td>
            <td>{l.manifest.name ?? l.asset_id} / {l.version} / {l.manifest.element}</td><td>{l.start_sample}</td>
            <td>{Number(l.modeled_reference_delay_ns) / 1e9}</td>
            <td>{noise?.mode === "None" ? "无噪声" : String(noise?.manifest?.asset_id ?? "未记录")}</td></tr>;
        })}</tbody></table></div>
      <p>同一接收端共享一条连续噪声，只叠加一次；列表中重复列出资源不表示重复加噪声。</p>
    </details>
  </div>;
}
