interface Props {
  status: string
}

const statusColorMap: Record<string, string> = {
  New: 'badge-blue',
  Pending: 'badge-blue',
  PendingNew: 'badge-blue',
  PartialFill: 'badge-amber',
  Filled: 'badge-green',
  Canceled: 'badge-red',
  Rejected: 'badge-red',
  Expired: 'badge-red',
  Replaced: 'badge-amber',
  PendingCancel: 'badge-amber',
  PendingReplace: 'badge-amber',
  DoneForDay: 'badge-green',
}

export default function StatusBadge({ status }: Props) {
  const cls = statusColorMap[status] ?? 'badge-blue'
  return <span className={`badge ${cls}`}>{status}</span>
}
