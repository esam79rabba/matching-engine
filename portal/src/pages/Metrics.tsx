import { useEffect, useState } from 'react'
import { apiGet } from '../api'
import type { MetricsResponse } from '../types'
import StatusBadge from '../components/StatusBadge'

export default function Metrics() {
  const [metrics, setMetrics] = useState<MetricsResponse | null>(null)

  useEffect(() => {
    const load = () => {
      apiGet<MetricsResponse>('/api/v1/metrics/json').then(setMetrics)
    }
    load()
    const id = setInterval(load, 5000)
    return () => clearInterval(id)
  }, [])

  if (!metrics) {
    return (
      <>
        <div className="page-title">Metrics</div>
        <div className="empty">Metrics unavailable</div>
      </>
    )
  }

  const counters = Object.entries(metrics.counters ?? {}).sort(([a], [b]) => a.localeCompare(b))
  const gauges = Object.entries(metrics.gauges ?? {}).sort(([a], [b]) => a.localeCompare(b))

  return (
    <>
      <div className="page-title">Metrics</div>
      <table>
        <thead>
          <tr>
            <th>Metric</th>
            <th>Type</th>
            <th>Value</th>
          </tr>
        </thead>
        <tbody>
          {counters.map(([key, value]) => (
            <tr key={key}>
              <td>{key}</td>
              <td><StatusBadge text="counter" variant="blue" /></td>
              <td>{value}</td>
            </tr>
          ))}
          {gauges.map(([key, value]) => (
            <tr key={key}>
              <td>{key}</td>
              <td><StatusBadge text="gauge" variant="amber" /></td>
              <td>{value}</td>
            </tr>
          ))}
        </tbody>
      </table>
    </>
  )
}
