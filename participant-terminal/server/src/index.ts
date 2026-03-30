import express from 'express'
import cors from 'cors'
import http from 'http'
import { FixClient } from './fix-client.js'
import { RestClient } from './rest-client.js'
import { ParticipantState } from './state.js'
import { WsBroadcaster } from './ws-server.js'

const app = express()
app.use(cors())
app.use(express.json())

const state = new ParticipantState()

const FIX_HOST = process.env.FIX_HOST || 'localhost'
const FIX_PORT = parseInt(process.env.FIX_PORT || '9876')
const SENDER_COMP_ID = process.env.SENDER_COMP_ID || 'PARTICIPANT1'
const TARGET_COMP_ID = process.env.TARGET_COMP_ID || 'EXCHANGE'
const EXCHANGE_URL = process.env.EXCHANGE_URL || 'http://localhost:8180'

let fixClient = new FixClient(FIX_HOST, FIX_PORT, SENDER_COMP_ID, TARGET_COMP_ID)
const restClient = new RestClient(EXCHANGE_URL)

// Auto-login to exchange REST API
const exchangeUser = process.env.EXCHANGE_USER || 'admin'
const exchangePass = process.env.EXCHANGE_PASS || 'production-key-2026-rabba3'
restClient.login(exchangeUser, exchangePass).then(token => {
  if (token) process.stdout.write('  Exchange REST: authenticated\n')
  else process.stdout.write('  Exchange REST: login failed (some endpoints may not work)\n')
})

const server = http.createServer(app)
const wsBroadcaster = new WsBroadcaster(server)

// Auto-trader state
let autoTradeTimer: ReturnType<typeof setInterval> | null = null
let autoTradeOrderCount = 0

function wireFixEvents(): void {
  fixClient.on('executionReport', (report) => {
    state.onExecutionReport(report)
    wsBroadcaster.broadcast('execution', report)
  })

  fixClient.on('disconnected', (reason: string) => {
    state.connected = false
    wsBroadcaster.broadcast('status', { connected: false, reason })
  })

  fixClient.on('logon', () => {
    state.connected = true
    state.senderCompId = SENDER_COMP_ID
    wsBroadcaster.broadcast('status', { connected: true })
  })

  fixClient.on('reject', (reject: unknown) => {
    wsBroadcaster.broadcast('reject', reject)
  })

  fixClient.on('cancelReject', (reject: unknown) => {
    wsBroadcaster.broadcast('cancelReject', reject)
  })

  fixClient.on('businessReject', (reject: unknown) => {
    wsBroadcaster.broadcast('reject', reject)
  })
}

wireFixEvents()

// ─── REST API ───────────────────────────────────────────────────

app.get('/api/status', (_req, res) => {
  res.json({
    connected: state.connected,
    senderCompId: state.senderCompId,
    fixHost: FIX_HOST,
    fixPort: FIX_PORT,
    exchangeUrl: EXCHANGE_URL,
    activeOrders: state.getActiveOrders().length,
    totalTrades: state.trades.length,
    totalPnl: state.getTotalPnl(),
    autoTrading: autoTradeTimer !== null,
  })
})

app.get('/api/instruments', async (_req, res) => {
  const instruments = await restClient.getInstruments()
  res.json(instruments)
})

app.get('/api/orders', (_req, res) => {
  res.json(state.getAllOrders())
})

app.get('/api/orders/active', (_req, res) => {
  res.json(state.getActiveOrders())
})

app.get('/api/trades', (_req, res) => {
  res.json(state.trades)
})

app.get('/api/positions', (_req, res) => {
  res.json([...state.positions.values()])
})

app.get('/api/book/:instrumentId', async (req, res) => {
  try {
    const book = await restClient.getOrderBook(parseInt(req.params.instrumentId))
    res.json(book ?? { bids: [], asks: [] })
  } catch {
    res.json({ bids: [], asks: [] })
  }
})

app.get('/api/book-fix/:symbol', (req, res) => {
  if (!fixClient.connected) {
    res.json({ bids: [], asks: [] })
    return
  }

  const symbol = req.params.symbol
  let responded = false

  const handler = (snapshot: { symbol: string; bids: Array<{ price: number; qty: number }>; asks: Array<{ price: number; qty: number }> }) => {
    // The exchange returns instrument_id in tag 55, so match loosely
    if (!responded) {
      responded = true
      fixClient.off('marketData', handler)
      res.json({
        symbol: snapshot.symbol,
        bids: snapshot.bids.map(b => ({ price: b.price, quantity: b.qty, order_count: 0 })),
        asks: snapshot.asks.map(a => ({ price: a.price, quantity: a.qty, order_count: 0 })),
      })
    }
  }

  fixClient.on('marketData', handler)

  try {
    fixClient.sendMarketDataRequest(symbol)
  } catch {
    fixClient.off('marketData', handler)
    res.json({ bids: [], asks: [] })
    return
  }

  // Timeout fallback
  setTimeout(() => {
    if (!responded) {
      responded = true
      fixClient.off('marketData', handler)
      res.json({ bids: [], asks: [] })
    }
  }, 3000)
})

