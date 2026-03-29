import { useEffect, useState, useCallback } from 'react'
import { apiGet } from '../api'
import type { Instrument } from '../types'
import Table from '../components/Table'
import StatusBadge from '../components/StatusBadge'

interface AuditRecord {
  sequence: number
  type: string
  timestamp: string
  participant_id: number
  instrument_id: number
  order_id: string
  side: string
  price: number
  quantity: number
  hash: string
  hash_valid?: boolean
}

export default function TradeAuditTrail() {
  const [instruments, setInstruments] = useState<Instrument[]>([])
  const [records, setRecords] = useState<AuditRecord[]>([])
  const [filterParticipant, setFilterParticipant] = useState('')
  const [filterInstrument, setFilterInstrument] = useState('')
  const [filterType, setFilterType] = useState('')
  const [loading, setLoading] = useState(true)

  const symbolMap: Record<number, string> = {}
  for (const inst of instruments) {
    symbolMap[inst.instrument_id] = inst.symbol || `#${inst.instrument_id}`
  }

  const loadInstruments = useCallback(() => {
    apiGet<Instrument[]>('/api/v1/instruments').then((d) => setInstruments(d ?? []))
  }, [])

  const loadRecords = useCallback(() => {
    let path = '/api/v1/audit/trades'
    const params: string[] = []
    if (filterParticipant) params.push(`participant_id=${filterParticipant}`)
    if (filterInstrument) params.push(`instrument_id=${filterInstrument}`)
    if (filterType) params.push(`type=${filterType}`)
    if (params.length > 0) path += '?' + params.join('&')
    apiGet<AuditRecord[]>(path).then((d) => {
      if (d === null) {
        // Fallback to /api/v1/audit
        apiGet<AuditRecord[]>('/api/v1/audit').then((d2) => {
          setRecords(d2 ?? [])
          setLoading(false)
        })
      } else {
        setRecords(d)
        setLoading(false)
      }
    })
  }, [filterParticipant, filterInstrument, filterType])

  useEffect(() => {
    loadInstruments()
  }, [loadInstruments])

  useEffect(() => {
    setLoading(true)
    loadRecords()
    const id = setInterval(loadRecords, 5000)
    return () => clearInterval(id)
  }, [loadRecords])

  const EVENT_TYPES = ['New', 'Cancel', 'Fill', 'Reject', 'Modify']

  return (
    <>
      <div className="page-title">Trade Audit Trail</div>

      <div style={{ display: 'flex', gap: 12, marginBottom: 16, alignItems: 'flex-end', flexWrap: 'wrap' }}>
        <div className="form-field">
          <label>Participant</label>
          <input
            type="number"
            value={filterParticipant}
            onChange={(e) => setFilterParticipant(e.target.value)}
            placeholder="All"
            style={{ width: 100 }}
          />
        </div>
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
          <label>Type</label>
          <select value={filterType} onChange={(e) => setFilterType(e.target.value)}>
            <option value="">All</option>
            {EVENT_TYPES.map((t) => (
              <option key={t} value={t}>{t}</option>
            ))}
          </select>
        </div>
      </div>

      {loading ? (
        <div className="empty">Loading...</div>
      ) : (
        <Table
          columns={[
            { header: 'Seq', render: (r) => r.sequence },
            { header: 'Type', render: (r) => {
              const v = r.type === 'Fill' ? 'green'
                : r.type === 'Reject' ? 'red'
                : r.type === 'Cancel' ? 'amber'
                : 'blue'
              return <StatusBadge text={r.type} variant={v} />
            }},
            { header: 'Time', render: (r) => r.timestamp },
            { header: 'Participant', render: (r) => String(r.participant_id) },
            { header: 'Instrument', render: (r) => symbolMap[r.instrument_id] || `#${r.instrument_id}` },
            { header: 'Order ID', render: (r) => r.order_id },
            { header: 'Side', render: (r) => (
              <span style={{ color: r.side === 'Buy' ? 'var(--green)' : 'var(--red)' }}>{r.side}</span>
            )},
            { header: 'Price', render: (r) => r.price || '-' },
            { header: 'Qty', render: (r) => r.quantity },
            { header: 'Hash', render: (r) => (
              <span style={{ display: 'flex', alignItems: 'center', gap: 4 }}>
                <span style={{ fontFamily: 'var(--mono)', fontSize: 11 }}>
                  {r.hash ? r.hash.substring(0, 12) + '...' : '-'}
                </span>
                {r.hash_valid !== undefined && (
                  <span style={{ color: r.hash_valid ? 'var(--green)' : 'var(--red)' }}>
                    {r.hash_valid ? '\u2713' : '\u2717'}
                  </span>
                )}
              </span>
            )},
          ]}
          data={records}
          emptyMessage="No audit records"
        />
      )}
    </>
  )
}
