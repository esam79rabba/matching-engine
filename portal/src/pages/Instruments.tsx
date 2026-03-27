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

  const handleHalt = (instrumentId: number) => {
    setConfirm({
      title: `Halt Instrument ${instrumentId}`,
      message: `This will reject all new orders for instrument ${instrumentId}. Existing resting orders remain. Continue?`,
      action: async () => {
        const r = await apiPost<{ status?: string; error?: string }>(`/api/v1/instruments/${instrumentId}/halt`)
        if (r?.status === 'executed') {
          showToast(`Instrument ${instrumentId} halted`, 'success')
          load()
        } else {
          showToast(`Halt failed: ${r?.error ?? 'unknown'}`, 'error')
        }
      },
    })
  }

  const handleResume = (instrumentId: number) => {
    setConfirm({
      title: `Resume Instrument ${instrumentId}`,
      message: `This will resume accepting orders for instrument ${instrumentId}. Continue?`,
      action: async () => {
        const r = await apiPost<{ status?: string; error?: string }>(`/api/v1/instruments/${instrumentId}/resume`)
        if (r?.status === 'executed') {
          showToast(`Instrument ${instrumentId} resumed`, 'success')
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
          { header: 'ID', render: (i) => i.instrument_id },
          { header: 'Tick', render: (i) => i.tick_size },
          { header: 'Lot', render: (i) => i.lot_size },
          { header: 'Ref Price', render: (i) => i.static_ref_price },
          { header: 'Band %', render: (i) => `${i.static_band_pct}%` },
          {
            header: 'Actions',
            render: (i) =>
              canWrite ? (
                <>
                  <button className="btn btn-danger" onClick={() => handleHalt(i.instrument_id)}>Halt</button>
                  {' '}
                  <button className="btn btn-success" onClick={() => handleResume(i.instrument_id)}>Resume</button>
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