app.get('/api/exchange/health', async (_req, res) => {
  const health = await restClient.getHealth()
  res.json(health ?? { status: 'unreachable' })
})

app.get('/api/exchange/trades', async (_req, res) => {
  const trades = await restClient.getTrades()
  res.json(trades)
})

// Submit new order
app.post('/api/orders', (req, res) => {
  if (!state.connected) {
    res.status(400).json({ error: 'Not connected to exchange' })
    return
  }

  const { symbol, side, type, price, quantity, tif } = req.body
  if (!symbol || !side || !quantity) {
    res.status(400).json({ error: 'Missing required fields: symbol, side, quantity' })
    return
  }

  const clOrdId = `ORD-${Date.now()}-${Math.random().toString(36).substring(2, 6)}`

  try {
    fixClient.sendNewOrder({
      clOrdId,
      symbol,
      side,
      orderType: type || 'limit',
      price: price !== undefined ? parseFloat(price) : undefined,
      quantity: parseInt(quantity),
      timeInForce: tif,
    })

    state.orders.set(clOrdId, {
      clOrdId,
      symbol,
      side,
      type: type || 'limit',
      price: price !== undefined ? parseFloat(price) : undefined,
      quantity: parseInt(quantity),
      filledQty: 0,
      leavesQty: parseInt(quantity),
      status: 'Pending',
      timestamp: new Date().toISOString(),
    })

    res.json({ clOrdId, status: 'sent' })
  } catch (err) {
    res.status(500).json({ error: (err as Error).message })
  }
})

// Cancel order
app.post('/api/orders/:clOrdId/cancel', (req, res) => {
  if (!state.connected) {
    res.status(400).json({ error: 'Not connected to exchange' })
    return
  }

  const order = state.orders.get(req.params.clOrdId)
  if (!order) {
    res.status(404).json({ error: 'Order not found' })
    return
  }

  const cancelId = `CXL-${Date.now()}-${Math.random().toString(36).substring(2, 6)}`
  try {
    fixClient.sendCancel(req.params.clOrdId, cancelId, order.symbol, order.side)
    res.json({ status: 'cancel sent', cancelId })
  } catch (err) {
    res.status(500).json({ error: (err as Error).message })
  }
})

// Modify order
app.post('/api/orders/:clOrdId/modify', (req, res) => {
  if (!state.connected) {
    res.status(400).json({ error: 'Not connected to exchange' })
    return
  }

  const order = state.orders.get(req.params.clOrdId)
  if (!order) {
    res.status(404).json({ error: 'Order not found' })
    return
  }

  const { price, quantity } = req.body
  const modifyId = `MOD-${Date.now()}-${Math.random().toString(36).substring(2, 6)}`

  try {
    fixClient.sendModify(
      req.params.clOrdId,
      modifyId,
      order.symbol,
      order.side,
      parseFloat(price ?? order.price ?? 0),
      parseInt(quantity ?? order.quantity)
    )
    res.json({ status: 'modify sent', modifyId })
  } catch (err) {
    res.status(500).json({ error: (err as Error).message })
  }
})

// Connect to exchange
app.post('/api/connect', async (req, res) => {
  const { host, port, senderCompId, targetCompId } = req.body || {}

  // Allow reconfiguring connection
  if (host || port || senderCompId) {
    fixClient.disconnect()
    fixClient = new FixClient(
      host || FIX_HOST,
      parseInt(port || FIX_PORT),
      senderCompId || SENDER_COMP_ID,
      targetCompId || TARGET_COMP_ID
    )
    wireFixEvents()
  }

  try {
    await fixClient.connect()
    state.connected = true
    state.senderCompId = senderCompId || SENDER_COMP_ID
    res.json({ status: 'connected' })
  } catch (err) {
    res.status(500).json({ error: (err as Error).message })
  }
})

// Disconnect
app.post('/api/disconnect', (_req, res) => {
  fixClient.disconnect()
  state.connected = false
  if (autoTradeTimer) {
    clearInterval(autoTradeTimer)
    autoTradeTimer = null
  }
  res.json({ status: 'disconnected' })
})

// ─── Auto-Trader ────────────────────────────────────────────────

