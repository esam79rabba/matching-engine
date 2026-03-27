import { useEffect, useState, useCallback } from 'react'
import { apiGet, apiPost } from '../api'
import type { KillSwitchResponse } from '../types'
import Card from '../components/Card'
import StatusBadge from '../components/StatusBadge'
import Modal from '../components/Modal'

interface KillSwitchProps {
  canWrite: boolean
  showToast: (msg: string, type: 'success' | 'error') => void
}

interface ConfirmState {
  title: string
  message: string
  action: () => Promise<void>
}

export default function KillSwitch({ canWrite, showToast }: KillSwitchProps) {
  const [data, setData] = useState<KillSwitchResponse | null>(null)
  const [pid, setPid] = useState('')
  const [confirm, setConfirm] = useState<ConfirmState | null>(null)

  const load = useCallback(() => {
    apiGet<KillSwitchResponse>('/api/v1/kill-switch').then(setData)
  }, [])

  useEffect(() => {
    load()
    const id = setInterval(load, 5000)
    return () => clearInterval(id)
  }, [load])

  if (data?.error) {
    return (
      <>
        <div className="page-title">Kill Switch</div>
        <div className="card">
          <div className="card-label">Status</div>
          <div className="card-value amber">Not configured</div>
        </div>
      </>
    )
  }

  const blocked = data?.blocked_participants ?? []

  const handleActivate = () => {
    if (!pid) return
    setConfirm({
      title: 'ACTIVATE Kill Switch',
      message: `This will BLOCK ALL ORDERS from participant ${pid} and CANCEL all their resting orders. This is a high-impact action. Continue?`,
      action: async () => {
        const r = await apiPost<{ action?: string; error?: string }>(`/api/v1/kill-switch/${pid}/activate`)
        if (r?.action) {
          showToast(`Kill switch activated for PID ${pid}`, 'success')
          setPid('')
          load()
        } else {
          showToast(`Activation failed: ${r?.error ?? 'unknown'}`, 'error')
        }
      },
    })
  }

  const handleDeactivate = (participantId: number) => {
    setConfirm({
      title: 'Deactivate Kill Switch',
      message: `Unblock participant ${participantId}? They will be able to submit new orders.`,
      action: async () => {
        const r = await apiPost<{ action?: string }>(`/api/v1/kill-switch/${participantId}/deactivate`)
        if (r?.action) {
          showToast(`Participant ${participantId} unblocked`, 'success')
          load()
        } else {
          showToast('Deactivate failed', 'error')
        }
      },
    })
  }

  return (
    <>
      <div className="page-title">Kill Switch</div>
      <div className="card-grid">
        <Card
          label="Blocked Participants"
          value={blocked.length}
          colorClass={blocked.length > 0 ? 'red' : 'green'}
        />
      </div>

      {blocked.length > 0 && (
        <div className="card" style={{ marginBottom: 16 }}>
          <div className="card-label">Currently Blocked</div>
          {blocked.map((participantId) => (
            <div key={participantId} className="blocked-row">
              <StatusBadge text={`PID ${participantId}`} variant="red" />
              {canWrite && (
                <button
                  className="btn btn-success"
                  style={{ fontSize: 11 }}
                  onClick={() => handleDeactivate(participantId)}
                >
                  Unblock
                </button>
              )}
            </div>
          ))}
        </div>
      )}

      {canWrite && (
        <div className="card ks-input-row">
          <span>Activate for participant ID:</span>
          <input
            type="number"
            placeholder="PID"
            value={pid}
            onChange={(e) => setPid(e.target.value)}
          />
          <button className="btn btn-danger" onClick={handleActivate}>
            Activate
          </button>
        </div>
      )}

      {confirm && (
        <Modal
          title={confirm.title}
          message={confirm.message}
          onConfirm={async () => { await confirm.action(); setConfirm(null) }}
          onCancel={() => setConfirm(null)}
        />
      )}
    </>
  )
}
