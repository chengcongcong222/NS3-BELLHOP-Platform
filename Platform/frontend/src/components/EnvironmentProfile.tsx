import type { EnvironmentDto } from "../api/types";
import type { EnvironmentDraft } from "../domain/workspace";

export function PublishedEnvironmentProfile({ environment }: { environment: EnvironmentDto }) {
  const total = BigInt(environment.cell_count);
  const noArrival = BigInt(environment.no_arrival_cell_count);
  const ratio = total === 0n ? 0 : Number((noArrival * 10_000n) / total) / 100;
  const depth = Math.max(environment.axes.source_depth.maximum, environment.axes.receiver_depth.maximum);
  return (
    <div className="environment-visual-grid">
      <svg viewBox="0 0 700 270" role="img" aria-label="环境覆盖剖面" className="environment-profile">
        <defs><linearGradient id="water" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stopColor="#d8f1f4" /><stop offset="1" stopColor="#2e7180" /></linearGradient></defs>
        <rect x="40" y="28" width="620" height="190" rx="4" fill="url(#water)" />
        <path d="M40 218 C180 202 300 230 440 208 C540 192 610 205 660 188 L660 238 L40 238Z" className="seabed" />
        <line x1="40" y1="28" x2="660" y2="28" className="surface-line" />
        <text x="44" y="20">海面 · 0 m</text><text x="44" y="258">覆盖深度 {depth} m</text>
        <text x="485" y="258">水平距离 {environment.axes.horizontal_range.maximum} m</text>
        <rect x="70" y="52" width="560" height="132" className="coverage-field" />
        <text x="350" y="122" textAnchor="middle" className="coverage-label">AcousticFieldAsset 覆盖范围</text>
      </svg>
      <div className="visual-facts">
        <strong>{environment.axes.frequency.minimum / 1000} kHz</strong><span>正式资产工作频率</span>
        <strong>{environment.signal_cell_count} / {environment.cell_count}</strong><span>Signal 网格</span>
        <strong>{ratio}%</strong><span>正式 NoArrival 网格占比</span>
        <p>当前 HTTP DTO 不含逐点声速、海底地形或传播损失矩阵，示意图只表达正式覆盖轴；不会补造 SSP、TL 或声线路径。</p>
      </div>
    </div>
  );
}

export function DraftEnvironmentProfile({ draft }: { draft: EnvironmentDraft }) {
  const speeds = draft.soundSpeedProfile.map((point) => point.speedMetersPerSecond); const minimumSpeed = Math.min(...speeds); const maximumSpeed = Math.max(...speeds);
  const ssp = draft.soundSpeedProfile.map((point) => `${44 + ((point.speedMetersPerSecond - minimumSpeed) / Math.max(maximumSpeed - minimumSpeed, 1)) * 288},${24 + Math.min(1, point.depthMeters / Math.max(draft.maximumDepthMeters, 1)) * 206}`).join(" ");
  const bathymetry = draft.bathymetry.map((point) => `${44 + Math.min(1, point.rangeMeters / Math.max(draft.maximumRangeMeters, 1)) * 288},${24 + Math.min(1, point.depthMeters / Math.max(draft.maximumDepthMeters, 1)) * 206}`).join(" ");
  return (
    <div className="profile-pair">
      <figure><figcaption>声速剖面（草稿输入）</figcaption><svg viewBox="0 0 360 260" role="img" aria-label="草稿声速剖面"><rect x="1" y="1" width="358" height="258" className="plot-frame" /><polyline points={ssp} className="profile-line" /><text x="210" y="248">声速 m/s</text><text x="10" y="20">深度 m ↓</text></svg></figure>
      <figure><figcaption>海底地形（草稿输入）</figcaption><svg viewBox="0 0 360 260" role="img" aria-label="草稿海底地形"><rect x="1" y="1" width="358" height="258" className="plot-frame" /><polyline points={bathymetry} className="bathymetry-line" /><text x="230" y="248">水平距离 m</text><text x="10" y="20">深度 m ↓</text></svg></figure>
    </div>
  );
}