app.post('/api/auto-trade/start', (req, res) => {
  if (!state.connected) {
    res.status(400).json({ error: 'Not connected to exchange' })
    return
  }

  if (autoTradeTimer) {
    res.status(400).json({ error: 'Auto-trader already running' })
    return
  }

  const { symbol, strategy, interval, priceBase, spread, quantity } = req.body
  if (!symbol) {
    res.status(400).json({ error: 'Symbol required' })
    return
  }

  const intervalMs = (interval || 5) * 1000
  const basePrice = parseFloat(priceBase || '100')
  const halfSpread = parseFloat(spread || '0.5')
  const qty = parseInt(quantity || '10')
  autoTradeOrderCount = 0

  autoTradeTimer = setInterval(() => {
    if (!state.connected) {
      if (autoTradeTimer) {
        clearInterval(autoTradeTimer)
        autoTradeTimer = null
      }
      return
    }

    try {
      const strat = strategy || 'market-maker'
      autoTradeOrderCount++

      if (strat === 'market-maker') {
        // Place both bid and ask
        const jitter = (Math.random() - 0.5) * halfSpread * 0.5
        const bidPrice = Math.round((basePrice - halfSpread + jitter) * 100) / 100
        const askPrice = Math.round((basePrice + halfSpread + jitter) * 100) / 100

        const bidId = `AUTO-B-${Date.now()}`
        const askId = `AUTO-A-${Date.now()}`

        fixClient.sendNewOrder({ clOrdId: bidId, symbol, side: 'buy', orderType: 'limit', price: bidPrice, quantity: qty })
        state.orders.set(bidId, {
          clOrdId: bidId, symbol, side: 'buy', type: 'limit', price: bidPrice,
          quantity: qty, filledQty: 0, leavesQty: qty, status: 'Pending', timestamp: new Date().toISOString(),
        })

        fixClient.sendNewOrder({ clOrdId: askId, symbol, side: 'sell', orderType: 'limit', price: askPrice, quantity: qty })
        state.orders.set(askId, {
          clOrdId: askId, symbol, side: 'sell', type: 'limit', price: askPrice,
          quantity: qty, filledQty: 0, leavesQty: qty, status: 'Pending', timestamp: new Date().toISOString(),
        })

      } else if (strat === 'random') {
        const side = Math.random() > 0.5 ? 'buy' : 'sell' as const
        const price = Math.round((basePrice + (Math.random() - 0.5) * halfSpread * 4) * 100) / 100
        const ordId = `AUTO-R-${Date.now()}`

        fixClient.sendNewOrder({ clOrdId: ordId, symbol, side, orderType: 'limit', price, quantity: qty })
        state.orders.set(ordId, {
          clOrdId: ordId, symbol, side, type: 'limit', price,
          quantity: qty, filledQty: 0, leavesQty: qty, status: 'Pending', timestamp: new Date().toISOString(),
        })

      } else if (strat === 'aggressive') {
        const side = Math.random() > 0.5 ? 'buy' : 'sell' as const
        const ordId = `AUTO-X-${Date.now()}`

        fixClient.sendNewOrder({ clOrdId: ordId, symbol, side, orderType: 'market', quantity: qty })
        state.orders.set(ordId, {
          clOrdId: ordId, symbol, side, type: 'market',
          quantity: qty, filledQty: 0, leavesQty: qty, status: 'Pending', timestamp: new Date().toISOString(),
        })
      }

      wsBroadcaster.broadcast('autoTradeStatus', { running: true, orderCount: autoTradeOrderCount })
    } catch {
      // Auto-trade error, continue
    }
  }, intervalMs)

  res.json({ status: 'started', strategy: strategy || 'market-maker', intervalMs })
})

app.post('/api/auto-trade/stop', (_req, res) => {
  if (autoTradeTimer) {
    clearInterval(autoTradeTimer)
    autoTradeTimer = null
    wsBroadcaster.broadcast('autoTradeStatus', { running: false, orderCount: autoTradeOrderCount })
    res.json({ status: 'stopped', totalOrders: autoTradeOrderCount })
  } else {
    res.json({ status: 'not running' })
  }
})

app.get('/api/auto-trade/status', (_req, res) => {
  res.json({
    running: autoTradeTimer !== null,
    orderCount: autoTradeOrderCount,
  })
})

// Settings endpoint
app.get('/api/settings', (_req, res) => {
  res.json({
    fixHost: FIX_HOST,
    fixPort: FIX_PORT,
    senderCompId: SENDER_COMP_ID,
    targetCompId: TARGET_COMP_ID,
    exchangeUrl: EXCHANGE_URL,
  })
})

// ─── Start ──────────────────────────────────────────────────────

const PORT = parseInt(process.env.PORT || '3001')
server.listen(PORT, () => {
  const info = [
    `Participant Terminal Server listening on port ${PORT}`,
    `  FIX target: ${FIX_HOST}:${FIX_PORT}`,
    `  SenderCompID: ${SENDER_COMP_ID}`,
    `  Exchange API: ${EXCHANGE_URL}`,
  ]
  for (const line of info) {
    process.stdout.write(line + '\n')
  }
})
