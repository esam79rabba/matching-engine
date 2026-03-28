import { useEffect, useState } from 'react'
import { apiGet } from '../api'
import type { HealthResponse, MetricsResponse, Instrument } from '../types'
import Card from '../components/Card'
import Table from '../components/Table'

export default function Dashboard() {
  const [health, setHealth] = useState<HealthResponse | null>(null)
  const [metrics, setMetrics] = useState<MetricsResponse | null>(null)
  const [instruments, setInstruments] = useState<Instrument[]>([])

  useEffect(() => {
    const load = () => {
      apiGet<HealthResponse>('/api/v1/health').then(setHealth)
      apiGet<MetricsResponse>('/api/v1/metrics/json').then(setMetrics)
      apiGet<Instrument[]>('/api/v1/instruments').then((d) => setInstruments(d ?? []))
    }
    load()
    const id = setInterval(load, 5000)
    return () => clearInterval(id)
  }, [])

  const ok = health?.status === 'ok'
  const counters = metrics?.counters ?? {}
  const gauges = metrics?.gauges ?? {}

  return (
    <>
      <div className="page-title">Dashboard</div>
      <div className="card-grid">
        <Card label="Status" value={ok ? 'Healthy' : 'Unhealthy'} colorClass={ok ? 'green' : 'red'} />
        <Card label="Instruments" value={gauges['partition.instrument_count'] ?? 0} />
        <Card label="Orders Accepted" value={counters['fix.orders_accepted'] ?? 0} />
        <Card
          label="Orders Rejected"
          value={counters['fix.orders_rejected'] ?? 0}
          colorClass={(counters['fix.orders_rejected'] ?? 0) > 0 ? 'amber' : ''}
        />
        <Card label="Cancels" value={counters['fix.cancels_processed'] ?? 0} />
        <Card label="FIX Logons" value={counters['fix.sessions_logon'] ?? 0} />
        <Card label="Halts" value={counters['ops.halt'] ?? 0} />
        <Card label="EOD Triggers" value={counters['ops.eod_trigger'] ?? 0} />
      </div>
      <div className="section-title">Instruments</div>
      <Table
        columns={[
          { header: 'Symbol', render: (i) => i.symbol || `#${i.instrument_id}` },
          { header: 'Description', render: (i) => i.description || '' },
          { header: 'State', render: (i) => i.state || 'Inactive' },
          { header: 'Bid', render: (i) => i.best_bid || 0 },
          { header: 'Ask', render: (i) => i.best_ask || 0 },
          { header: 'Last', render: (i) => i.last_traded_price || 0 },
          { header: 'Ref Price', render: (i) => i.static_ref_price },
        ]}
        data={instruments}
        emptyMessage="No instruments loaded"
      />
    </>
  )
}
