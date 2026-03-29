import type { ExecutionReport } from './fix-client.js'

export interface Order {
  clOrdId: string
  orderId?: string
  symbol: string
  side: 'buy' | 'sell'
  type: string
  price?: number
  quantity: number
  filledQty: number
  leavesQty: number
  status: string
  text?: string
  timestamp: string
}

export interface TradeRecord {
  tradeId: string
  clOrdId: string
  symbol: string
  side: 'buy' | 'sell'
  price: number
  quantity: number
  fee: number
  timestamp: string
}

export interface PositionRecord {
  symbol: string
  netQty: number
  avgPrice: number
  realizedPnl: number
  unrealizedPnl: number
  lastPrice: number
}

export class ParticipantState {
  orders: Map<string, Order> = new Map()
  trades: TradeRecord[] = []
  positions: Map<string, PositionRecord> = new Map()
  connected = false
  senderCompId = ''

  onExecutionReport(report: ExecutionReport): void {
    const existing = this.orders.get(report.clOrdId)

    const statusMap: Record<string, string> = {
      '0': 'New',
      '1': 'PartialFill',
      '2': 'Filled',
      '3': 'DoneForDay',
      '4': 'Canceled',
      '5': 'Replaced',
      '6': 'PendingCancel',
      '8': 'Rejected',
      'A': 'PendingNew',
      'C': 'Expired',
      'E': 'PendingReplace',
    }

    const execTypeMap: Record<string, string> = {
      '0': 'New',
      'F': 'Fill',
      '4': 'Cancel',
      '5': 'Replace',
      '8': 'Reject',
      'C': 'Expired',
    }

    const status = statusMap[report.ordStatus] ?? report.ordStatus
    const execType = execTypeMap[report.execType] ?? report.execType

    if (existing) {
      const updated: Order = {
        ...existing,
        orderId: report.orderId || existing.orderId,
        status,
        filledQty: report.cumQty,
        leavesQty: report.leavesQty,
        text: report.text || existing.text,
      }
      this.orders.set(report.clOrdId, updated)
    } else {
      this.orders.set(report.clOrdId, {
        clOrdId: report.clOrdId,
        orderId: report.orderId,
        symbol: report.symbol,
        side: report.side as 'buy' | 'sell',
        type: 'limit',
        price: report.price,
        quantity: report.quantity,
        filledQty: report.cumQty,
        leavesQty: report.leavesQty,
        status,
        text: report.text,
        timestamp: report.transactTime || new Date().toISOString(),
      })
    }

    // Record trade on fill
    if ((execType === 'Fill' || report.execType === 'F' || report.execType === '1' || report.execType === '2')
      && report.lastQty > 0) {
      this.trades.push({
        tradeId: `T-${Date.now()}-${this.trades.length}`,
        clOrdId: report.clOrdId,
        symbol: report.symbol,
        side: report.side as 'buy' | 'sell',
        price: report.lastPx,
        quantity: report.lastQty,
        fee: report.lastPx * report.lastQty * 0.0001, // 1bps assumed fee
        timestamp: report.transactTime || new Date().toISOString(),
      })
      this.updatePosition(report.symbol, report.side, report.lastPx, report.lastQty)
    }
  }

  private updatePosition(symbol: string, side: string, price: number, qty: number): void {
    const pos = this.positions.get(symbol) ?? {
      symbol,
      netQty: 0,
      avgPrice: 0,
      realizedPnl: 0,
      unrealizedPnl: 0,
      lastPrice: price,
    }

    const signedQty = side === 'buy' ? qty : -qty
    const prevNetQty = pos.netQty
    const newNetQty = prevNetQty + signedQty

    // Update average price on position increases
    if (Math.sign(signedQty) === Math.sign(prevNetQty) || prevNetQty === 0) {
      const totalCost = pos.avgPrice * Math.abs(prevNetQty) + price * Math.abs(signedQty)
      pos.avgPrice = Math.abs(newNetQty) > 0
        ? totalCost / Math.abs(newNetQty)
        : 0
    } else {
      // Position reduction or flip: realize P&L
      const closedQty = Math.min(Math.abs(signedQty), Math.abs(prevNetQty))
      const pnlPerUnit = prevNetQty > 0 ? price - pos.avgPrice : pos.avgPrice - price
      pos.realizedPnl += pnlPerUnit * closedQty

      if (Math.abs(newNetQty) > Math.abs(prevNetQty)) {
        pos.avgPrice = price
      }
    }

    pos.netQty = newNetQty
    pos.lastPrice = price

    this.positions.set(symbol, pos)
  }

  updateLastPrice(symbol: string, price: number): void {
    const pos = this.positions.get(symbol)
    if (pos) {
      pos.lastPrice = price
      pos.unrealizedPnl = pos.netQty > 0
        ? (price - pos.avgPrice) * pos.netQty
        : pos.netQty < 0
        ? (pos.avgPrice - price) * Math.abs(pos.netQty)
        : 0
    }
  }

  getActiveOrders(): Order[] {
    return [...this.orders.values()].filter(
      (o) => o.status === 'New' || o.status === 'PartialFill' || o.status === 'PendingNew' || o.status === 'Pending'
    )
  }

  getAllOrders(): Order[] {
    return [...this.orders.values()].sort(
      (a, b) => new Date(b.timestamp).getTime() - new Date(a.timestamp).getTime()
    )
  }

  getTotalPnl(): number {
    let pnl = 0
    for (const pos of this.positions.values()) {
      pnl += pos.realizedPnl + pos.unrealizedPnl
    }
    for (const trade of this.trades) {
      pnl -= trade.fee
    }
    return pnl
  }
}
