import { useState, useEffect, useCallback } from 'react'
import { apiGet, apiPost } from '../api'
import type { SettingsResponse } from '../types'

interface Props {
  connected: boolean
  showToast: (msg: string, type: 'success' | 'error') => void
}

export default function Settings({ connected, showToast }: Props) {
  const [settings, setSettings] = useState<SettingsResponse | null>(null)
  const [fixHost, setFixHost] = useState('')
  const [fixPort, setFixPort] = useState('')
  const [senderCompId, setSenderCompId] = useState('')
  const [targetCompId, setTargetCompId] = useState('')

  // Auto-trader state
  const [atSymbol, setAtSymbol] = useState('')
  const [atStrategy, setAtStrategy] = useState('market-maker')
  const [atInterval, setAtInterval] = useState('5')
  const [atPrice, setAtPrice] = useState('100')
  const [atSpread, setAtSpread] = useState('0.5')
  const [atQty, setAtQty] = useState('10')
  const [autoTrading, setAutoTrading] = useState(false)

  const loadSettings = useCallback(async () => {
    const data = await apiGet<SettingsResponse>('/api/settings')
    if (data) {
      setSettings(data)
      setFixHost(data.fixHost)
      setFixPort(String(data.fixPort))
      setSenderCompId(data.senderCompId)
      setTargetCompId(data.targetCompId)
    }

    const status = await apiGet<{ autoTrading: boolean }>('/api/status')
    if (status) setAutoTrading(status.autoTrading)
  }, [])

  useEffect(() => {
    loadSettings()
  }, [loadSettings])

  const handleReconnect = async () => {
    const res = await apiPost<{ status?: string; error?: string }>('/api/connect', {
      host: fixHost,
      port: fixPort,
      senderCompId,
      targetCompId,
    })
    if (res?.status === 'connected') {
      showToast('Reconnected with new settings', 'success')
    } else {
      showToast(res?.error ?? 'Connection failed', 'error')
    }
  }

  const handleStartAutoTrade = async () => {
    const res = await apiPost<{ status?: string; error?: string }>('/api/auto-trade/start', {
      symbol: atSymbol,
      strategy: atStrategy,
      interval: parseInt(atInterval),
      priceBase: atPrice,
      spread: atSpread,
      quantity: atQty,
    })
    if (res?.status === 'started') {
      setAutoTrading(true)
      showToast('Auto-trader started', 'success')
    } else {
      showToast(res?.error ?? 'Failed to start', 'error')
    }
  }

  const handleStopAutoTrade = async () => {
    const res = await apiPost<{ status?: string }>('/api/auto-trade/stop')
    if (res) {
      setAutoTrading(false)
      showToast('Auto-trader stopped', 'success')
    }
  }

  return (
    <div>
      <h1 className="page-title">Settings</h1>

      <div className="settings-panel">
        <h3>FIX Connection</h3>
        <div className="form-grid">
          <div className="form-field">
            <label>FIX Host</label>
            <input value={fixHost} onChange={(e) => setFixHost(e.target.value)} />
          </div>
          <div className="form-field">
            <label>FIX Port</label>
            <input type="number" value={fixPort} onChange={(e) => setFixPort(e.target.value)} />
          </div>
          <div className="form-field">
            <label>SenderCompID</label>
            <input value={senderCompId} onChange={(e) => setSenderCompId(e.target.value)} />
          </div>
          <div className="form-field">
            <label>TargetCompID</label>
            <input value={targetCompId} onChange={(e) => setTargetCompId(e.target.value)} />
          </div>
        </div>
        <div style={{ display: 'flex', gap: 8, marginTop: 8 }}>
          <button className="btn btn-primary" onClick={handleReconnect}>
            {connected ? 'Reconnect' : 'Connect'}
          </button>
          {settings && (
            <span style={{ color: 'var(--text-muted)', fontSize: 12, alignSelf: 'center' }}>
              Exchange API: {settings.fixHost}:{settings.fixPort}
            </span>
          )}
        </div>
      </div>

      <div className="auto-trader-panel">
        <h3>
          Auto-Trader
          {autoTrading && <span className="badge badge-amber" style={{ marginLeft: 8 }}>Running</span>}
        </h3>

        {autoTrading ? (
          <div>
            <p style={{ color: 'var(--text-muted)', fontSize: 13, marginBottom: 12 }}>
              Auto-trader is active. Orders are being sent automatically.
            </p>
            <button className="btn btn-danger" onClick={handleStopAutoTrade}>Stop Auto-Trader</button>
          </div>
        ) : (
          <>
            <div className="form-grid">
              <div className="form-field">
                <label>Symbol</label>
                <input
                  value={atSymbol}
                  onChange={(e) => setAtSymbol(e.target.value)}
                  placeholder="e.g. AAPL"
                />
              </div>
              <div className="form-field">
                <label>Strategy</label>
                <select value={atStrategy} onChange={(e) => setAtStrategy(e.target.value)}>
                  <option value="market-maker">Market Maker (bid+ask)</option>
                  <option value="random">Random</option>
                  <option value="aggressive">Aggressive (market sweeps)</option>
                </select>
              </div>
              <div className="form-field">
                <label>Interval (sec)</label>
                <input type="number" value={atInterval} onChange={(e) => setAtInterval(e.target.value)} />
              </div>
              <div className="form-field">
                <label>Base Price</label>
                <input type="number" step="0.01" value={atPrice} onChange={(e) => setAtPrice(e.target.value)} />
              </div>
              <div className="form-field">
                <label>Spread</label>
                <input type="number" step="0.01" value={atSpread} onChange={(e) => setAtSpread(e.target.value)} />
              </div>
              <div className="form-field">
                <label>Quantity</label>
                <input type="number" value={atQty} onChange={(e) => setAtQty(e.target.value)} />
              </div>
            </div>
            <button
              className="btn btn-primary"
              onClick={handleStartAutoTrade}
              disabled={!connected || !atSymbol}
            >
              Start Auto-Trader
            </button>
          </>
        )}
      </div>
    </div>
  )
}
