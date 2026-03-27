import { useEffect, useState } from 'react'
import { apiGet } from '../api'
import type { FIXSession } from '../types'
import Table from '../components/Table'
import StatusBadge from '../components/StatusBadge'

export default function Sessions() {
  const [sessions, setSessions] = useState<FIXSession[]>([])

  useEffect(() => {
    const load = () => {
      apiGet<FIXSession[]>('/api/v1/fix/sessions').then((d) => setSessions(d ?? []))
    }
    load()
    const id = setInterval(load, 5000)
    return () => clearInterval(id)
  }, [])

  return (
    <>
      <div className="page-title">FIX Sessions</div>
      <Table
        columns={[
          { header: 'Session', render: (s) => s.session_id },
          { header: 'CompID', render: (s) => s.sender_comp_id },
          {
            header: 'Status',
            render: (s) =>
              s.logged_in ? (
                <StatusBadge text="Logged In" variant="green" />
              ) : (
                <StatusBadge text="Disconnected" variant="red" />
              ),
          },
          { header: 'In Seq', render: (s) => s.inbound_seq },
          { header: 'Out Seq', render: (s) => s.outbound_seq },
        ]}
        data={sessions}
        emptyMessage="No active FIX sessions"
      />
    </>
  )
}
