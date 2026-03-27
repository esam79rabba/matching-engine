interface StatusBadgeProps {
  text: string
  variant: 'green' | 'red' | 'amber' | 'blue'
}

const variantClass: Record<StatusBadgeProps['variant'], string> = {
  green: 'badge-green',
  red: 'badge-red',
  amber: 'badge-amber',
  blue: 'badge-blue',
}

export default function StatusBadge({ text, variant }: StatusBadgeProps) {
  return <span className={`badge ${variantClass[variant]}`}>{text}</span>
}
