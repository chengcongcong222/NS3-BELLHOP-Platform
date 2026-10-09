import type { EnvironmentDto } from "../api/types";
import type { EnvironmentDraft } from "../domain/workspace";

export function PublishedEnvironmentProfile({ environment }: { environment: EnvironmentDto }) {
  const total = BigInt(environment.cell_count); const noArrival = BigInt(environment.no_arrival_cell_count); const ratio = total === 0n ? 0 : Number((noArrival * 10_000n) / total) / 100; const depth = Math.max(environment.axes.source_depth.maximum, environment.axes.receiver_depth.maximum);
  return <div className="environment-visual-grid"><svg viewBox="0 0 700 270" role="img" aria-label="正式环境覆盖范围"><defs><linearGradient id="water" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stopColor="#d8f1f4" /><stop offset="1" stopColor="#2e7180" /></linearGradient></defs><rect x="40" y="28" width="620" height="190" rx="4" fill="url(#water)" /><line x1="40" y1="28" x2="660" y2="28" className="surface-line" /><line x1="40" y1="218" x2="660" y2="218" className="plot-axis" /><text x="44" y="20">海面 · 0 m</text><text x="44" y="238">深度轴 0–{depth} m</text><text x="485" y="238">水平距离 0–{environment.axes.horizontal_range.maximum} m</text><rect x="70" y="52" width="560" height="132" className="coverage-field" /><text x="350" y="122" textAnchor="middle" className="coverage-label">正式 AcousticFieldAsset 覆盖范围</text></svg><div className="visual-facts"><strong>{environment.axes.frequency.minimum / 1000} kHz</strong><span>正式资产工作频率</span><strong>{environment.signal_cell_count} / {environment.cell_count}</strong><span>Signal 网格</span><strong>{ratio}%</strong><span>正式 NoArrival 网格占比</span><p>当前资源接口未提供逐点 SSP、Bathymetry、TL、SNR 或传播路径；本视图只显示正式 coverage 与坐标轴。</p></div></div>;
}
export function DraftEnvironmentProfile({ draft }: { draft: EnvironmentDraft }) {
  return <div className="profile-pair">
    <NumericProfile name="草稿声速剖面" label="声速剖面" xLabel="声速（m/s）" line="profile-line" values={draft.soundSpeedProfile.map(p => [p.speedMetersPerSecond, p.depthMeters])} />
    <div className="environment-input-note"><strong>水文输入与信道计算</strong><p>声速剖面描述声速随水深的变化，不是水声传播结果。</p><p>完成计算后可查看真实声线路径。</p></div>
  </div>;
}

function NumericProfile({ name, label, xLabel, values, line }: { name: string; label: string; xLabel: string; values: [number, number][]; line: string }) {
  // Each plot owns its domain. Loading terrain must never change the SSP curve.
  const valid = values.length >= 2 && values.every(p => p.every(Number.isFinite));
  const xs = values.map(p => p[0]);
  const minimum = valid ? Math.min(...xs) : 0;
  const maximum = valid ? Math.max(...xs) : 1;
  const span = Math.max(maximum - minimum, 1);
  const depth = valid ? Math.max(1, ...values.map(p => p[1])) : 1;
  const point = ([x, y]: [number, number]) => [58 + (x - minimum) / span * 362, 32 + y / depth * 170];
  return <figure className="numeric-profile"><figcaption>{label}</figcaption>
    <svg viewBox="0 0 460 255" role="img" aria-label={name}>
      <rect width="460" height="255" fill="#ffffff" />
      <g fill="#d8eaf4" fontSize="13" fontFamily="sans-serif">
        <text x="12" y="19">深度（m，向下）</text><text x="230" y="249" textAnchor="middle">{xLabel}</text>
        {valid ? <>
          {[0, .25, .5, .75, 1].map(fraction => <g key={fraction}>
            <path d={`M58 ${32 + fraction * 170} H420 M${58 + fraction * 362} 32 V202`} stroke="#d3dfe8" fill="none" />
            <text x="49" y={36 + fraction * 170} textAnchor="end">{(depth * fraction).toFixed(0)}</text>
            <text x={58 + fraction * 362} y="223" textAnchor="middle">{(minimum + fraction * span).toFixed(maximum < 100 ? 1 : 0)}</text>
          </g>)}
          <polyline points={values.map(p => point(p).join(",")).join(" ")} className={line} />
          {values.map((p, i) => <circle key={i} cx={point(p)[0]} cy={point(p)[1]} r="3" fill="#70dced"><title>{`${xLabel} ${p[0]} · 深度 ${p[1]} m`}</title></circle>)}
        </> : <text x="230" y="128" textAnchor="middle">尚未输入{label}采样点</text>}
      </g>
    </svg>
  </figure>;
}
