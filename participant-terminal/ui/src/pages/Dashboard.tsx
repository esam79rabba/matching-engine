import { useState, useEffect, useCallback } from 'react'
import { apiGet, apiPost } from '../api'
import type { StatusResponse, Instrument } from '../types'
import Card from '../components/Card'

interface Props {
  connected: boolean
  onConnectionChange: (connected: boolean) => void
  showToast: (msg: string, type: 'success' | 'error') => void
}

export default function Dashboard({ connected, onConnectionChange, showToast }: Props) {
  const [status, setStatus] = useState<StatusResponse | null>(null)
  const [instruments, setInstruments] = useState<Instrument[]>([])
  const [exchangeHealth, setExchangeHealth] = useState<{ status: string } | null>(null)
  const [connecting, setConnecting] = useState(false)

  const refresh = useCallback(async () => {
    const [s, inst, health] = await Promise.all([
      apiGet<StatusResponse>('/api/status'),
      apiGet<Instrument[]>('/api/instruments'),
      apiGet<{ status: string }>('/api/exchange/health'),
    ])
    if (s) {
      setStatus(s)
      onConnectionChange(s.connected)
    }
    if (inst) setInstruments(inst)
    if (health) setExchangeHealth(health)
  }, [onConnectionChange])

  useEffect(() => {
    refresh()
    const id = setInterval(refresh, 3000)
    return () => clearInterval(id)
  }, [refresh])

  const handleConnect = async () => {
    setConnecting(true)
    const res = await apiPost<{ status?: string; error?: string }>('/api/connect')
    setConnecting(false)
    if (res?.status === 'connected') {
      onConnectionChange(true)
      showToast('FIX session connected', 'success')
    } else {
      showToast(res?.error ?? 'Connection failed', 'error')
    }
    refresh()
  }

  const handleDisconnect = async () => {
    await apiPost('/api/disconnect')
    onConnectionChange(false)
    showToast('Disconnected', 'success')
    refresh()
  }

  const pnl = status?.totalPnl ?? 0
  const pnlColor = pnl > 0 ? 'green' : pnl < 0 ? 'red' : '' as const

  return (
    <div>
      <h1 className="page-title">Dashboard</h1>

      <div className="action-bar">
        {connected ? (
          <button className="btn btn-danger" onClick={handleDisconnect}>Disconnect</button>
        ) : (
          <button className="btn btn-success" onClick={handleConnect} disabled={connecting}>
            {connecting ? 'Connecting...' : 'Connect to Exchange'}
          </button>
        )}
        <span style={{ color: 'var(--text-muted)', fontSize: 12 }}>
          {status?.senderCompId ?? ''} &rarr; {status?.fixHost}:{status?.fixPort}
        </span>
      </div>

      <div className="card-grid">
        <Card
          label="FIX Connection"
          value={connected ? 'Connected' : 'Disconnected'}
          color={connected ? 'green' : 'red'}
        />
        <Card
          label="Exchange API"
          value={exchangeHealth?.status === 'ok' ? 'Reachable' : 'Unreachable'}
          color={exchangeHealth?.status === 'ok' ? 'green' : 'red'}
        />
        <Card label="Active Orders" value={status?.activeOrders ?? 0} />
        <Card label="Total Trades" value={status?.totalTrades ?? 0} />
        <Card label="P&L" value={pnl.toFixed(2)} color={pnlColor} />
        <Card
          label="Auto-Trader"
          value={status?.autoTrading ? 'Running' : 'Off'}
          color={status?.autoTrading ? 'amber' : ''}
        />
      </div>

      <h2 className="section-title">Exchange Instruments</h2>
      {instruments.length === 0 ? (
        <div className="empty">No instruments loaded from exchange</div>
      ) : (
        <table>
          <thead>
            <tr>
              <th>ID</th>
              <th>Symbol</th>
              <th>Description</th>
              <th>State</th>
              <th>Bid</th>
              <th>Ask</th>
              <th>Last</th>
            </tr>
          </thead>
          <tbody>
            {instruments.map((inst) => (
              <tr key={inst.instrument_id}>
                <td>{inst.instrument_id}</td>
                <td style={{ fontWeight: 600 }}>{inst.symbol}</td>
                <td style={{ fontFamily: 'var(--font)', color: 'var(--text-muted)' }}>{inst.description}</td>
                <td>
                  <span className={`badge ${inst.state === 'Trading' ? 'badge-green' : 'badge-amber'}`}>
                    {inst.state}
                  </span>
                </td>
                <td className="green">{inst.best_bid > 0 ? inst.best_bid.toFixed(2) : '-'}</td>
                <td className="red">{inst.best_ask > 0 ? inst.best_ask.toFixed(2) : '-'}</td>
                <td>{inst.last_traded_price > 0 ? inst.last_traded_price.toFixed(2) : '-'}</td>
              </tr>
            ))}
          </tbody>
        </table>
      )}
    </div>
  )
}
