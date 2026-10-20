import { useRef, useState } from "react";
import { useMutation, useQuery } from "@tanstack/react-query";
import { Link, useNavigate, useParams } from "react-router-dom";
import { apiClient } from "../api/client";
import { ErrorState, LoadingState } from "../components/common";
import { ResultAnalysis } from "./resultAnalysis";
import { waveformSummary } from "../domain/resultAnalysis";

const labels: Record<string, string> = { Queued: "排队中", Running: "运行中", RunningA: "正在运行 A", RunningB: "正在运行 B",
  RunningC: "正在运行 C",
  Completed: "已完成", PartialFailed: "部分完成", Failed: "运行失败", Canceled: "已取消", Interrupted: "已中断", ArchiveFailed: "归档失败" };
const label = (state: string) => labels[state] ?? state;

export function RunGroupCatalogPage() {
  const query = useQuery({ queryKey: ["run-groups"], queryFn: apiClient.listRunGroups, refetchInterval: 1000 });
  if (query.error) return <ErrorState error={query.error} />;
  if (!query.data) return <LoadingState />;
  return <section className="panel"><h1>A/B 对照与 C 代理实验归档</h1><Link to="/runs">返回运行列表</Link>
    {!query.data.length && <p>尚无对照实验。在几何声场工作台中选择“A+B 顺序对照”。</p>}
    <table><thead><tr><th>实验归档</th><th>状态</th><th>运行成员</th></tr></thead><tbody>{query.data.map(group =>
      <tr key={group.group_id}><td><Link to={`/run-groups/${group.group_id}`}>{group.group_id}</Link></td>
        <td>{label(group.state)}</td><td>{group.members.map(m => `${m.member}：${label(m.state)}`).join(" / ")}</td></tr>)}</tbody></table>
  </section>;
}

