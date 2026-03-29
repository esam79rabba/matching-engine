import { useState, useEffect, useCallback } from 'react'
import { apiGet } from '../api'
import type { Instrument } from '../types'

export default function MarketData() {
  const [instruments, setInstruments] = useState<Instrument[]>([])

  const refresh = useCallback(async () => {
    const data = await apiGet<Instrument[]>('/api/instruments')
    if (data) setInstruments(data)
  }, [])

  useEffect(() => {
    refresh()
    const id = setInterval(refresh, 2000)
    return () => clearInterval(id)
  }, [refresh])

  return (
    <div>
      <h1 className="page-title">Market Data</h1>

      {instruments.length === 0 ? (
        <div className="empty">No instruments available</div>
      ) : (
        <div className="ticker-grid">
          {instruments.map((inst) => {
            const spread = inst.best_ask > 0 && inst.best_bid > 0
              ? (inst.best_ask - inst.best_bid).toFixed(2)
              : '-'

            return (
              <div key={inst.instrument_id} className="ticker-card">
                <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
                  <span className="ticker-symbol">{inst.symbol}</span>
                  <span className={`badge ticker-state ${inst.state === 'Trading' ? 'badge-green' : 'badge-amber'}`}>
                    {inst.state}
                  </span>
                </div>

                <div style={{ color: 'var(--text-muted)', fontSize: 12 }}>{inst.description}</div>

                <div className="ticker-prices">
                  <div>
                    <div className="ticker-label">Bid</div>
                    <div className="green">
                      {inst.best_bid > 0 ? inst.best_bid.toFixed(2) : '-'}
                      {inst.best_bid_qty > 0 && (
                        <span style={{ color: 'var(--text-muted)', fontSize: 11 }}> x{inst.best_bid_qty}</span>
                      )}
                    </div>
                  </div>
                  <div>
                    <div className="ticker-label">Ask</div>
                    <div className="red">
                      {inst.best_ask > 0 ? inst.best_ask.toFixed(2) : '-'}
                      {inst.best_ask_qty > 0 && (
                        <span style={{ color: 'var(--text-muted)', fontSize: 11 }}> x{inst.best_ask_qty}</span>
                      )}
                    </div>
                  </div>
                  <div>
                    <div className="ticker-label">Last</div>
                    <div>{inst.last_traded_price > 0 ? inst.last_traded_price.toFixed(2) : '-'}</div>
                  </div>
                  <div>
                    <div className="ticker-label">Spread</div>
                    <div style={{ color: 'var(--text-muted)' }}>{spread}</div>
                  </div>
                </div>

                <div style={{ display: 'flex', gap: 16, fontSize: 11, color: 'var(--text-muted)' }}>
                  <span>Tick: {inst.tick_size}</span>
                  <span>Lot: {inst.lot_size}</span>
                  <span>{inst.currency}</span>
                  <span>{inst.matching_algorithm}</span>
                </div>
              </div>
            )
          })}
        </div>
      )}
    </div>
  )
}
