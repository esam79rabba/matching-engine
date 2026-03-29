import { useEffect, useState, useCallback } from 'react'
import { apiGet, apiPost } from '../api'
import type { Instrument } from '../types'
import Table from '../components/Table'
import StatusBadge from '../components/StatusBadge'

interface OrderResponse {
  order_id?: string
  status?: string
  rejection_reason?: string
  error?: string
}

interface Order {
  order_id: string
  instrument_id: number
  side: string
  order_type: string
  price: number
  quantity: number
  time_in_force: string
  status: string
  timestamp: string
}

const ORDER_TYPES = ['Limit', 'Market', 'Stop', 'StopLimit', 'TrailingStop']
const TIF_OPTIONS = ['Day', 'GTC', 'GTD', 'IOC', 'FOK', 'AuctionOnly', 'MOC', 'MOO']

export default function OrderEntry({ canWrite, showToast }: { canWrite: boolean; showToast: (msg: string, type: 'success' | 'error') => void }) {
  const [instruments, setInstruments] = useState<Instrument[]>([])
  const [orders, setOrders] = useState<Order[]>([])
  const [instrumentId, setInstrumentId] = useState('')
  const [side, setSide] = useState<'Buy' | 'Sell'>('Buy')
  const [orderType, setOrderType] = useState('Limit')
  const [price, setPrice] = useState('')
  const [quantity, setQuantity] = useState('')
  const [tif, setTif] = useState('Day')
  const [participantId, setParticipantId] = useState('1')
  const [expiryDate, setExpiryDate] = useState('')
  const [stopPrice, setStopPrice] = useState('')
  const [submitting, setSubmitting] = useState(false)
  const [result, setResult] = useState<{ ok: boolean; msg: string } | null>(null)

  const symbolMap = useCallback(
    () => {
      const map: Record<number, string> = {}
      for (const inst of instruments) {
        map[inst.instrument_id] = inst.symbol || `#${inst.instrument_id}`
      }
      return map
    },
    [instruments],
  )

  const loadInstruments = useCallback(() => {
    apiGet<Instrument[]>('/api/v1/instruments').then((d) => setInstruments(d ?? []))
  }, [])

  const loadOrders = useCallback(() => {
    if (!participantId) return
    apiGet<Order[]>(`/api/v1/orders?participant_id=${participantId}`).then((d) => setOrders(d ?? []))
  }, [participantId])

  useEffect(() => {
    loadInstruments()
  }, [loadInstruments])

  useEffect(() => {
    loadOrders()
    const id = setInterval(loadOrders, 5000)
    return () => clearInterval(id)
  }, [loadOrders])

  const needsPrice = orderType === 'Limit' || orderType === 'Stop' || orderType === 'StopLimit'
  const needsStopPrice = orderType === 'Stop' || orderType === 'StopLimit'
  const needsExpiry = tif === 'GTD'

  const handleSubmit = async () => {
    if (!instrumentId || !quantity) {
      showToast('Instrument and quantity are required', 'error')
      return
    }
    if (needsPrice && !price) {
      showToast('Price is required for this order type', 'error')
      return
    }
    setSubmitting(true)
    setResult(null)
    const body: Record<string, unknown> = {
      instrument_id: parseInt(instrumentId),
      side: side,
      type: orderType,
      quantity: parseInt(quantity),
      tif: tif,
      participant_id: parseInt(participantId) || 1,
    }
    if (needsPrice) {
      body.price = parseFloat(price)
    }
    if (needsStopPrice && stopPrice) {
      body.stop_price = parseFloat(stopPrice)
    }
    if (needsExpiry && expiryDate) {
      // Convert datetime-local to nanoseconds since epoch
      body.expiry_time = new Date(expiryDate).getTime() * 1000000
    }
    const r = await apiPost<OrderResponse>('/api/v1/orders', body)
    setSubmitting(false)
    if (r?.order_id) {
      setResult({ ok: true, msg: `Order accepted: ${r.order_id}` })
      showToast(`Order ${r.order_id} submitted`, 'success')
      loadOrders()
    } else {
      const reason = r?.rejection_reason || r?.error || 'Unknown error'
      setResult({ ok: false, msg: `Rejected: ${reason}` })
      showToast(`Order rejected: ${reason}`, 'error')
    }
  }

  const sMap = symbolMap()

  return (
    <>
      <div className="page-title">Order Entry</div>

      <div className="order-entry-form">
        <div className="form-grid" style={{ gap: 10 }}>
          <div className="form-field">
            <label>Instrument</label>
            <select value={instrumentId} onChange={(e) => setInstrumentId(e.target.value)}>
              <option value="">-- Select --</option>
              {instruments.map((inst) => (
                <option key={inst.instrument_id} value={inst.instrument_id}>
                  {inst.symbol || `#${inst.instrument_id}`}
                </option>
              ))}
            </select>
          </div>

          <div className="form-field">
            <label>Side</label>
            <div style={{ display: 'flex', gap: 4 }}>
              <button
                className={`btn ${side === 'Buy' ? 'btn-success' : ''}`}
                onClick={() => setSide('Buy')}
                style={{ flex: 1 }}
              >
                Buy
              </button>
              <button
                className={`btn ${side === 'Sell' ? 'btn-danger' : ''}`}
                onClick={() => setSide('Sell')}
                style={{ flex: 1 }}
              >
                Sell
              </button>
            </div>
          </div>

          <div className="form-field">
            <label>Order Type</label>
            <select value={orderType} onChange={(e) => setOrderType(e.target.value)}>
              {ORDER_TYPES.map((t) => (
                <option key={t} value={t}>{t}</option>
              ))}
            </select>
          </div>

          <div className="form-field">
            <label>Time in Force</label>
            <select value={tif} onChange={(e) => setTif(e.target.value)}>
              {TIF_OPTIONS.map((t) => (
                <option key={t} value={t}>{t}</option>
              ))}
            </select>
          </div>

          {needsPrice && (
            <div className="form-field">
              <label>Price</label>
              <input
                type="number"
                step="0.01"
                value={price}
                onChange={(e) => setPrice(e.target.value)}
                placeholder="0.00"
              />
            </div>
          )}

          {needsStopPrice && (
            <div className="form-field">
              <label>Stop Price</label>
              <input
                type="number"
                step="0.01"
                value={stopPrice}
                onChange={(e) => setStopPrice(e.target.value)}
                placeholder="Trigger price"
              />
            </div>
          )}

          {needsExpiry && (
            <div className="form-field">
              <label>Expiry Date & Time</label>
              <input
                type="datetime-local"
                value={expiryDate}
                onChange={(e) => setExpiryDate(e.target.value)}
                min={new Date().toISOString().slice(0, 16)}
                style={{ colorScheme: 'dark' }}
              />
            </div>
          )}

          <div className="form-field">
            <label>Quantity</label>
            <input
              type="number"
              value={quantity}
              onChange={(e) => setQuantity(e.target.value)}
              placeholder="0"
            />
          </div>

          <div className="form-field">
            <label>Participant ID</label>
            <input
              type="number"
              value={participantId}
              onChange={(e) => setParticipantId(e.target.value)}
              placeholder="1"
            />
          </div>
        </div>

        <div style={{ marginTop: 12, display: 'flex', alignItems: 'center', gap: 12 }}>
          <button
            className="btn btn-success"
            onClick={handleSubmit}
            disabled={submitting || !canWrite}
          >
            {submitting ? 'Submitting...' : 'Submit Order'}
          </button>
          {result && (
            <span style={{ color: result.ok ? 'var(--green)' : 'var(--red)', fontSize: 13 }}>
              {result.msg}
            </span>
          )}
          {!canWrite && (
            <span style={{ color: 'var(--text-muted)', fontSize: 12 }}>Write access required</span>
          )}
        </div>
      </div>

      <div className="section-title">Recent Orders</div>
      <Table
        columns={[
          { header: 'Order ID', render: (o) => o.order_id },
          { header: 'Instrument', render: (o) => sMap[o.instrument_id] || `#${o.instrument_id}` },
          { header: 'Side', render: (o) => {
            const s = o.side === 1 || o.side === 'Buy' ? 'Buy' : o.side === 2 || o.side === 'Sell' ? 'Sell' : String(o.side)
            return <span style={{ color: s === 'Buy' ? 'var(--green)' : 'var(--red)', fontWeight: 600 }}>{s}</span>
          }},
          { header: 'Type', render: (o) => {
            const types: Record<number, string> = {1:'Market',2:'Limit',3:'Stop',4:'StopLimit',5:'TrailingStop'}
            return types[o.order_type as number] || o.order_type || o.type || '-'
          }},
          { header: 'Price', render: (o) => o.price || '-' },
          { header: 'Qty', render: (o) => o.quantity },
          { header: 'TIF', render: (o) => {
            const tifs: Record<number, string> = {0:'Day',1:'GTC',2:'GTD',3:'IOC',4:'FOK',5:'AuctionOnly',6:'MOC',7:'MOO'}
            return tifs[o.time_in_force as number] || o.tif || o.time_in_force || '-'
          }},
          {
            header: 'Status',
            render: (o) => {
              const events: Record<number, string> = {0:'New',1:'Cancel',2:'Fill',3:'Reject',4:'Modify'}
              const label = events[o.event as number] || o.status || 'Resting'
              const v = label === 'Fill' || label === 'Filled' ? 'green'
                : label === 'Reject' || label === 'Rejected' ? 'red'
                : label === 'Cancel' || label === 'Cancelled' ? 'amber'
                : 'blue'
              return <StatusBadge text={label} variant={v} />
            },
          },
          { header: 'Time', render: (o) => {
            const ts = o.timestamp as number
            if (!ts || ts === 0) return '-'
            const ms = ts > 1e15 ? ts / 1e6 : ts  // nanoseconds to milliseconds
            return new Date(ms).toLocaleString()
          }},
        ]}
        data={orders}
        emptyMessage="No recent orders"
      />
    </>
  )
}
