import { useEffect, useState, useCallback } from 'react'
import { apiGet, apiPost } from '../api'
import Card from '../components/Card'

interface CreditData {
  participant_id: number
  credit_limit: number
  current_exposure: number
  available_credit: number
}

export default function AccountCredit({ canWrite, showToast }: { canWrite: boolean; showToast: (msg: string, type: 'success' | 'error') => void }) {
  const [participantId, setParticipantId] = useState('1')
  const [credit, setCredit] = useState<CreditData | null>(null)
  const [newLimit, setNewLimit] = useState('')
  const [loading, setLoading] = useState(false)

  const loadCredit = useCallback(() => {
    if (!participantId) return
    apiGet<CreditData>(`/api/v1/accounts/${participantId}/credit`).then((d) => {
      setCredit(d)
      setLoading(false)
    })
  }, [participantId])

  useEffect(() => {
    if (!participantId) {
      setCredit(null)
      return
    }
    setLoading(true)
    loadCredit()
    const id = setInterval(loadCredit, 5000)
    return () => clearInterval(id)
  }, [participantId, loadCredit])

  const handleSetLimit = async () => {
    if (!participantId || !newLimit) {
      showToast('Participant ID and credit limit are required', 'error')
      return
    }
    const r = await apiPost<{ status?: string; error?: string }>(
      `/api/v1/accounts/${participantId}/credit-limit`,
      { credit_limit: parseFloat(newLimit) },
    )
    if (r?.status) {
      showToast(`Credit limit set to ${newLimit}`, 'success')
      setNewLimit('')
      loadCredit()
    } else {
      showToast(`Failed: ${r?.error || 'unknown'}`, 'error')
    }
  }

  const handleReset = async () => {
    if (!participantId) return
    const r = await apiPost<{ status?: string; error?: string }>(
      `/api/v1/accounts/${participantId}/credit-limit/reset`,
    )
    if (r?.status) {
      showToast('Exposure reset', 'success')
      loadCredit()
    } else {
      showToast(`Failed: ${r?.error || 'unknown'}`, 'error')
    }
  }

  const usagePct = credit && credit.credit_limit > 0
    ? Math.min((credit.current_exposure / credit.credit_limit) * 100, 100)
    : 0
  const usageColor = usagePct > 80 ? 'var(--red)' : usagePct > 50 ? 'var(--amber)' : 'var(--green)'

  return (
    <>
      <div className="page-title">Credit / Exposure</div>

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
      </div>

      {loading && !credit && <div className="empty">Loading...</div>}

      {credit && (
        <>
          <div className="card-grid">
            <Card label="Credit Limit" value={credit.credit_limit.toLocaleString()} />
            <Card
              label="Current Exposure"
              value={credit.current_exposure.toLocaleString()}
              colorClass={usagePct > 80 ? 'red' : usagePct > 50 ? 'amber' : ''}
            />
            <Card
              label="Available Credit"
              value={credit.available_credit.toLocaleString()}
              colorClass="green"
            />
          </div>

          {/* Usage bar */}
          <div style={{ marginBottom: 24 }}>
            <div style={{ fontSize: 12, color: 'var(--text-muted)', marginBottom: 4 }}>
              Usage: {usagePct.toFixed(1)}%
            </div>
            <div style={{
              background: 'var(--bg)',
              borderRadius: 4,
              height: 8,
              overflow: 'hidden',
              border: '1px solid var(--border)',
            }}>
              <div style={{
                width: `${usagePct}%`,
                height: '100%',
                background: usageColor,
                borderRadius: 4,
                transition: 'width 0.3s',
              }} />
            </div>
          </div>
        </>
      )}

      {!participantId && <div className="empty">Enter a participant ID to view credit</div>}

      {canWrite && participantId && (
        <div style={{ marginTop: 16 }}>
          <div className="section-title">Admin Actions</div>
          <div style={{ display: 'flex', gap: 12, alignItems: 'flex-end', flexWrap: 'wrap' }}>
            <div className="form-field">
              <label>Set Credit Limit</label>
              <input
                type="number"
                value={newLimit}
                onChange={(e) => setNewLimit(e.target.value)}
                placeholder="100000"
                style={{ width: 140 }}
              />
            </div>
            <button className="btn btn-success" onClick={handleSetLimit} style={{ marginBottom: 2 }}>
              Set Limit
            </button>
            <button className="btn btn-danger" onClick={handleReset} style={{ marginBottom: 2 }}>
              Reset Exposure
            </button>
          </div>
        </div>
      )}
    </>
  )
}
