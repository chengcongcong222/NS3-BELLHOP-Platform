import { useMutation, useQuery, useQueryClient } from "@tanstack/react-query";
import { useEffect } from "react";
import { Link, useNavigate } from "react-router-dom";
import { apiClient } from "../api/client";
import { queries } from "../api/queries";
import { formatNanoseconds } from "../domain/format";
import { ErrorState } from "./common";
import "../features/runLibrary.css";

export function RunActions({ runId, compact = false, lifecycle }: { runId: string; compact?: boolean; lifecycle?: string }) {
  const action = useQuery(queries.runActions(runId));
  const navigate = useNavigate(), client = useQueryClient();
  useEffect(() => { if (lifecycle === "Completed" || lifecycle === "Failed") void client.invalidateQueries({ queryKey: queries.runActions(runId).queryKey }); }, [client, runId, lifecycle]);
  const repeat = useMutation({ mutationFn: () => apiClient.repeatRun(runId), onSuccess: async created => {
    await client.invalidateQueries({ queryKey: ["runs"] }); navigate(`/runs/${created.run_id}?presentation=live`);
  } });
  if (!action.data) return <small className="run-action-unavailable">{action.isPending ? "正在核对原实验…" : "当前服务未提供原配置操作，未跳转到空白实验。"}</small>;
  const { edit, rerun, timing } = action.data;
  return <div className={`run-actions ${compact ? "compact" : ""}`}>
    <div className="run-action-buttons">{edit.available && edit.experiment_draft_id ? <Link to={`/workspace/experiment?draft=${encodeURIComponent(edit.experiment_draft_id)}`}>修改本实验</Link> : <span title={edit.reason ?? undefined}>原草稿不可编辑</span>}
      <button disabled={!rerun.available || repeat.isPending} title={rerun.reason ?? "用本次冻结配置重新计算；创建新记录，不覆盖当前运行"} onClick={() => repeat.mutate()}>{repeat.isPending ? "正在创建新运行…" : "按原配置重新运行"}</button></div>
    {!compact && <><small>修改：打开原实验的当前草稿；重新运行：使用本次冻结配置，不读取后续草稿修改。
      {edit.reason && <span className="run-edit-reason"> {edit.reason}</span>}</small>
      {timing && !timing.slot_contains_airtime_and_guard && <p className="run-timing-warning">本次配置的时隙为 {formatNanoseconds(timing.configured_slot_duration_ns).split(" (")[0]}，仅数据发送与保护时间就至少需要 {formatNanoseconds(timing.minimum_slot_duration_ns).split(" (")[0]}。请先修改时隙；直接重跑同一配置仍会失败。</p>}</>}
    {repeat.error && <ErrorState error={repeat.error} />}
  </div>;
}
