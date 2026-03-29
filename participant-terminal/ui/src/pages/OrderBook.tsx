import { useState, useEffect, useCallback } from 'react'
import { apiGet } from '../api'
import type { Instrument, OrderBookSnapshot } from '../types'

interface Props {
  onPriceClick?: (price: number, symbol: string, side: 'buy' | 'sell') => void
}

export default function OrderBook({ onPriceClick }: Props) {
  const [instruments, setInstruments] = useState<Instrument[]>([])
  const [selectedInst, setSelectedInst] = useState<number>(0)
  const [book, setBook] = useState<OrderBookSnapshot | null>(null)

  const loadInstruments = useCallback(async () => {
    const data = await apiGet<Instrument[]>('/api/instruments')
    if (data && data.length > 0) {
      setInstruments(data)
      if (selectedInst === 0) setSelectedInst(data[0].instrument_id)
    }
  }, [selectedInst])

  useEffect(() => {
    loadInstruments()
  }, [loadInstruments])

  const loadBook = useCallback(async () => {
    if (!selectedInst) return
    const data = await apiGet<OrderBookSnapshot>(`/api/book/${selectedInst}`)
    if (data) setBook(data)
  }, [selectedInst])

  useEffect(() => {
    loadBook()
    const id = setInterval(loadBook, 2000)
    return () => clearInterval(id)
  }, [loadBook])

  const bids = book?.bids ?? []
  const asks = book?.asks ?? []
  const maxQty = Math.max(
    ...bids.map((b) => b.quantity),
    ...asks.map((a) => a.quantity),
    1
  )

  const bestBid = bids.length > 0 ? bids[0].price : 0
  const bestAsk = asks.length > 0 ? asks[0].price : 0
  const spread = bestBid > 0 && bestAsk > 0 ? (bestAsk - bestBid).toFixed(2) : '-'
  const selectedSymbol = instruments.find((i) => i.instrument_id === selectedInst)?.symbol ?? ''

  return (
    <div>
      <h1 className="page-title">Order Book</h1>

      <div className="filter-row">
        <select
          value={selectedInst}
          onChange={(e) => setSelectedInst(parseInt(e.target.value))}
        >
          {instruments.map((inst) => (
            <option key={inst.instrument_id} value={inst.instrument_id}>
              {inst.symbol}
            </option>
          ))}
        </select>
        <span className="ob-spread">Spread: {spread}</span>
      </div>

      <div className="ob-container">
        <div>
          <div className="ob-side-title green">Bids</div>
          {bids.length === 0 ? (
            <div className="empty">No bids</div>
          ) : (
            bids.map((level, i) => (
              <div
                key={i}
                className="ob-qty-bar"
                onClick={() => onPriceClick?.(level.price, selectedSymbol, 'sell')}
              >
                <div
                  className="ob-qty-fill ob-qty-bid"
                  style={{ width: `${(level.quantity / maxQty) * 100}%` }}
                />
                <div className="ob-qty-text">
                  <span className="green">{level.price.toFixed(2)}</span>
                  <span>{level.quantity}</span>
                </div>
              </div>
            ))
          )}
        </div>

        <div>
          <div className="ob-side-title red">Asks</div>
          {asks.length === 0 ? (
            <div className="empty">No asks</div>
          ) : (
            asks.map((level, i) => (
              <div
                key={i}
                className="ob-qty-bar"
                onClick={() => onPriceClick?.(level.price, selectedSymbol, 'buy')}
              >
                <div
                  className="ob-qty-fill ob-qty-ask"
                  style={{ width: `${(level.quantity / maxQty) * 100}%` }}
                />
                <div className="ob-qty-text">
                  <span className="red">{level.price.toFixed(2)}</span>
                  <span>{level.quantity}</span>
                </div>
              </div>
            ))
          )}
        </div>
      </div>
    </div>
  )
}
