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

export interface Trade {
  trade_id: number
  instrument_id: number
  symbol: string
  price: number
  quantity: number
  buy_participant_id: number
  sell_participant_id: number
  timestamp: string
}

export interface Position {
  instrument_id: number
  symbol: string
  net_position: number
  avg_price: number
  realized_pnl: number
  unrealized_pnl: number
}

export class RestClient {
  private token?: string

  constructor(private baseUrl: string) {}

  private async get<T>(path: string): Promise<T | null> {
    try {
      const headers: Record<string, string> = { 'Content-Type': 'application/json' }
      if (this.token) headers['Authorization'] = `Bearer ${this.token}`
      const res = await fetch(`${this.baseUrl}${path}`, { headers })
      if (!res.ok) return null
      return await res.json() as T
    } catch {
      return null
    }
  }

  private async post<T>(path: string, body?: unknown): Promise<T | null> {
    try {
      const headers: Record<string, string> = { 'Content-Type': 'application/json' }
      if (this.token) headers['Authorization'] = `Bearer ${this.token}`
      const res = await fetch(`${this.baseUrl}${path}`, {
        method: 'POST',
        headers,
        body: body ? JSON.stringify(body) : undefined,
      })
      if (!res.ok) return null
      return await res.json() as T
    } catch {
      return null
    }
  }

  async login(username: string, password: string): Promise<string | null> {
    const resp = await this.post<{ token?: string }>('/api/v1/auth/login', { username, password })
    if (resp?.token) {
      this.token = resp.token
      return resp.token
    }
    return null
  }

  setToken(token: string): void {
    this.token = token
  }

  async getInstruments(): Promise<Instrument[]> {
    return (await this.get<Instrument[]>('/api/v1/instruments')) ?? []
  }

  async getOrderBook(instrumentId: number): Promise<OrderBookSnapshot | null> {
    return this.get<OrderBookSnapshot>(`/api/v1/instruments/${instrumentId}/book`)
  }

  async getTrades(participantId?: number): Promise<Trade[]> {
    const query = participantId ? `?participant_id=${participantId}` : ''
    return (await this.get<Trade[]>(`/api/v1/trades${query}`)) ?? []
  }

  async getHealth(): Promise<{ status: string; version?: string; uptime?: number } | null> {
    return this.get('/api/v1/health')
  }

  async getMetrics(): Promise<{ counters: Record<string, number>; gauges: Record<string, number> } | null> {
    return this.get('/api/v1/metrics')
  }
}
