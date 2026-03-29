import { useEffect, useState, useCallback } from 'react'
import { apiGet } from '../api'
import type { Instrument } from '../types'

interface BookLevel {
  price: number
  quantity: number
  order_count: number
}

interface BookData {
  bids: BookLevel[]
  asks: BookLevel[]
  last_traded_price?: number
}

export default function OrderBook() {
  const [instruments, setInstruments] = useState<Instrument[]>([])
  const [instrumentId, setInstrumentId] = useState('')
  const [book, setBook] = useState<BookData | null>(null)
  const [loading, setLoading] = useState(false)

  const loadInstruments = useCallback(() => {
    apiGet<Instrument[]>('/api/v1/instruments').then((d) => setInstruments(d ?? []))
  }, [])

  useEffect(() => {
    loadInstruments()
  }, [loadInstruments])

  const loadBook = useCallback(() => {
    if (!instrumentId) return
    apiGet<BookData>(`/api/v1/instruments/${instrumentId}/book`).then((d) => {
      setBook(d)
      setLoading(false)
    })
  }, [instrumentId])

  useEffect(() => {
    if (!instrumentId) {
      setBook(null)
      return
    }
    setLoading(true)
    loadBook()
    const id = setInterval(loadBook, 2000)
    return () => clearInterval(id)
  }, [instrumentId, loadBook])

  const selectedInst = instruments.find((i) => String(i.instrument_id) === instrumentId)
  const bids = book?.bids ?? []
  const asks = book?.asks ?? []
  const bestBid = bids.length > 0 ? bids[0].price : 0
  const bestAsk = asks.length > 0 ? asks[0].price : 0
  const spread = bestBid && bestAsk ? (bestAsk - bestBid).toFixed(2) : '-'
  const lastPrice = book?.last_traded_price ?? selectedInst?.last_traded_price ?? 0
  const maxQty = Math.max(
    ...bids.map((b) => b.quantity),
    ...asks.map((a) => a.quantity),
    1,
  )

  return (
    <>
      <div className="page-title">Order Book</div>

      <div style={{ marginBottom: 16, display: 'flex', alignItems: 'center', gap: 16 }}>
        <div className="form-field" style={{ minWidth: 200 }}>
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
        {instrumentId && (
          <div style={{ fontSize: 13, color: 'var(--text-muted)' }}>
            Auto-refresh: 2s
          </div>
        )}
      </div>

      {!instrumentId && (
        <div className="empty">Select an instrument to view order book</div>
      )}

      {instrumentId && loading && !book && (
        <div className="empty">Loading...</div>
      )}

      {instrumentId && book && (
        <>
          <div className="ob-header">
            <div className="ob-last-price">
              Last: <span style={{ color: 'var(--accent)', fontWeight: 700 }}>{lastPrice || '-'}</span>
            </div>
            <div className="ob-spread">
              Spread: <span style={{ fontWeight: 700 }}>{spread}</span>
            </div>
          </div>

          <div className="ob-container">
            {/* Bids */}
            <div className="ob-side">
              <div className="ob-side-title green">Bids</div>
              <table>
                <thead>
                  <tr>
                    <th>Orders</th>
                    <th>Qty</th>
                    <th>Price</th>
                  </tr>
                </thead>
                <tbody>
                  {bids.length === 0 && (
                    <tr><td colSpan={3} style={{ textAlign: 'center', color: 'var(--text-muted)' }}>No bids</td></tr>
                  )}
                  {bids.map((level, i) => (
                    <tr key={i}>
                      <td>{level.order_count}</td>
                      <td>
                        <div className="ob-qty-bar">
                          <div
                            className="ob-qty-fill ob-qty-bid"
                            style={{ width: `${(level.quantity / maxQty) * 100}%` }}
                          />
                          <span className="ob-qty-text">{level.quantity}</span>
                        </div>
                      </td>
                      <td className="green">{level.price.toFixed(2)}</td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>

            {/* Asks */}
            <div className="ob-side">
              <div className="ob-side-title red">Asks</div>
              <table>
                <thead>
                  <tr>
                    <th>Price</th>
                    <th>Qty</th>
                    <th>Orders</th>
                  </tr>
                </thead>
                <tbody>
                  {asks.length === 0 && (
                    <tr><td colSpan={3} style={{ textAlign: 'center', color: 'var(--text-muted)' }}>No asks</td></tr>
                  )}
                  {asks.map((level, i) => (
                    <tr key={i}>
                      <td className="red">{level.price.toFixed(2)}</td>
                      <td>
                        <div className="ob-qty-bar">
                          <div
                            className="ob-qty-fill ob-qty-ask"
                            style={{ width: `${(level.quantity / maxQty) * 100}%` }}
                          />
                          <span className="ob-qty-text">{level.quantity}</span>
                        </div>
                      </td>
                      <td>{level.order_count}</td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
          </div>
        </>
      )}
    </>
  )
}
