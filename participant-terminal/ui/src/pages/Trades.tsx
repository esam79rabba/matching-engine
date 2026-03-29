import { useState, useEffect, useCallback } from 'react'
import { apiGet } from '../api'
import type { TradeRecord } from '../types'
import Table from '../components/Table'

export default function Trades() {
  const [trades, setTrades] = useState<TradeRecord[]>([])

  const refresh = useCallback(async () => {
    const data = await apiGet<TradeRecord[]>('/api/trades')
    if (data) setTrades(data)
  }, [])

  useEffect(() => {
    refresh()
    const id = setInterval(refresh, 2000)
    return () => clearInterval(id)
  }, [refresh])

  const totalPnl = trades.reduce((sum, t) => {
    const sign = t.side === 'sell' ? 1 : -1
    return sum + sign * t.price * t.quantity - t.fee
  }, 0)

  const columns = [
    { key: 'id', header: 'Trade ID', render: (t: TradeRecord) => <span style={{ fontSize: 11 }}>{t.tradeId}</span> },
    {
      key: 'time', header: 'Time', render: (t: TradeRecord) => {
        try { return new Date(t.timestamp).toLocaleTimeString() } catch { return t.timestamp }
      }
    },
    { key: 'symbol', header: 'Symbol', render: (t: TradeRecord) => <span style={{ fontWeight: 600 }}>{t.symbol}</span> },
    {
      key: 'side', header: 'Side', render: (t: TradeRecord) =>
        <span className={t.side === 'buy' ? 'green' : 'red'}>{t.side.toUpperCase()}</span>
    },
    { key: 'price', header: 'Price', render: (t: TradeRecord) => t.price.toFixed(2) },
    { key: 'qty', header: 'Qty', render: (t: TradeRecord) => t.quantity },
    { key: 'notional', header: 'Notional', render: (t: TradeRecord) => (t.price * t.quantity).toFixed(2) },
    { key: 'fee', header: 'Fee', render: (t: TradeRecord) => t.fee.toFixed(4) },
  ]

  return (
    <div>
      <h1 className="page-title">Trades</h1>

      <div className="card-grid" style={{ maxWidth: 400 }}>
        <div className="card">
          <div className="card-label">Total Fills</div>
          <div className="card-value">{trades.length}</div>
        </div>
        <div className="card">
          <div className="card-label">Running P&L</div>
          <div className={`card-value ${totalPnl >= 0 ? 'green' : 'red'}`}>
            {totalPnl.toFixed(2)}
          </div>
        </div>
      </div>

      <Table columns={columns} data={trades} emptyText="No fills yet" />
    </div>
  )
}
