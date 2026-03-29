import { useState, useEffect, useCallback } from 'react'
import { apiGet, apiPost } from '../api'
import type { Instrument } from '../types'

interface Props {
  connected: boolean
  showToast: (msg: string, type: 'success' | 'error') => void
  prefillPrice?: number
  prefillSymbol?: string
  prefillSide?: 'buy' | 'sell'
}

export default function OrderEntry({ connected, showToast, prefillPrice, prefillSymbol, prefillSide }: Props) {
  const [instruments, setInstruments] = useState<Instrument[]>([])
  const [symbol, setSymbol] = useState(prefillSymbol ?? '')
  const [side, setSide] = useState<'buy' | 'sell'>(prefillSide ?? 'buy')
  const [type, setType] = useState('limit')
  const [price, setPrice] = useState(prefillPrice?.toString() ?? '')
  const [quantity, setQuantity] = useState('100')
  const [tif, setTif] = useState('0')
  const [lastResult, setLastResult] = useState<{ ok: boolean; msg: string } | null>(null)
  const [submitting, setSubmitting] = useState(false)

  const loadInstruments = useCallback(async () => {
    const data = await apiGet<Instrument[]>('/api/instruments')
    if (data) {
      setInstruments(data)
      if (!symbol && data.length > 0) {
        setSymbol(data[0].symbol)
      }
    }
  }, [symbol])

  useEffect(() => {
    loadInstruments()
  }, [loadInstruments])

  useEffect(() => {
    if (prefillPrice !== undefined) setPrice(prefillPrice.toString())
    if (prefillSymbol) setSymbol(prefillSymbol)
    if (prefillSide) setSide(prefillSide)
  }, [prefillPrice, prefillSymbol, prefillSide])

  const handleSubmit = async () => {
    if (!connected) {
      showToast('Not connected to exchange', 'error')
      return
    }
    if (!symbol || !quantity) {
      showToast('Fill in required fields', 'error')
      return
    }

    setSubmitting(true)
    const body: Record<string, unknown> = { symbol, side, type, quantity }
    if (type !== 'market' && price) {
      body.price = price
    }
    if (tif !== '0') {
      body.tif = tif
    }

    const res = await apiPost<{ clOrdId?: string; status?: string; error?: string }>('/api/orders', body)
    setSubmitting(false)

    if (res?.clOrdId) {
      setLastResult({ ok: true, msg: `Order sent: ${res.clOrdId}` })
      showToast(`Order ${res.clOrdId} submitted`, 'success')
    } else {
      setLastResult({ ok: false, msg: res?.error ?? 'Failed to send order' })
      showToast(res?.error ?? 'Order failed', 'error')
    }
  }

  return (
    <div>
      <h1 className="page-title">Order Entry</h1>

      <div className="order-panel">
        <div className="side-toggle">
          <button
            className={side === 'buy' ? 'active-buy' : ''}
            onClick={() => setSide('buy')}
          >
            BUY
          </button>
          <button
            className={side === 'sell' ? 'active-sell' : ''}
            onClick={() => setSide('sell')}
          >
            SELL
          </button>
        </div>

        <div className="form-grid">
          <div className="form-field">
            <label>Instrument</label>
            <select value={symbol} onChange={(e) => setSymbol(e.target.value)}>
              {instruments.length === 0 && <option value="">No instruments</option>}
              {instruments.map((inst) => (
                <option key={inst.instrument_id} value={inst.symbol}>
                  {inst.symbol} - {inst.description}
                </option>
              ))}
            </select>
          </div>

          <div className="form-field">
            <label>Order Type</label>
            <select value={type} onChange={(e) => setType(e.target.value)}>
              <option value="limit">Limit</option>
              <option value="market">Market</option>
              <option value="ioc">IOC</option>
              <option value="fok">FOK</option>
            </select>
          </div>

          <div className="form-field">
            <label>Price</label>
            <input
              type="number"
              step="0.01"
              value={price}
              onChange={(e) => setPrice(e.target.value)}
              disabled={type === 'market'}
              placeholder={type === 'market' ? 'N/A' : 'Price'}
            />
          </div>

          <div className="form-field">
            <label>Quantity</label>
            <input
              type="number"
              value={quantity}
              onChange={(e) => setQuantity(e.target.value)}
              placeholder="Quantity"
            />
          </div>

          <div className="form-field">
            <label>Time In Force</label>
            <select value={tif} onChange={(e) => setTif(e.target.value)}>
              <option value="0">Day</option>
              <option value="1">GTC</option>
              <option value="3">IOC</option>
              <option value="4">FOK</option>
              <option value="6">GTD</option>
            </select>
          </div>
        </div>

        <button
          className={`btn btn-lg ${side === 'buy' ? 'btn-success' : 'btn-danger'}`}
          style={{ width: '100%' }}
          onClick={handleSubmit}
          disabled={!connected || submitting}
        >
          {submitting ? 'Sending...' : `${side === 'buy' ? 'BUY' : 'SELL'} ${symbol}`}
        </button>

        {lastResult && (
          <div className={`order-result ${lastResult.ok ? 'success' : 'error'}`}>
            {lastResult.msg}
          </div>
        )}
      </div>
    </div>
  )
}
