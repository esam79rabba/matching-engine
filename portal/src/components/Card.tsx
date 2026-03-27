interface CardProps {
  label: string
  value: string | number
  colorClass?: string
}

export default function Card({ label, value, colorClass }: CardProps) {
  return (
    <div className="card">
      <div className="card-label">{label}</div>
      <div className={`card-value ${colorClass ?? ''}`}>{String(value)}</div>
    </div>
  )
}
