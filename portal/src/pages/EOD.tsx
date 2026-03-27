import { useEffect, useState } from 'react'
import { apiGet, apiPost } from '../api'
import type { EODStatus } from '../types'
import Card from '../components/Card'
import Modal from '../components/Modal'

interface EODProps {
  canWrite: boolean
  showToast: (msg: string, type: 'success' | 'error') => void
}

export default function EOD({ canWrite, showToast }: EODProps) {
  const [eod, setEod] = useState<EODStatus | null>(null)
  const [showConfirm, setShowConfirm] = useState(false)

  useEffect(() => {
    const load = () => {
      apiGet<EODStatus>('/api/v1/eod/status').then(setEod)
    }
    load()
    const id = setInterval(load, 5000)
    return () => clearInterval(id)
  }, [])

  const noRun = !eod || eod.status === 'no_eod_run'

  const handleTrigger = async () => {
    const r = await apiPost<{ action?: string; total_trades?: number }>('/api/v1/eod/trigger')
    if (r?.action === 'eod_trigger') {
      showToast(`EOD triggered: ${r.total_trades ?? 0} trades`, 'success')
    } else {
      showToast('EOD trigger failed', 'error')
    }
    setShowConfirm(false)
  }

  return (
    <>
      <div className="page-title">EOD Status</div>
      <div className="card-grid">
        {noRun ? (
          <Card label="Last EOD" value="No EOD run" colorClass="amber" />
        ) : (
          <>
            <Card label="Date" value={eod?.date ?? '--'} />
            <Card label="Total Trades" value={eod?.total_trades ?? 0} />
            <Card label="Total Volume" value={eod?.total_volume ?? 0} />
          </>
        )}
      </div>
      {canWrite && (
        <div className="card" style={{ marginTop: 16 }}>
          <div className="card-label">Manual EOD Trigger</div>
          <button
            className="btn btn-danger"
            style={{ marginTop: 8 }}
            onClick={() => setShowConfirm(true)}
          >
            Trigger EOD
          </button>
        </div>
      )}
      {showConfirm && (
        <Modal
          title="Trigger EOD Processing"
          message="This will generate end-of-day trade summaries, fee reports, and output files. Continue?"
          onConfirm={handleTrigger}
          onCancel={() => setShowConfirm(false)}
        />
      )}
    </>
  )
}
