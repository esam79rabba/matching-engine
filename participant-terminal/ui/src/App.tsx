import { useState, useCallback, useEffect } from 'react'
import { createWs } from './api'
import type { Page } from './types'
import Sidebar from './components/Sidebar'
import Dashboard from './pages/Dashboard'
import OrderEntry from './pages/OrderEntry'
import OrderBook from './pages/OrderBook'
import Orders from './pages/Orders'
import Trades from './pages/Trades'
import Positions from './pages/Positions'
import MarketData from './pages/MarketData'
import Settings from './pages/Settings'

function App() {
  const [page, setPage] = useState<Page>('dashboard')
  const [connected, setConnected] = useState(false)
  const [toasts, setToasts] = useState<Array<{ id: number; msg: string; type: 'success' | 'error' }>>([])

  // Order entry prefill from order book click
  const [prefillPrice, setPrefillPrice] = useState<number | undefined>()
  const [prefillSymbol, setPrefillSymbol] = useState<string | undefined>()
  const [prefillSide, setPrefillSide] = useState<'buy' | 'sell' | undefined>()

  const showToast = useCallback((msg: string, type: 'success' | 'error') => {
    const id = Date.now()
    setToasts((prev) => [...prev, { id, msg, type }])
    setTimeout(() => {
      setToasts((prev) => prev.filter((t) => t.id !== id))
    }, 3000)
  }, [])

  // WebSocket for real-time updates
  useEffect(() => {
    const ws = createWs((msg) => {
      if (msg.type === 'status') {
        const data = msg.data as { connected: boolean; reason?: string }
        setConnected(data.connected)
        if (!data.connected && data.reason) {
          showToast(`Disconnected: ${data.reason}`, 'error')
        }
      }
      if (msg.type === 'reject') {
        const data = msg.data as { text?: string }
        showToast(`Reject: ${data.text ?? 'Unknown'}`, 'error')
      }
      if (msg.type === 'cancelReject') {
        const data = msg.data as { text?: string }
        showToast(`Cancel rejected: ${data.text ?? 'Unknown'}`, 'error')
      }
    })

    return () => { ws.close() }
  }, [showToast])

  const handlePriceClick = (price: number, symbol: string, side: 'buy' | 'sell') => {
    setPrefillPrice(price)
    setPrefillSymbol(symbol)
    setPrefillSide(side)
    setPage('order-entry')
  }

  const renderPage = () => {
    switch (page) {
      case 'dashboard':
        return <Dashboard connected={connected} onConnectionChange={setConnected} showToast={showToast} />
      case 'order-entry':
        return (
          <OrderEntry
            connected={connected}
            showToast={showToast}
            prefillPrice={prefillPrice}
            prefillSymbol={prefillSymbol}
            prefillSide={prefillSide}
          />
        )
      case 'order-book':
        return <OrderBook onPriceClick={handlePriceClick} />
      case 'orders':
        return <Orders connected={connected} showToast={showToast} />
      case 'trades':
        return <Trades />
      case 'positions':
        return <Positions />
      case 'market-data':
        return <MarketData />
      case 'settings':
        return <Settings connected={connected} showToast={showToast} />
      default:
        return <div className="empty">Unknown page</div>
    }
  }

  return (
    <>
      <div className="app">
        <Sidebar activePage={page} connected={connected} onNavigate={setPage} />
        <div className="main">
          {renderPage()}
        </div>
      </div>
      {toasts.map((t) => (
        <div key={t.id} className={`toast toast-${t.type}`}>
          {t.msg}
        </div>
      ))}
    </>
  )
}

export default App
