import { useState, useEffect, useCallback } from 'react'
import { apiGet, apiPost } from '../api'
import type { Order } from '../types'
import Table from '../components/Table'
import StatusBadge from '../components/StatusBadge'

interface Props {
  connected: boolean
  showToast: (msg: string, type: 'success' | 'error') => void
}

export default function Orders({ connected, showToast }: Props) {
  const [orders, setOrders] = useState<Order[]>([])
  const [filter, setFilter] = useState<'all' | 'active'>('all')

  const refresh = useCallback(async () => {
    const endpoint = filter === 'active' ? '/api/orders/active' : '/api/orders'
    const data = await apiGet<Order[]>(endpoint)
    if (data) setOrders(data)
  }, [filter])

  useEffect(() => {
    refresh()
    const id = setInterval(refresh, 2000)
    return () => clearInterval(id)
  }, [refresh])

  const handleCancel = async (clOrdId: string) => {
    const res = await apiPost<{ status?: string; error?: string }>(`/api/orders/${clOrdId}/cancel`)
    if (res?.status) {
      showToast(`Cancel sent for ${clOrdId}`, 'success')
    } else {
      showToast(res?.error ?? 'Cancel failed', 'error')
    }
    refresh()
  }

  const isActive = (status: string) =>
    ['New', 'PartialFill', 'PendingNew', 'Pending'].includes(status)

  const columns = [
    { key: 'clOrdId', header: 'ClOrdID', render: (o: Order) => <span style={{ fontSize: 11 }}>{o.clOrdId}</span> },
    {
      key: 'symbol', header: 'Symbol', render: (o: Order) =>
        <span style={{ fontWeight: 600 }}>{o.symbol}</span>
    },
    {
      key: 'side', header: 'Side', render: (o: Order) =>
        <span className={o.side === 'buy' ? 'green' : 'red'}>{o.side.toUpperCase()}</span>
    },
    { key: 'type', header: 'Type', render: (o: Order) => o.type },
    { key: 'price', header: 'Price', render: (o: Order) => o.price?.toFixed(2) ?? '-' },
    { key: 'qty', header: 'Qty', render: (o: Order) => o.quantity },
    { key: 'filled', header: 'Filled', render: (o: Order) => o.filledQty },
    { key: 'leaves', header: 'Leaves', render: (o: Order) => o.leavesQty },
    { key: 'status', header: 'Status', render: (o: Order) => <StatusBadge status={o.status} /> },
    {
      key: 'actions', header: 'Actions', render: (o: Order) =>
        isActive(o.status) ? (
          <button
            className="btn btn-sm btn-danger"
            onClick={() => handleCancel(o.clOrdId)}
            disabled={!connected}
          >
            Cancel
          </button>
        ) : null
    },
  ]

  return (
    <div>
      <h1 className="page-title">Orders</h1>

      <div className="filter-row">
        <select value={filter} onChange={(e) => setFilter(e.target.value as 'all' | 'active')}>
          <option value="all">All Orders</option>
          <option value="active">Active Only</option>
        </select>
        <span style={{ color: 'var(--text-muted)', fontSize: 12 }}>
          {orders.length} order{orders.length !== 1 ? 's' : ''}
        </span>
      </div>

      <Table columns={columns} data={orders} emptyText="No orders yet" />
    </div>
  )
}
