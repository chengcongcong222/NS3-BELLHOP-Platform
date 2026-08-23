interface MetricCardProps {
  label: string;
  value: string;
  detail: string;
  tone?: 'default' | 'accent' | 'warning' | 'danger';
}

export function MetricCard({
  label,
  value,
  detail,
  tone = 'default',
}: MetricCardProps) {
  return (
    <article className={`metric-card metric-card--${tone}`}>
      <p className="metric-card__label">{label}</p>
      <p className="metric-card__value">{value}</p>
      <p className="metric-card__detail">{detail}</p>
    </article>
  );
}