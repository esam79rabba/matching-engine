import { useEffect, useState, useCallback } from 'react'
import { apiGet, apiPost } from '../api'
import type { Participant } from '../types'
import Table from '../components/Table'
import Modal from '../components/Modal'

interface AdminParticipantsProps {
  showToast: (msg: string, type: 'success' | 'error') => void
}

interface ConfirmState {
  title: string
  message: string
  action: () => Promise<void>
}

const defaultForm = {
  participant_id: '',
  name: '',
  sender_comp_id: '',
  max_order_rate: '500',
  fat_finger_price_pct: '10',
  fat_finger_max_notional: '5000000',
}

export default function AdminParticipants({ showToast }: AdminParticipantsProps) {
  const [participants, setParticipants] = useState<Participant[]>([])
  const [form, setForm] = useState(defaultForm)
  const [confirm, setConfirm] = useState<ConfirmState | null>(null)

  const load = useCallback(() => {
    apiGet<Participant[]>('/api/v1/participants').then((d) => setParticipants(d ?? []))
  }, [])

  useEffect(() => {
    load()
  }, [load])

  const setField = (field: string, value: string) => {
    setForm((prev) => ({ ...prev, [field]: value }))
  }

  const handleCreate = () => {
    const id = parseInt(form.participant_id) || 0
    const name = form.name
    if (!id || !name) {
      showToast('ID and Name are required', 'error')
      return
    }
    const body = {
      participant_id: id,
      name,
      sender_comp_id: form.sender_comp_id,
      max_order_rate: parseInt(form.max_order_rate) || 500,
      fat_finger_price_pct: parseFloat(form.fat_finger_price_pct) || 10,
      fat_finger_max_notional: parseFloat(form.fat_finger_max_notional) || 5000000,
    }
    setConfirm({
      title: 'Create Participant',
      message: `Create participant ${body.participant_id} (${body.name})?`,
      action: async () => {
        const r = await apiPost<{ status?: string; error?: string }>('/api/v1/participants', body)
        if (r?.status === 'created') {
          showToast(`Participant ${body.name} created`, 'success')
          setForm(defaultForm)
          load()
        } else {
          showToast(`Failed: ${r?.error ?? 'unknown'}`, 'error')
        }
      },
    })
  }

  const fields: { key: keyof typeof defaultForm; label: string; type: string; placeholder: string }[] = [
    { key: 'participant_id', label: 'Participant ID', type: 'number', placeholder: '3' },
    { key: 'name', label: 'Name', type: 'text', placeholder: 'New Market Maker' },
    { key: 'sender_comp_id', label: 'SenderCompID', type: 'text', placeholder: 'NEW_MM' },
    { key: 'max_order_rate', label: 'Max Order Rate', type: 'number', placeholder: '500' },
    { key: 'fat_finger_price_pct', label: 'Fat-Finger Price %', type: 'number', placeholder: '10' },
    { key: 'fat_finger_max_notional', label: 'Max Notional', type: 'number', placeholder: '5000000' },
  ]

  return (
    <>
      <div className="page-title">Admin: Participants</div>

      <div className="card" style={{ marginBottom: 20 }}>
        <div className="card-label">Create New Participant</div>
        <div className="form-grid">
          {fields.map(({ key, label, type, placeholder }) => (
            <div key={key} className="form-field">
              <label>{label}</label>
              <input
                type={type}
                placeholder={placeholder}
                value={form[key]}
                onChange={(e) => setField(key, e.target.value)}
              />
            </div>
          ))}
        </div>
        <button className="btn btn-success" onClick={handleCreate}>
          Create Participant
        </button>
      </div>

      <div className="section-title">Existing Participants</div>
      <Table
        columns={[
          { header: 'ID', render: (p) => p.participant_id ?? p.id ?? '--' },
          { header: 'Name', render: (p) => p.name ?? '--' },
          { header: 'Rate', render: (p) => String(p.max_message_rate ?? '--') },
          { header: 'Notional', render: (p) => String(p.max_notional ?? '--') },
        ]}
        data={participants}
        emptyMessage="No participants configured"
      />

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
