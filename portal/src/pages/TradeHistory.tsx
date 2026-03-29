import { useEffect, useState, useCallback } from 'react'
import { apiGet } from '../api'
import type { Instrument } from '../types'
import Table from '../components/Table'

interface Trade {
  trade_id: string
  timestamp: string
  instrument_id: number
  side: string
  price: number
  quantity: number
  buyer_fee: number
  seller_fee: number
  buyer_participant_id?: number
  seller_participant_id?: number
}

export default function TradeHistory() {
  const [instruments, setInstruments] = useState<Instrument[]>([])
  const [trades, setTrades] = useState<Trade[]>([])
  const [filterInstrument, setFilterInstrument] = useState('')
  const [filterParticipant, setFilterParticipant] = useState('')
  const [limit, setLimit] = useState(50)
  const [loading, setLoading] = useState(true)

  const symbolMap: Record<number, string> = {}
  for (const inst of instruments) {
    symbolMap[inst.instrument_id] = inst.symbol || `#${inst.instrument_id}`
  }

  const loadInstruments = useCallback(() => {
    apiGet<Instrument[]>('/api/v1/instruments').then((d) => setInstruments(Array.isArray(d) ? d : []))
  }, [])

  const loadTrades = useCallback(() => {
    let path = `/api/v1/trades?limit=${limit}`
    if (filterInstrument) path += `&instrument_id=${filterInstrument}`
    if (filterParticipant) path += `&participant_id=${filterParticipant}`
    apiGet<Trade[]>(path).then((d) => {
      setTrades(Array.isArray(d) ? d : [])
      setLoading(false)
    })
  }, [limit, filterInstrument, filterParticipant])

  useEffect(() => {
    loadInstruments()
  }, [loadInstruments])

  useEffect(() => {
    setLoading(true)
    loadTrades()
    const id = setInterval(loadTrades, 5000)
    return () => clearInterval(id)
  }, [loadTrades])

  const handleExportCSV = () => {
    const header = 'Trade ID,Time,Instrument,Side,Price,Qty,Buyer Fee,Seller Fee'
    const rows = trades.map((t) =>
      `${t.trade_id},${t.timestamp},${symbolMap[t.instrument_id] || t.instrument_id},${t.side},${t.price},${t.quantity},${t.buyer_fee},${t.seller_fee}`
    )
    const csv = [header, ...rows].join('\n')
    navigator.clipboard.writeText(csv).catch(() => {})
  }

  return (
    <>
      <div className="page-title">Trade History</div>

      <div style={{ display: 'flex', gap: 12, marginBottom: 16, alignItems: 'flex-end', flexWrap: 'wrap' }}>
        <div className="form-field">
          <label>Instrument</label>
          <select value={filterInstrument} onChange={(e) => setFilterInstrument(e.target.value)}>
            <option value="">All</option>
            {instruments.map((inst) => (
              <option key={inst.instrument_id} value={inst.instrument_id}>
                {inst.symbol || `#${inst.instrument_id}`}
              </option>
            ))}
          </select>
        </div>
        <div className="form-field">
          <label>Participant ID</label>
          <input
            type="number"
            value={filterParticipant}
            onChange={(e) => setFilterParticipant(e.target.value)}
            placeholder="All"
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
        <button className="btn" onClick={handleExportCSV} style={{ marginBottom: 2 }}>
          Copy CSV
        </button>
      </div>

      {loading ? (
        <div className="empty">Loading...</div>
      ) : (
        <Table
          columns={[
            { header: 'Trade ID', render: (t) => t.trade_id },
            { header: 'Time', render: (t) => t.timestamp },
            { header: 'Instrument', render: (t) => symbolMap[t.instrument_id] || `#${t.instrument_id}` },
            { header: 'Side', render: (t) => (
              <span style={{ color: t.side === 'Buy' ? 'var(--green)' : 'var(--red)' }}>{t.side}</span>
            )},
            { header: 'Price', render: (t) => t.price },
            { header: 'Qty', render: (t) => t.quantity },
            { header: 'Buyer Fee', render: (t) => t.buyer_fee ?? '-' },
            { header: 'Seller Fee', render: (t) => t.seller_fee ?? '-' },
          ]}
          data={trades}
          emptyMessage="No trades found"
        />
      )}
    </>
  )
}
