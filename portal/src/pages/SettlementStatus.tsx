import { useEffect, useState, useCallback } from 'react'
import { apiGet, apiPost } from '../api'
import Table from '../components/Table'
import StatusBadge from '../components/StatusBadge'

interface SettlementEvent {
  trade_ids: string[]
  status: string
  settlement_date: string
  amount?: number
}

export default function SettlementStatus({ canWrite, showToast }: { canWrite: boolean; showToast: (msg: string, type: 'success' | 'error') => void }) {
  const [events, setEvents] = useState<SettlementEvent[]>([])
  const [loading, setLoading] = useState(true)
  const [notifyBody, setNotifyBody] = useState('')
  const [adjustPid, setAdjustPid] = useState('')
  const [adjustAmount, setAdjustAmount] = useState('')

  const loadEvents = useCallback(() => {
    apiGet<SettlementEvent[]>('/api/v1/settlement/status').then((d) => {
      setEvents(Array.isArray(d) ? d : [])
      setLoading(false)
    })
  }, [])

  useEffect(() => {
    loadEvents()
    const id = setInterval(loadEvents, 10000)
    return () => clearInterval(id)
  }, [loadEvents])

  const handleNotify = async () => {
    let body: unknown = undefined
    if (notifyBody.trim()) {
      try {
        body = JSON.parse(notifyBody)
      } catch {
        showToast('Invalid JSON', 'error')
        return
      }
    }
    const r = await apiPost<{ status?: string; error?: string }>('/api/v1/settlement/notify', body)
    if (r?.status) {
      showToast('Settlement notification sent', 'success')
      setNotifyBody('')
      loadEvents()
    } else {
      showToast(`Failed: ${r?.error || 'unknown'}`, 'error')
    }
  }

  const handleAdjustCredit = async () => {
    if (!adjustPid || !adjustAmount) {
      showToast('Participant ID and amount are required', 'error')
      return
    }
    const r = await apiPost<{ status?: string; error?: string }>('/api/v1/settlement/adjust-credit', {
      participant_id: parseInt(adjustPid),
      amount: parseFloat(adjustAmount),
    })
    if (r?.status) {
      showToast('Credit adjusted', 'success')
      setAdjustPid('')
      setAdjustAmount('')
      loadEvents()
    } else {
      showToast(`Failed: ${r?.error || 'unknown'}`, 'error')
    }
  }

  const statusVariant = (status: string): 'green' | 'amber' | 'blue' | 'red' => {
    if (status === 'Settled') return 'green'
    if (status === 'Pending') return 'amber'
    if (status === 'Failed') return 'red'
    return 'blue'
  }

  return (
    <>
      <div className="page-title">Settlement Status</div>

      {loading ? (
        <div className="empty">Loading...</div>
      ) : (
        <Table
          columns={[
            { header: 'Trade IDs', render: (e) => (e.trade_ids ?? []).join(', ') || '-' },
            { header: 'Status', render: (e) => <StatusBadge text={e.status} variant={statusVariant(e.status)} /> },
            { header: 'Settlement Date', render: (e) => e.settlement_date || '-' },
            { header: 'Amount', render: (e) => e.amount != null ? e.amount.toLocaleString() : '-' },
          ]}
          data={events}
          emptyMessage="No settlement events. Settlement data comes from the external settlement system."
        />
      )}

      {canWrite && (
        <>
          <div className="section-title">Settlement Notify</div>
          <div style={{ display: 'flex', gap: 12, alignItems: 'flex-end', flexWrap: 'wrap', marginBottom: 24 }}>
            <div className="form-field" style={{ flex: 1, maxWidth: 400 }}>
              <label>Notification Body (JSON, optional)</label>
              <input
                value={notifyBody}
                onChange={(e) => setNotifyBody(e.target.value)}
                placeholder='{"trade_ids": ["T001"]}'
              />
            </div>
            <button className="btn btn-success" onClick={handleNotify} style={{ marginBottom: 2 }}>
              Send Notification
            </button>
          </div>

          <div className="section-title">Adjust Credit</div>
          <div style={{ display: 'flex', gap: 12, alignItems: 'flex-end', flexWrap: 'wrap' }}>
            <div className="form-field">
              <label>Participant ID</label>
              <input
                type="number"
                value={adjustPid}
                onChange={(e) => setAdjustPid(e.target.value)}
                placeholder="1"
                style={{ width: 100 }}
              />
            </div>
            <div className="form-field">
              <label>Amount</label>
              <input
                type="number"
                value={adjustAmount}
                onChange={(e) => setAdjustAmount(e.target.value)}
                placeholder="10000"
                style={{ width: 140 }}
              />
            </div>
            <button className="btn btn-success" onClick={handleAdjustCredit} style={{ marginBottom: 2 }}>
              Adjust Credit
            </button>
          </div>
        </>
      )}
    </>
  )
}