export function RunGroupPage() {
  const id = useParams().groupId ?? "";
  const navigate = useNavigate();
  const repeatKey = useRef<{ id: string; key: string } | null>(null);
  const [tab, setTab] = useState("Compare");
  const query = useQuery({ queryKey: ["run-group", id], queryFn: () => apiClient.getRunGroup(id),
    refetchInterval: q => q.state.data && ["Queued", "RunningA", "RunningB", "RunningC"].includes(q.state.data.state) ? 1000 : false });
  const cancel = useMutation({ mutationFn: () => apiClient.cancelPendingGroup(id), onSuccess: () => { void query.refetch(); } });
  const repeat = useMutation({ mutationFn: () => {
    if (repeatKey.current?.id !== id) repeatKey.current = { id, key: `repeat-${crypto.randomUUID()}` };
    return apiClient.repeatRunGroup(id, repeatKey.current.key);
  }, onSuccess: created => { repeatKey.current = null; navigate(`/run-groups/${created.group_id}`); } });
  const archive = useMutation({ mutationFn: () => apiClient.retryGroupArchive(id), onSuccess: () => { void query.refetch(); } });
  if (query.error) return <ErrorState error={query.error} />;
  if (!query.data) return <LoadingState />;
  const group = query.data;
  if (group.members.length === 1 && group.members[0].member === "C") {
    const member = group.members[0];
    return <div className="analysis-page"><header className="analysis-page-toolbar"><h1>实录样本代理网络归档</h1>
      <span role="status">{label(group.state)}</span><Link to="/run-groups">全部实验归档</Link></header>
      <section className="panel"><p>跨链路时间关系、噪声分配与建模时延均是冻结的仿真假设，不代表实测多发一收海试。</p>
        <p>运行：{member.run_id} · 业务包：{group.workload.count}</p>
        {!group.archived && <Link to={`/runs/${member.run_id}`}>查看运行与回放</Link>}
        {group.archived && <p>结果与配置已归档；历史事件未归档，不提供虚假回放。</p>}
        {member.failure && <p role="alert">{member.failure.message}</p>}
        {group.rerun_available && <button disabled={repeat.isPending} onClick={() => repeat.mutate()}>按原配置重跑 C</button>}
        {group.archive_failure && <button disabled={archive.isPending} onClick={() => archive.mutate()}>重试保存归档</button>}
        {(repeat.error || archive.error) && <ErrorState error={repeat.error || archive.error} />}
        <a href={apiClient.url(`/run-groups/${encodeURIComponent(id)}/export`)} download={`${id}.json`}>下载冻结配置与结果</a>
      </section>
      {member.result ? <ResultAnalysis result={member.result} physicalLayer="Channel3Recorded" eventStreamComplete={false} />
        : <p>尚无正式结果；失败或未完成不记为零误码。</p>}
    </div>;
  }
  const selected = group.members.find(m => m.member === tab);
  return <div className="analysis-page"><header className="analysis-page-toolbar"><h1>A/B 对照实验</h1>
    <span role="status">{label(group.state)}</span><Link to="/run-groups">全部对照组</Link></header>
    <section className="panel"><p>同一场景与业务输入，共 {group.workload.count} 个业务包；A、B 顺序独立执行。</p>
      <p>A 使用抽象接收模型；B 使用波形解调统计，二者误码率不能混称实测值。</p>
      <div className="form-grid">{group.members.map(m => <article key={m.member} className="panel"><h2>{m.member === "A" ? "A · 快速网络实验" : "B · 波形通信实验"}</h2>
        <p>{label(m.state)}</p>{m.failure && <p role="alert">{m.failure.message}</p>}
        {!group.archived && <Link to={`/runs/${m.run_id}`}>查看运行与回放</Link>}
        {group.archived && <p>历史结果已归档；事件回放未归档。</p>}</article>)}</div>
      {!group.archived && group.members[1].state === "Queued" && <button disabled={cancel.isPending} onClick={() => cancel.mutate()}>取消尚未开始的 B</button>}
      {group.rerun_available && <button disabled={repeat.isPending} onClick={() => repeat.mutate()}>{repeat.isPending ? "正在校验原配置…" : "按原配置重跑 A+B"}</button>}
      {group.archive_failure && !group.archived && <button disabled={archive.isPending} onClick={() => archive.mutate()}>重试保存归档</button>}
      {(repeat.error || archive.error) && <ErrorState error={repeat.error || archive.error} />}
      {cancel.error && <ErrorState error={cancel.error} />}{group.archive_failure && <p role="alert">{group.archive_failure.message}</p>}
      <details><summary>冻结输入与结果导出</summary><p>共同输入：{group.common_snapshot_sha256}</p><p>业务清单：{group.workload_sha256}</p>
        <a href={apiClient.url(`/run-groups/${encodeURIComponent(id)}/export`)} download={`${id}.json`}>下载对照组归档</a></details>
    </section>
    <nav aria-label="对照结果视图">{[["Compare", "对照"], ["A", "A 结果"], ["B", "B 结果"]].map(([key, name]) =>
      <button key={key} aria-pressed={tab === key} onClick={() => setTab(key)}>{name}</button>)}</nav>
    {tab === "Compare" ? <section className="panel"><h2>运行结果对照</h2><table><thead><tr><th>指标</th><th>A</th><th>B</th></tr></thead><tbody>
      {([["transmission_count", "实际发送次数"], ["local_delivery_count", "本节点成功交付次数"], ["cycle_count", "完成周期数"]] as const).map(([key, name]) =>
        <tr key={key}><th>{name}</th>{group.members.map(m => <td key={m.member}>{m.result?.projection[key] ?? "尚无结果"}</td>)}</tr>)}
      {(["实际误码数 / 比较比特数", "波形误码率 BER", "CRC 通过 / 失败帧数"] as const).map((name, index) =>
        <tr key={name}><th>{name}</th>{group.members.map(m => {
          if (m.member === "A") return <td key={m.member}>不适用：抽象接收模型</td>;
          const summary = m.result ? waveformSummary(m.result) : null;
          const value = !summary?.frames ? "尚无波形记录" : index === 0 ? `${summary.errors} / ${summary.bits}`
            : index === 1 ? summary.ber === null ? "无可比较比特" : summary.ber === 0 ? "0" : summary.ber.toExponential(3)
            : `${summary.crcPassed} / ${summary.crcFailed}`;
          return <td key={m.member}>{value}</td>;
        })}</tr>)}
    </tbody></table><p>波形统计包含已记录接收帧；未形成比特判决的失败不会记作零误码。交付次数不等同于去重业务包数。</p></section> : selected?.result ? <ResultAnalysis result={selected.result} physicalLayer={tab === "A" ? "Channel1Abstract" : "Channel2Waveform"} eventStreamComplete={false} /> : <p>该成员尚无可用结果。请查看上方运行状态。</p>}
  </div>;
}
