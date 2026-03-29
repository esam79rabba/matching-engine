import { useState, useEffect, useCallback } from 'react'
import { apiGet } from '../api'
import type { PositionRecord } from '../types'
import Table from '../components/Table'

export default function Positions() {
  const [positions, setPositions] = useState<PositionRecord[]>([])

  const refresh = useCallback(async () => {
    const data = await apiGet<PositionRecord[]>('/api/positions')
    if (data) setPositions(data)
  }, [])

  useEffect(() => {
    refresh()
    const id = setInterval(refresh, 2000)
    return () => clearInterval(id)
  }, [refresh])

  const totalRealized = positions.reduce((s, p) => s + p.realizedPnl, 0)
  const totalUnrealized = positions.reduce((s, p) => s + p.unrealizedPnl, 0)

  const columns = [
    { key: 'symbol', header: 'Symbol', render: (p: PositionRecord) => <span style={{ fontWeight: 600 }}>{p.symbol}</span> },
    {
      key: 'net', header: 'Net Qty', render: (p: PositionRecord) =>
        <span className={p.netQty > 0 ? 'green' : p.netQty < 0 ? 'red' : ''}>
          {p.netQty > 0 ? '+' : ''}{p.netQty}
        </span>
    },
    { key: 'avg', header: 'Avg Price', render: (p: PositionRecord) => p.avgPrice.toFixed(2) },
    { key: 'last', header: 'Last Price', render: (p: PositionRecord) => p.lastPrice.toFixed(2) },
    {
      key: 'realized', header: 'Realized P&L', render: (p: PositionRecord) =>
        <span className={p.realizedPnl >= 0 ? 'green' : 'red'}>{p.realizedPnl.toFixed(2)}</span>
    },
    {
      key: 'unrealized', header: 'Unrealized P&L', render: (p: PositionRecord) =>
        <span className={p.unrealizedPnl >= 0 ? 'green' : 'red'}>{p.unrealizedPnl.toFixed(2)}</span>
    },
    {
      key: 'total', header: 'Total P&L', render: (p: PositionRecord) => {
        const total = p.realizedPnl + p.unrealizedPnl
        return <span className={total >= 0 ? 'green' : 'red'}>{total.toFixed(2)}</span>
      }
    },
  ]

  return (
    <div>
      <h1 className="page-title">Positions</h1>

      <div className="card-grid" style={{ maxWidth: 600 }}>
        <div className="card">
          <div className="card-label">Realized P&L</div>
          <div className={`card-value ${totalRealized >= 0 ? 'green' : 'red'}`}>
            {totalRealized.toFixed(2)}
          </div>
        </div>
        <div className="card">
          <div className="card-label">Unrealized P&L</div>
          <div className={`card-value ${totalUnrealized >= 0 ? 'green' : 'red'}`}>
            {totalUnrealized.toFixed(2)}
          </div>
        </div>
        <div className="card">
          <div className="card-label">Open Positions</div>
          <div className="card-value">{positions.filter((p) => p.netQty !== 0).length}</div>
        </div>
      </div>

      <Table columns={columns} data={positions} emptyText="No positions" />
    </div>
  )
}
