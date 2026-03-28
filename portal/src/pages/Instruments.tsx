import { useEffect, useState, useCallback } from 'react'
import { apiGet, apiPost } from '../api'
import type { Instrument } from '../types'
import Table from '../components/Table'
import Modal from '../components/Modal'

interface ConfirmState {
  title: string
  message: string
  action: () => Promise<void>
}

interface InstrumentsProps {
  canWrite: boolean
  showToast: (msg: string, type: 'success' | 'error') => void
}

export default function Instruments({ canWrite, showToast }: InstrumentsProps) {
  const [instruments, setInstruments] = useState<Instrument[]>([])
  const [confirm, setConfirm] = useState<ConfirmState | null>(null)

  const load = useCallback(() => {
    apiGet<Instrument[]>('/api/v1/instruments').then((d) => setInstruments(d ?? []))
  }, [])

  useEffect(() => {
    load()
    const id = setInterval(load, 5000)
    return () => clearInterval(id)
  }, [load])

  const getLabel = (i: Instrument) => i.symbol || `#${i.instrument_id}`

  const handleHalt = (inst: Instrument) => {
    const label = getLabel(inst)
    setConfirm({
      title: `Halt ${label}`,
      message: `This will reject all new orders for ${label}. Existing resting orders remain. Continue?`,
      action: async () => {
        const r = await apiPost<{ status?: string; error?: string }>(`/api/v1/instruments/${inst.instrument_id}/halt`)
        if (r?.status === 'executed') {
          showToast(`${label} halted`, 'success')
          load()
        } else {
          showToast(`Halt failed: ${r?.error ?? 'unknown'}`, 'error')
        }
      },
    })
  }

  const handleResume = (inst: Instrument) => {
    const label = getLabel(inst)
    setConfirm({
      title: `Resume ${label}`,
      message: `This will resume accepting orders for ${label}. Continue?`,
      action: async () => {
        const r = await apiPost<{ status?: string; error?: string }>(`/api/v1/instruments/${inst.instrument_id}/resume`)
        if (r?.status === 'executed') {
          showToast(`${label} resumed`, 'success')
          load()
        } else {
          showToast(`Resume failed: ${r?.error ?? 'unknown'}`, 'error')
        }
      },
    })
  }

  return (
    <>
      <div className="page-title">Instruments</div>
      <Table
        columns={[
          { header: 'Symbol', render: (i) => i.symbol || `#${i.instrument_id}` },
          { header: 'Description', render: (i) => i.description || '' },
          { header: 'State', render: (i) => i.state || 'Inactive' },
          { header: 'Bid', render: (i) => i.best_bid || 0 },
          { header: 'Ask', render: (i) => i.best_ask || 0 },
          { header: 'Spread', render: (i) => (i.best_ask && i.best_bid) ? i.best_ask - i.best_bid : '-' },
          { header: 'Last', render: (i) => i.last_traded_price || 0 },
          {
            header: 'Actions',
            render: (i) =>
              canWrite ? (
                <>
                  <button className="btn btn-danger" onClick={() => handleHalt(i)}>Halt</button>
                  {' '}
                  <button className="btn btn-success" onClick={() => handleResume(i)}>Resume</button>
                </>
              ) : (
                <span style={{ color: 'var(--text-muted)', fontSize: 11 }}>View only</span>
              ),
          },
        ]}
        data={instruments}
        emptyMessage="No instruments loaded"
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
