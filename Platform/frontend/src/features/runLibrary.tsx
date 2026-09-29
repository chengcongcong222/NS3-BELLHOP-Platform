import { useState } from "react";
import { useQueries, useQuery } from "@tanstack/react-query";
import { Link } from "react-router-dom";
import { queries } from "../api/queries";
import { ErrorState, LoadingState, PageHeader, Status } from "../components/common";
import { RunActions } from "../components/RunActions";
import { runFailureExplanation } from "../components/RunFailure";
import "./runLibrary.css";

export function RunCatalogPage() {
  const query = useQuery({ ...queries.runs(), refetchInterval: 1000 });
  const metadata = useQueries({ queries: (query.data ?? []).map(run => queries.runActions(run.run_id)) });
  const [search, setSearch] = useState("");
  const [lifecycle, setLifecycle] = useState("All");
  const [page, setPage] = useState(0);
  const [expanded, setExpanded] = useState<Record<string, boolean>>({});
  if (query.isPending) return <LoadingState />;
  if (query.error) return <ErrorState error={query.error} />;
  const items = query.data.map((run, index) => ({ run, details: metadata[index].data }));
  const groups = new Map<string, typeof items>();
  for (const item of [...items].reverse()) {
    const key = item.details?.edit.experiment_draft_id ?? item.run.experiment_id;
    const content = [item.details?.experiment_name, item.details?.scenario_name, item.run.run_id,
      item.run.experiment_id, item.run.scenario_id, item.run.environment_asset_id].join(" ");
    if (search && !content.toLowerCase().includes(search.toLowerCase())) continue;
    if (lifecycle !== "All" && item.run.lifecycle !== lifecycle) continue;
    groups.set(key, [...(groups.get(key) ?? []), item]);
  }
  const pageSize = 6;
  const pageCount = Math.max(1, Math.ceil(groups.size / pageSize));
  const currentPage = Math.min(page, pageCount - 1);
  const visibleGroups = [...groups.entries()].slice(currentPage * pageSize, (currentPage + 1) * pageSize);
  return <div className="run-library"><PageHeader title="运行与结果" detail="按实验归档每次运行；展开查看当时的场景、环境与结果，原记录不会被重跑覆盖。" />
    <div className="run-library-toolbar"><label>查找实验或运行<input type="search" value={search} onChange={e => { setSearch(e.target.value); setPage(0); }} placeholder="实验、场景、环境或运行编号" /></label><label>运行状态<select value={lifecycle} onChange={event => { setLifecycle(event.target.value); setPage(0); }}><option value="All">全部状态</option><option value="Running">运行中</option><option value="Completed">已完成</option><option value="Failed">运行失败</option><option value="Created">已创建</option></select></label><span className="run-library-count">{groups.size} 个实验 · {[...groups.values()].reduce((count, records) => count + records.length, 0)} 次运行</span><Link to="/workspace/experiment">新建实验</Link></div>
    {!groups.size && <p>尚无匹配的运行记录。实验草稿可从首页“我的实验”继续编辑。</p>}
    {visibleGroups.map(([key, records], index) => {
      const latest = records[0];
      const name = latest.details?.experiment_name ?? latest.run.experiment_id;
      const open = expanded[key] ?? index === 0;
      return <section key={key} className="experiment-run-group">
      <header><button type="button" className="run-group-toggle" aria-label={`${open ? "收起" : "展开"} ${name}`} aria-expanded={open} onClick={() => setExpanded(previous => ({ ...previous, [key]: !open }))}><span aria-hidden="true">{open ? "▾" : "▸"}</span></button><div className="run-group-title"><h2>{name}</h2><small>最近场景：{latest.details?.scenario_name ?? latest.run.scenario_id} · v{latest.run.scenario_version}</small></div><span>{records.length} 次运行</span><Status value={latest.run.lifecycle} /><Link to={`/runs/${latest.run.run_id}`}>打开最近运行 →</Link></header>
      {open && <div className="run-record-table"><table><thead><tr><th>运行记录</th><th>状态</th><th>场景 / 环境版本</th><th>结果与操作</th></tr></thead><tbody>{records.map(({ run, details }) => <tr key={run.run_id}>
        <td><Link to={`/runs/${run.run_id}`}>{run.run_id}</Link><small>运行序号 #{run.catalog_sequence} · 实验版本 {run.experiment_version}</small></td>
        <td><Status value={run.lifecycle} />{run.failure && <span className="run-library-failure" title={run.failure.message}>{runFailureExplanation(run.failure)}</span>}</td>
        <td><span>{details?.scenario_name ?? run.scenario_id} · v{run.scenario_version}</span><small>{run.environment_asset_id} · v{run.environment_format_version}</small></td>
        <td><div className="run-record-links"><Link to={`/runs/${run.run_id}`}>查看运行</Link>{run.result_available && <Link to={`/results/${run.run_id}`}>查看结果</Link>}</div><details className="run-record-actions"><summary>重新运行 / 修改</summary><RunActions runId={run.run_id} compact lifecycle={run.lifecycle} /></details></td>
      </tr>)}</tbody></table></div>}
    </section>; })}
    {groups.size > 0 && <nav className="run-library-pagination" aria-label="运行目录分页"><span>第 {currentPage + 1} / {pageCount} 页 · 每页至多 {pageSize} 个实验</span><button type="button" disabled={currentPage === 0} onClick={() => setPage(currentPage - 1)}>上一页</button><button type="button" disabled={currentPage + 1 >= pageCount} onClick={() => setPage(currentPage + 1)}>下一页</button></nav>}
  </div>;
}
