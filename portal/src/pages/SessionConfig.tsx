import { useEffect, useState, useCallback } from 'react'
import {
  getSessionConfigs,
  addSessionConfig,
  updateSessionConfig,
  deleteSessionConfig,
  enableSession,
  disableSession,
  resetSequences,
} from '../api'
import type { SessionConfig as SessionConfigType } from '../types'
import Table from '../components/Table'
import StatusBadge from '../components/StatusBadge'
import Modal from '../components/Modal'

interface SessionConfigProps {
  showToast: (msg: string, type: 'success' | 'error') => void
}

interface ConfirmState {
  title: string
  message: string
  action: () => Promise<void>
}

const FIX_VERSIONS = ['FIX.4.2', 'FIX.4.4', 'FIXT.1.1']

const defaultForm = {
  sender_comp_id: '',
  target_comp_id: 'EXCHANGE',
  fix_version: 'FIX.4.2',
  heartbeat_interval: '30',
  participant_id: '',
  description: '',
}

export default function SessionConfig({ showToast }: SessionConfigProps) {
  const [sessions, setSessions] = useState<SessionConfigType[]>([])
  const [form, setForm] = useState(defaultForm)
  const [editingId, setEditingId] = useState<string | null>(null)
  const [showForm, setShowForm] = useState(false)
  const [confirm, setConfirm] = useState<ConfirmState | null>(null)

  const load = useCallback(() => {
    getSessionConfigs().then((d) => setSessions(d ?? []))
  }, [])

  useEffect(() => {
    load()
    const id = setInterval(load, 5000)
    return () => clearInterval(id)
  }, [load])

  const setField = (field: string, value: string) => {
    setForm((prev) => ({ ...prev, [field]: value }))
  }

  const openAddForm = () => {
    setForm(defaultForm)
    setEditingId(null)
    setShowForm(true)
  }

  const openEditForm = (s: SessionConfigType) => {
    setForm({
      sender_comp_id: s.sender_comp_id,
      target_comp_id: s.target_comp_id,
      fix_version: s.fix_version,
      heartbeat_interval: String(s.heartbeat_interval),
      participant_id: String(s.participant_id || ''),
      description: s.description,
    })
    setEditingId(s.sender_comp_id)
    setShowForm(true)
  }

  const handleSave = () => {
    const body = {
      sender_comp_id: form.sender_comp_id,
      target_comp_id: form.target_comp_id || 'EXCHANGE',
      fix_version: form.fix_version,
      heartbeat_interval: parseInt(form.heartbeat_interval) || 30,
      participant_id: parseInt(form.participant_id) || 0,
      description: form.description,
      enabled: true,
    }

    if (!body.sender_comp_id) {
      showToast('SenderCompID is required', 'error')
      return
    }

    const doSave = async () => {
      if (editingId) {
        const r = await updateSessionConfig(editingId, body)
        if (r?.status === 'updated') {
          showToast(`Session ${body.sender_comp_id} updated`, 'success')
          setShowForm(false)
          load()
        } else {
          showToast('Failed to update session', 'error')
        }
      } else {
        const r = await addSessionConfig(body)
        if (r?.status === 'created') {
          showToast(`Session ${body.sender_comp_id} created`, 'success')
          setShowForm(false)
          load()
        } else {
          showToast('Failed to create session', 'error')
        }
      }
    }

    doSave()
  }

  const handleDelete = (compId: string) => {
    setConfirm({
      title: 'Delete Session Config',
      message: `Remove session configuration for ${compId}? This cannot be undone.`,
      action: async () => {
        const r = await deleteSessionConfig(compId)
        if (r?.status === 'deleted') {
          showToast(`Session ${compId} deleted`, 'success')
          load()
        } else {
          showToast('Failed to delete session', 'error')
        }
      },
    })
  }

  const handleToggle = async (s: SessionConfigType) => {
    if (s.enabled) {
      const r = await disableSession(s.sender_comp_id)
      if (r?.status === 'disabled') {
        showToast(`Session ${s.sender_comp_id} disabled`, 'success')
        load()
      } else {
        showToast('Failed to disable session', 'error')
      }
    } else {
      const r = await enableSession(s.sender_comp_id)
      if (r?.status === 'enabled') {
        showToast(`Session ${s.sender_comp_id} enabled`, 'success')
        load()
      } else {
        showToast('Failed to enable session', 'error')
      }
    }
  }

  const handleResetSeq = (compId: string) => {
    setConfirm({
      title: 'Reset Sequences',
      message: `Reset inbound/outbound sequence numbers for ${compId} to 0?`,
      action: async () => {
        const r = await resetSequences(compId)
        if (r?.status === 'sequences_reset') {
          showToast(`Sequences reset for ${compId}`, 'success')
          load()
        } else {
          showToast('Failed to reset sequences', 'error')
        }
      },
    })
  }

  return (
    <>
      <div className="page-title">Session Configurator</div>

      <div style={{ marginBottom: 16 }}>
        <button className="btn btn-success" onClick={openAddForm}>
          + Add Session
        </button>
      </div>

      <Table
        columns={[
          { header: 'CompID', render: (s) => s.sender_comp_id },
          { header: 'Target', render: (s) => s.target_comp_id },
          { header: 'FIX', render: (s) => s.fix_version },
          { header: 'HB', render: (s) => String(s.heartbeat_interval) + 's' },
          { header: 'Participant', render: (s) => String(s.participant_id || '--') },
          {
            header: 'Enabled',
            render: (s) => (
              <button
                className={`btn btn-sm ${s.enabled ? 'btn-success' : 'btn-danger'}`}
                onClick={() => handleToggle(s)}
                style={{ minWidth: 70 }}
              >
                {s.enabled ? 'Yes' : 'No'}
              </button>
            ),
          },
          {
            header: 'Status',
            render: (s) =>
              s.connected ? (
                <StatusBadge text="Connected" variant="green" />
              ) : (
                <StatusBadge text="Offline" variant="red" />
              ),
          },
          { header: 'In Seq', render: (s) => String(s.inbound_seq) },
          { header: 'Out Seq', render: (s) => String(s.outbound_seq) },
          { header: 'Last Logon', render: (s) => s.last_logon_time || '--' },
          { header: 'Last Logout', render: (s) => s.last_logout_time || '--' },
          { header: 'Description', render: (s) => s.description || '--' },
          {
            header: 'Actions',
            render: (s) => (
              <div style={{ display: 'flex', gap: 4 }}>
                <button className="btn btn-sm" onClick={() => openEditForm(s)}>
                  Edit
                </button>
                <button className="btn btn-sm" onClick={() => handleResetSeq(s.sender_comp_id)}>
                  Reset Seq
                </button>
                <button className="btn btn-sm btn-danger" onClick={() => handleDelete(s.sender_comp_id)}>
                  Delete
                </button>
              </div>
            ),
          },
        ]}
        data={sessions}
        emptyMessage="No session configurations"
      />

      {/* Add/Edit Modal */}
      {showForm && (
        <div className="dialog-overlay" onClick={(e) => { if (e.target === e.currentTarget) setShowForm(false) }}>
          <div className="dialog" style={{ minWidth: 420 }}>
            <h3>{editingId ? 'Edit Session' : 'Add Session'}</h3>
            <div className="form-grid" style={{ gap: 10 }}>
              <div className="form-field">
                <label>SenderCompID</label>
                <input
                  value={form.sender_comp_id}
                  onChange={(e) => setField('sender_comp_id', e.target.value)}
                  disabled={!!editingId}
                  placeholder="MM1"
                />
              </div>
              <div className="form-field">
                <label>TargetCompID</label>
                <input
                  value={form.target_comp_id}
                  onChange={(e) => setField('target_comp_id', e.target.value)}
                  placeholder="EXCHANGE"
                />
              </div>
              <div className="form-field">
                <label>FIX Version</label>
                <select
                  value={form.fix_version}
                  onChange={(e) => setField('fix_version', e.target.value)}
                >
                  {FIX_VERSIONS.map((v) => (
                    <option key={v} value={v}>
                      {v}
                    </option>
                  ))}
                </select>
              </div>
              <div className="form-field">
                <label>Heartbeat Interval (s)</label>
                <input
                  type="number"
                  value={form.heartbeat_interval}
                  onChange={(e) => setField('heartbeat_interval', e.target.value)}
                  placeholder="30"
                />
              </div>
              <div className="form-field">
                <label>Participant ID</label>
                <input
                  type="number"
                  value={form.participant_id}
                  onChange={(e) => setField('participant_id', e.target.value)}
                  placeholder="0"
                />
              </div>
              <div className="form-field">
                <label>Description</label>
                <input
                  value={form.description}
                  onChange={(e) => setField('description', e.target.value)}
                  placeholder="Market Maker 1"
                />
              </div>
            </div>
            <div className="dialog-actions" style={{ marginTop: 16 }}>
              <button className="btn" onClick={() => setShowForm(false)}>
                Cancel
              </button>
              <button className="btn btn-success" onClick={handleSave}>
                {editingId ? 'Update' : 'Create'}
              </button>
            </div>
          </div>
        </div>
      )}

      {confirm && (
        <Modal
          title={confirm.title}
          message={confirm.message}
          onConfirm={async () => {
            await confirm.action()
            setConfirm(null)
          }}
          onCancel={() => setConfirm(null)}
        />
      )}
    </>
  )
}
