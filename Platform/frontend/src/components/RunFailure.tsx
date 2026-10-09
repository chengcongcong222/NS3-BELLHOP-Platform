import type { FailureDto } from "../api/types";

export function runFailureExplanation(failure: FailureDto): string {
  if (failure.message.includes("TDMA slot cannot contain")) return "发送时隙太短，放不下当前数据包和保护时间。请修改本实验的 TDMA 时序后再运行。";
  if (failure.message.includes("Trajectory initial position")) return "节点起点与绑定轨迹不一致。请回到本实验检查轨迹布设。";
  if (/out.of.(coverage|range)|outside.*(domain|coverage)/i.test(failure.message)) return "本次查询超出了环境或轨迹的有效范围，请检查节点位置、深度、频率与实验时长。";
  if (/NotFound|MissingResource/i.test(failure.code)) return "本次实验引用的资源或版本无法读取。请检查环境、轨迹和场景绑定，恢复相应版本后再试。";
  return "运行程序未能完成本次实验。请先检查资源绑定与实验参数；需要进一步排查时，可展开技术详情。";
}

export function RunFailure({ failure }: { failure: FailureDto }) {
  return <section className="run-failure" role="alert"><h2>本次运行未完成</h2><p>{runFailureExplanation(failure)}</p><p>失败记录会保留；重新运行会创建一次新记录，不会覆盖本次实验结果。</p><details><summary>技术详情</summary><code>{failure.code}</code><p>{failure.message}</p></details></section>;
}
