interface MiniTrendProps {
  data: number[];
  unit: string;
  label: string;
}

export function MiniTrend({ data, unit, label }: MiniTrendProps) {
  if (data.length === 0) {
    return null;
  }

  const width = 320;
  const height = 100;
  const PADDING_V = 8;
  const usable = height - PADDING_V * 2;
  const min = Math.min(...data);
  const max = Math.max(...data);
  const avg = data.reduce((a, b) => a + b, 0) / data.length;
  const range = max - min || 1;
  const gradId = `tg-${label.replace(/\s+/g, '')}`;

  const pts = data.map((value, index) => ({
    x: (index / Math.max(data.length - 1, 1)) * width,
    y: PADDING_V + usable - ((value - min) / range) * usable,
  }));

  const linePoints = pts.map((p) => `${p.x},${p.y}`).join(' ');
  const areaPoints = [
    `0,${height}`,
    ...pts.map((p) => `${p.x},${p.y}`),
    `${width},${height}`,
  ].join(' ');

  return (
    <article className="trend-card">
      <div className="trend-card__head">
        <span>{label}</span>
        <strong>
          {data[data.length - 1].toFixed(3)} {unit}
        </strong>
      </div>
      <svg viewBox={`0 0 ${width} ${height}`} className="trend-card__svg" role="img" aria-label={label}>
        <defs>
          <linearGradient id={gradId} x1="0" x2="0" y1="0" y2="1">
            <stop offset="0%" stopColor="rgba(34, 211, 238, 0.5)" />
            <stop offset="100%" stopColor="rgba(34, 211, 238, 0.02)" />
          </linearGradient>
        </defs>
        <polygon fill={`url(#${gradId})`} points={areaPoints} />
        <polyline
          fill="none"
          stroke="rgba(34, 211, 238, 1)"
          strokeWidth="2.5"
          strokeLinecap="round"
          strokeLinejoin="round"
          points={linePoints}
        />
      </svg>
      <div className="trend-card__stats">
        <span>min {min.toFixed(3)}</span>
        <span>avg {avg.toFixed(3)}</span>
        <span>max {max.toFixed(3)}</span>
      </div>
    </article>
  );
}