import { useEffect, useState, useCallback } from 'react'
import { apiGet, apiPost } from '../api'
import type { Instrument } from '../types'
import Table from '../components/Table'

interface Position {
  instrument_id: number
  net_quantity: number
  buy_quantity: number
  sell_quantity: number
}

export default function Positions({ canWrite, showToast }: { canWrite: boolean; showToast: (msg: string, type: 'success' | 'error') => void }) {
  const [instruments, setInstruments] = useState<Instrument[]>([])
  const [positions, setPositions] = useState<Position[]>([])
  const [participantId, setParticipantId] = useState('1')
  const [loading, setLoading] = useState(false)

  const symbolMap: Record<number, string> = {}
  for (const inst of instruments) {
    symbolMap[inst.instrument_id] = inst.symbol || `#${inst.instrument_id}`
  }

  const loadInstruments = useCallback(() => {
    apiGet<Instrument[]>('/api/v1/instruments').then((d) => setInstruments(d ?? []))
  }, [])

  const loadPositions = useCallback(() => {
    if (!participantId) {
      setPositions([])
      setLoading(false)
      return
    }
    apiGet<Position[]>(`/api/v1/accounts/${participantId}/positions`).then((d) => {
      setPositions(d ?? [])
      setLoading(false)
    })
  }, [participantId])

  useEffect(() => {
    loadInstruments()
  }, [loadInstruments])

  useEffect(() => {
    if (!participantId) {
      setPositions([])
      return
    }
    setLoading(true)
    loadPositions()
    const id = setInterval(loadPositions, 5000)
    return () => clearInterval(id)
  }, [participantId, loadPositions])

  const handleReset = async () => {
    if (!participantId) return
    const r = await apiPost<{ status?: string; error?: string }>(
      `/api/v1/accounts/${participantId}/positions/reset`,
    )
    if (r?.status) {
      showToast('Positions reset', 'success')
      loadPositions()
    } else {
      showToast(`Failed: ${r?.error || 'unknown'}`, 'error')
    }
  }

  const direction = (net: number) => {
    if (net > 0) return { label: 'Long', color: 'var(--green)' }
    if (net < 0) return { label: 'Short', color: 'var(--red)' }
    return { label: 'Flat', color: 'var(--text-muted)' }
  }

  return (
    <>
      <div className="page-title">Positions</div>

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
        {canWrite && participantId && (
          <button className="btn btn-danger" onClick={handleReset} style={{ marginBottom: 2 }}>
            Reset Positions
          </button>
        )}
      </div>

      {!participantId && <div className="empty">Enter a participant ID to view positions</div>}

      {loading ? (
        <div className="empty">Loading...</div>
      ) : participantId ? (
        <Table
          columns={[
            { header: 'Instrument', render: (p) => symbolMap[p.instrument_id] || `#${p.instrument_id}` },
            { header: 'Net Qty', render: (p) => {
              const d = direction(p.net_quantity)
              return <span style={{ color: d.color, fontWeight: 600 }}>{p.net_quantity}</span>
            }},
            { header: 'Buy Qty', render: (p) => p.buy_quantity },
            { header: 'Sell Qty', render: (p) => p.sell_quantity },
            { header: 'Direction', render: (p) => {
              const d = direction(p.net_quantity)
              return <span style={{ color: d.color }}>{d.label}</span>
            }},
          ]}
          data={positions}
          emptyMessage="No positions"
        />
      ) : null}
    </>
  )
}
