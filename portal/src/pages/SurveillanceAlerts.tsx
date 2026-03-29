import { useEffect, useState, useCallback } from 'react'
import { apiGet } from '../api'
import Table from '../components/Table'
import StatusBadge from '../components/StatusBadge'

interface Alert {
  alert_id: string
  severity: string
  rule_name: string
  participant_id: number
  instrument_id: number
  description: string
  timestamp: string
}

export default function SurveillanceAlerts() {
  const [alerts, setAlerts] = useState<Alert[]>([])
  const [filterSeverity, setFilterSeverity] = useState('')
  const [loading, setLoading] = useState(true)
  const [apiAvailable, setApiAvailable] = useState(true)

  const loadAlerts = useCallback(() => {
    let path = '/api/v1/surveillance/alerts'
    if (filterSeverity) path += `?severity=${filterSeverity}`
    apiGet<Alert[]>(path).then((d) => {
      if (d === null) {
        setApiAvailable(false)
      } else {
        setAlerts(Array.isArray(d) ? d : [])
        setApiAvailable(true)
      }
      setLoading(false)
    })
  }, [filterSeverity])

  useEffect(() => {
    setLoading(true)
    loadAlerts()
    const id = setInterval(loadAlerts, 3000)
    return () => clearInterval(id)
  }, [loadAlerts])

  const severityVariant = (sev: string): 'red' | 'amber' | 'blue' | 'green' => {
    if (sev === 'Critical') return 'red'
    if (sev === 'High') return 'amber'
    if (sev === 'Medium') return 'amber'
    if (sev === 'Low') return 'blue'
    return 'blue'
  }

  return (
    <>
      <div className="page-title">Surveillance Alerts</div>

      <div style={{ display: 'flex', gap: 12, marginBottom: 16, alignItems: 'flex-end' }}>
        <div className="form-field">
          <label>Severity Filter</label>
          <select value={filterSeverity} onChange={(e) => setFilterSeverity(e.target.value)}>
            <option value="">All</option>
            <option value="Critical">Critical</option>
            <option value="High">High</option>
            <option value="Medium">Medium</option>
            <option value="Low">Low</option>
          </select>
        </div>
        <div style={{ fontSize: 12, color: 'var(--text-muted)', marginBottom: 4 }}>
          Auto-refresh: 3s
        </div>
      </div>

      {!apiAvailable && (
        <div className="empty" style={{ padding: 48 }}>
          Surveillance alerts will appear here when the surveillance module is connected to the HTTP API.
        </div>
      )}

      {apiAvailable && loading && (
        <div className="empty">Loading...</div>
      )}

      {apiAvailable && !loading && (
        <Table
          columns={[
            { header: 'Alert ID', render: (a) => a.alert_id },
            { header: 'Severity', render: (a) => <StatusBadge text={a.severity} variant={severityVariant(a.severity)} /> },
            { header: 'Rule', render: (a) => a.rule_name },
            { header: 'Participant', render: (a) => String(a.participant_id) },
            { header: 'Instrument', render: (a) => String(a.instrument_id) },
            { header: 'Description', render: (a) => a.description },
            { header: 'Time', render: (a) => a.timestamp },
          ]}
          data={alerts}
          emptyMessage="No alerts"
        />
      )}
    </>
  )
}
