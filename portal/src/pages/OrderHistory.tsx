import { useEffect, useState, useCallback } from 'react'
import { apiGet } from '../api'
import type { Instrument } from '../types'
import Table from '../components/Table'
import StatusBadge from '../components/StatusBadge'

interface Order {
  order_id: string
  timestamp: string
  instrument_id: number
  side: string
  order_type: string
  price: number
  quantity: number
  status: string
  participant_id?: number
}

export default function OrderHistory() {
  const [instruments, setInstruments] = useState<Instrument[]>([])
  const [orders, setOrders] = useState<Order[]>([])
  const [participantId, setParticipantId] = useState('1')
  const [limit, setLimit] = useState(50)
  const [loading, setLoading] = useState(true)

  const symbolMap: Record<number, string> = {}
  for (const inst of instruments) {
    symbolMap[inst.instrument_id] = inst.symbol || `#${inst.instrument_id}`
  }

  const loadInstruments = useCallback(() => {
    apiGet<Instrument[]>('/api/v1/instruments').then((d) => setInstruments(d ?? []))
  }, [])

  const loadOrders = useCallback(() => {
    if (!participantId) {
      setOrders([])
      setLoading(false)
      return
    }
    let path = `/api/v1/orders?participant_id=${participantId}`
    if (limit) path += `&limit=${limit}`
    apiGet<Order[]>(path).then((d) => {
      setOrders(d ?? [])
      setLoading(false)
    })
  }, [participantId, limit])

  useEffect(() => {
    loadInstruments()
  }, [loadInstruments])

  useEffect(() => {
    setLoading(true)
    loadOrders()
    const id = setInterval(loadOrders, 5000)
    return () => clearInterval(id)
  }, [loadOrders])

  const statusVariant = (status: string): 'green' | 'red' | 'amber' | 'blue' => {
    if (status === 'Filled') return 'green'
    if (status === 'Rejected') return 'red'
    if (status === 'Cancelled') return 'amber'
    return 'blue'
  }

  return (
    <>
      <div className="page-title">Order History</div>

      <div style={{ display: 'flex', gap: 12, marginBottom: 16, alignItems: 'flex-end', flexWrap: 'wrap' }}>
        <div className="form-field">
          <label>Participant ID</label>
          <input
            type="number"
            value={participantId}
            onChange={(e) => setParticipantId(e.target.value)}
            placeholder="1"
            style={{ width: 100 }}
          />
        </div>
        <div className="form-field">
          <label>Limit</label>
          <select value={limit} onChange={(e) => setLimit(Number(e.target.value))}>
            <option value={25}>25</option>
            <option value={50}>50</option>
            <option value={100}>100</option>
            <option value={500}>500</option>
          </select>
        </div>
      </div>

      {loading ? (
        <div className="empty">Loading...</div>
      ) : (
        <Table
          columns={[
            { header: 'Order ID', render: (o) => o.order_id },
            { header: 'Time', render: (o) => o.timestamp },
            { header: 'Instrument', render: (o) => symbolMap[o.instrument_id] || `#${o.instrument_id}` },
            { header: 'Side', render: (o) => (
              <span style={{ color: o.side === 'Buy' ? 'var(--green)' : 'var(--red)' }}>{o.side}</span>
            )},
            { header: 'Type', render: (o) => o.order_type },
            { header: 'Price', render: (o) => o.price || '-' },
            { header: 'Qty', render: (o) => o.quantity },
            { header: 'Status', render: (o) => <StatusBadge text={o.status} variant={statusVariant(o.status)} /> },
          ]}
          data={orders}
          emptyMessage="No orders found"
        />
      )}
    </>
  )
}
