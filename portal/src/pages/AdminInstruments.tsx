import { useEffect, useState, useCallback } from 'react'
import { apiGet, apiPost, apiPut } from '../api'
import type { Instrument } from '../types'
import Modal from '../components/Modal'

interface AdminInstrumentsProps {
  showToast: (msg: string, type: 'success' | 'error') => void
}

interface ConfirmState {
  title: string
  message: string
  action: () => Promise<void>
}

const defaultForm = {
  instrument_id: '',
  symbol: '',
  tick_size: '1',
  lot_size: '1',
  min_quantity: '1',
  max_quantity: '1000000',
  static_ref_price: '',
  static_band_pct: '10',
}

export default function AdminInstruments({ showToast }: AdminInstrumentsProps) {
  const [instruments, setInstruments] = useState<Instrument[]>([])
  const [form, setForm] = useState(defaultForm)
  const [confirm, setConfirm] = useState<ConfirmState | null>(null)

  const load = useCallback(() => {
    apiGet<Instrument[]>('/api/v1/instruments').then((d) => setInstruments(d ?? []))
  }, [])

  useEffect(() => {
    load()
  }, [load])

  const setField = (field: string, value: string) => {
    setForm((prev) => ({ ...prev, [field]: value }))
  }

  const handleCreate = () => {
    const id = parseInt(form.instrument_id) || 0
    const symbol = form.symbol
    if (!id || !symbol) {
      showToast('ID and Symbol are required', 'error')
      return
    }
    const body = {
      instrument_id: id,
      symbol,
      tick_size: parseInt(form.tick_size) || 1,
      lot_size: parseInt(form.lot_size) || 1,
      min_quantity: parseInt(form.min_quantity) || 1,
      max_quantity: parseInt(form.max_quantity) || 1000000,
      static_ref_price: parseInt(form.static_ref_price) || 0,
      static_band_pct: parseFloat(form.static_band_pct) || 10,
    }
    setConfirm({
      title: 'Create Instrument',
      message: `Create instrument ${body.instrument_id} (${body.symbol})?`,
      action: async () => {
        const r = await apiPost<{ status?: string; error?: string }>('/api/v1/instruments', body)
        if (r?.status === 'created') {
          showToast(`Instrument ${body.symbol} created`, 'success')
          setForm(defaultForm)
          load()
        } else {
          showToast(`Failed: ${r?.error ?? 'unknown'}`, 'error')
        }
      },
    })
  }

  const handleUpdate = (inst: Instrument) => {
    const newRef = prompt(`New reference price (current: ${inst.static_ref_price}):`)
    const newBand = prompt(`New band % (current: ${inst.static_band_pct}):`)
    if (!newRef && !newBand) return
    const body: Record<string, number> = {}
    if (newRef) body.static_ref_price = parseInt(newRef)
    if (newBand) body.static_band_pct = parseFloat(newBand)
    setConfirm({
      title: `Update Instrument ${inst.instrument_id}`,
      message: `Update with: ${JSON.stringify(body)}?`,
      action: async () => {
        const r = await apiPut<{ status?: string; error?: string }>(`/api/v1/instruments/${inst.instrument_id}`, body)
        if (r?.status === 'updated') {
          showToast('Instrument updated', 'success')
          load()
        } else {
          showToast(`Failed: ${r?.error ?? 'unknown'}`, 'error')
        }
      },
    })
  }

  const fields: { key: keyof typeof defaultForm; label: string; type: string; placeholder: string }[] = [
    { key: 'instrument_id', label: 'Instrument ID', type: 'number', placeholder: '102' },
    { key: 'symbol', label: 'Symbol', type: 'text', placeholder: 'SOL-USD' },
    { key: 'tick_size', label: 'Tick Size', type: 'number', placeholder: '1' },
    { key: 'lot_size', label: 'Lot Size', type: 'number', placeholder: '1' },
    { key: 'min_quantity', label: 'Min Quantity', type: 'number', placeholder: '1' },
    { key: 'max_quantity', label: 'Max Quantity', type: 'number', placeholder: '1000000' },
    { key: 'static_ref_price', label: 'Ref Price', type: 'number', placeholder: '150' },
    { key: 'static_band_pct', label: 'Band %', type: 'number', placeholder: '10' },
  ]

  return (
    <>
      <div className="page-title">Admin: Instruments</div>

      <div className="card" style={{ marginBottom: 20 }}>
        <div className="card-label">Create New Instrument</div>
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
          Create Instrument
        </button>
      </div>

      <div className="section-title">Existing Instruments</div>
      {instruments.map((inst) => (
        <div key={inst.instrument_id} className="card inst-card-row">
          <div>
            <span className="inst-id">{inst.instrument_id}</span>
            <span className="inst-meta">
              tick={inst.tick_size} lot={inst.lot_size} ref={inst.static_ref_price} band={inst.static_band_pct}%
            </span>
          </div>
          <button className="btn" onClick={() => handleUpdate(inst)}>
            Update
          </button>
        </div>
      ))}

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
