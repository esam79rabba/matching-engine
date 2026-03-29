import { useEffect, useState, useCallback } from 'react'
import { apiGet } from '../api'
import Table from '../components/Table'
import Card from '../components/Card'

interface ReconTrade {
  trade_id: string
  instrument_id: number
  price: number
  quantity: number
  buyer_participant_id: number
  seller_participant_id: number
  timestamp: string
  status?: string
}

interface ReconPosition {
  participant_id: number
  instrument_id: number
  net_quantity: number
  buy_quantity: number
  sell_quantity: number
  status?: string
}

export default function Reconciliation() {
  const [tab, setTab] = useState<'trades' | 'positions'>('trades')
  const [trades, setTrades] = useState<ReconTrade[]>([])
  const [positions, setPositions] = useState<ReconPosition[]>([])
  const [loading, setLoading] = useState(true)

  const loadTrades = useCallback(() => {
    apiGet<ReconTrade[]>('/api/v1/reconciliation/trades').then((d) => {
      setTrades(Array.isArray(d) ? d : [])
      setLoading(false)
    })
  }, [])

  const loadPositions = useCallback(() => {
    apiGet<ReconPosition[]>('/api/v1/reconciliation/positions').then((d) => {
      setPositions(Array.isArray(d) ? d : [])
      setLoading(false)
    })
  }, [])

  useEffect(() => {
    setLoading(true)
    if (tab === 'trades') {
      loadTrades()
      const id = setInterval(loadTrades, 10000)
      return () => clearInterval(id)
    } else {
      loadPositions()
      const id = setInterval(loadPositions, 10000)
      return () => clearInterval(id)
    }
  }, [tab, loadTrades, loadPositions])

  const totalTrades = trades.length
  const totalVolume = trades.reduce((sum, t) => sum + t.quantity, 0)
  const totalFees = 0 // Fees not in recon data

  return (
    <>
      <div className="page-title">Reconciliation</div>

      {/* Tab selector */}
      <div style={{ display: 'flex', gap: 4, marginBottom: 16 }}>
        <button
          className={`btn ${tab === 'trades' ? 'btn-success' : ''}`}
          onClick={() => setTab('trades')}
        >
          Trades
        </button>
        <button
          className={`btn ${tab === 'positions' ? 'btn-success' : ''}`}
          onClick={() => setTab('positions')}
        >
          Positions
        </button>
      </div>

      {tab === 'trades' && (
        <>
          <div className="card-grid">
            <Card label="Total Trades" value={totalTrades} />
            <Card label="Total Volume" value={totalVolume.toLocaleString()} />
            <Card label="Total Fees" value={totalFees.toLocaleString()} />
          </div>

          {loading ? (
            <div className="empty">Loading...</div>
          ) : (
            <Table
              columns={[
                { header: 'Trade ID', render: (t) => t.trade_id },
                { header: 'Instrument', render: (t) => `#${t.instrument_id}` },
                { header: 'Price', render: (t) => t.price },
                { header: 'Qty', render: (t) => t.quantity },
                { header: 'Buyer', render: (t) => String(t.buyer_participant_id) },
                { header: 'Seller', render: (t) => String(t.seller_participant_id) },
                { header: 'Time', render: (t) => t.timestamp },
                { header: 'Status', render: (t) => t.status || '-' },
              ]}
              data={trades}
              emptyMessage="No reconciliation trade data"
            />
          )}
        </>
      )}

      {tab === 'positions' && (
        <>
          {loading ? (
            <div className="empty">Loading...</div>
          ) : (
            <Table
              columns={[
                { header: 'Participant', render: (p) => String(p.participant_id) },
                { header: 'Instrument', render: (p) => `#${p.instrument_id}` },
                { header: 'Net Qty', render: (p) => p.net_quantity },
                { header: 'Buy Qty', render: (p) => p.buy_quantity },
                { header: 'Sell Qty', render: (p) => p.sell_quantity },
                { header: 'Status', render: (p) => p.status || '-' },
              ]}
              data={positions}
              emptyMessage="No reconciliation position data"
            />
          )}
        </>
      )}
    </>
  )
}
