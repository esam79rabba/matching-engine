import { useEffect, useState } from 'react'
import { apiGet } from '../api'
import type { AuditEntry } from '../types'
import StatusBadge from '../components/StatusBadge'

function actionVariant(action: string): 'red' | 'green' | 'blue' {
  if (action.includes('halt')) return 'red'
  if (action.includes('resume')) return 'green'
  return 'blue'
}

export default function AuditLog() {
  const [entries, setEntries] = useState<AuditEntry[]>([])

  useEffect(() => {
    const load = () => {
      apiGet<AuditEntry[]>('/api/v1/audit').then((d) => setEntries(d ?? []))
    }
    load()
    const id = setInterval(load, 5000)
    return () => clearInterval(id)
  }, [])

  return (
    <>
      <div className="page-title">Audit Log</div>
      {entries.length === 0 ? (
        <div className="empty">No audit entries</div>
      ) : (
        <table>
          <thead>
            <tr>
              <th>Timestamp</th>
              <th>User</th>
              <th>Action</th>
              <th>Target</th>
              <th>Details</th>
            </tr>
          </thead>
          <tbody>
            {entries.map((e, idx) => (
              <tr key={idx}>
                <td style={{ fontSize: 11, color: 'var(--text-muted)' }}>{e.timestamp ?? '--'}</td>
                <td>{e.user ?? '--'}</td>
                <td>
                  <StatusBadge text={e.action ?? '--'} variant={actionVariant(e.action ?? '')} />
                </td>
                <td>{e.target ?? '--'}</td>
                <td style={{ fontSize: 11, color: 'var(--text-muted)' }}>{e.details ?? ''}</td>
              </tr>
            ))}
          </tbody>
        </table>
      )}
    </>
  )
}
