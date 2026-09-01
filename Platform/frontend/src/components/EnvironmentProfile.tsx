import type { EnvironmentDto } from "../api/types";
import type { EnvironmentDraft } from "../domain/workspace";

function polyline(points: Array<{ x: number; y: number }>, width: number, height: number): string {
  if (!points.length) return "";
  const xs = points.map((point) => point.x);
  const ys = points.map((point) => point.y);
  const minX = Math.min(...xs); const maxX = Math.max(...xs);
  const minY = Math.min(...ys); const maxY = Math.max(...ys);
  return points.map((point) => {
    const x = 44 + ((point.x - minX) / Math.max(maxX - minX, 1)) * (width - 72);
    const y = 24 + ((point.y - minY) / Math.max(maxY - minY, 1)) * (height - 54);
    return `${x},${y}`;
  }).join(" ");
}

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
        <g transform="translate(105 94)"><circle r="8" className="source-marker" /><path d="M15 -20 Q95 0 175 32 M15 0 Q95 22 175 52 M15 20 Q95 43 175 70" className="acoustic-rays" /></g>
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
  const ssp = draft.soundSpeedProfile.map((point) => ({ x: point.speedMetersPerSecond, y: point.depthMeters }));
  const bathymetry = draft.bathymetry.map((point) => ({ x: point.rangeMeters, y: point.depthMeters }));
  return (
    <div className="profile-pair">
      <figure><figcaption>声速剖面（草稿输入）</figcaption><svg viewBox="0 0 360 260" role="img" aria-label="草稿声速剖面"><rect x="1" y="1" width="358" height="258" className="plot-frame" /><polyline points={polyline(ssp, 360, 260)} className="profile-line" /><text x="210" y="248">声速 m/s</text><text x="10" y="20">深度 m ↓</text></svg></figure>
      <figure><figcaption>海底地形（草稿输入）</figcaption><svg viewBox="0 0 360 260" role="img" aria-label="草稿海底地形"><rect x="1" y="1" width="358" height="258" className="plot-frame" /><polyline points={polyline(bathymetry, 360, 260)} className="bathymetry-line" /><text x="230" y="248">水平距离 m</text><text x="10" y="20">深度 m ↓</text></svg></figure>
    </div>
  );
}
