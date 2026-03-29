interface Props {
  label: string
  value: string | number
  color?: 'green' | 'red' | 'amber' | ''
}

export default function Card({ label, value, color = '' }: Props) {
  return (
    <div className="card">
      <div className="card-label">{label}</div>
      <div className={`card-value ${color}`}>{value}</div>
    </div>
  )
}
