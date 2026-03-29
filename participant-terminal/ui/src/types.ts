export type Page =
  | 'dashboard'
  | 'order-entry'
  | 'order-book'
  | 'orders'
  | 'trades'
  | 'positions'
  | 'market-data'
  | 'settings'

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

export interface Instrument {
  instrument_id: number
  symbol: string
  description: string
  currency: string
  asset_class: string
  state: string
  tick_size: number
  lot_size: number
  best_bid: number
  best_ask: number
  best_bid_qty: number
  best_ask_qty: number
  last_traded_price: number
  matching_algorithm: string
}

export interface OrderBookLevel {
  price: number
  quantity: number
  order_count: number
}

export interface OrderBookSnapshot {
  instrument_id: number
  symbol: string
  bids: OrderBookLevel[]
  asks: OrderBookLevel[]
}

export interface StatusResponse {
  connected: boolean
  senderCompId: string
  fixHost: string
  fixPort: number
  exchangeUrl: string
  activeOrders: number
  totalTrades: number
  totalPnl: number
  autoTrading: boolean
}

export interface SettingsResponse {
  fixHost: string
  fixPort: number
  senderCompId: string
  targetCompId: string
  exchangeUrl: string
}

export interface WsMessage {
  type: string
  data: unknown
}
