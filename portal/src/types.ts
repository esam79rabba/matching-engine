export interface HealthResponse {
  status: string
  version?: string
  uptime?: number
}

export interface MetricsResponse {
  counters: Record<string, number>
  gauges: Record<string, number>
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
  min_quantity?: number
  max_quantity?: number
  static_ref_price: number
  static_band_pct: number
  dynamic_band_pct: number
  best_bid: number
  best_ask: number
  best_bid_qty: number
  best_ask_qty: number
  last_traded_price: number
  matching_algorithm: string
}

export interface FIXSession {
  session_id: string
  sender_comp_id: string
  logged_in: boolean
  inbound_seq: number
  outbound_seq: number
}

export interface KillSwitchResponse {
  blocked_participants: number[]
  error?: string
}

export interface EODStatus {
  status?: string
  date?: string
  total_trades?: number
  total_volume?: number
}

export interface AuditEntry {
  timestamp: string
  user: string
  action: string
  target: string
  details: string
}

export interface Participant {
  participant_id?: number
  id?: number
  name: string
  sender_comp_id?: string
  max_message_rate?: number
  max_order_rate?: number
  max_notional?: number
  fat_finger_price_pct?: number
  fat_finger_max_notional?: number
}

export interface AuthResponse {
  token?: string
  error?: string
}

export interface MeResponse {
  role: string
  username?: string
}

export interface SessionConfig {
  sender_comp_id: string
  target_comp_id: string
  fix_version: string
  heartbeat_interval: number
  enabled: boolean
  participant_id: number
  description: string
  connected: boolean
  inbound_seq: number
  outbound_seq: number
  last_logon_time: string
  last_logout_time: string
}

export type Page =
  | 'dashboard'
  | 'instruments'
  | 'sessions'
  | 'killswitch'
  | 'metrics'
  | 'eod'
  | 'audit'
  | 'admin-instruments'
  | 'admin-participants'
  | 'admin-session-config'
  | 'order-entry'
  | 'order-book'
  | 'trade-history'
  | 'order-history'
  | 'account-credit'
  | 'positions'
  | 'surveillance'
  | 'trade-audit'
  | 'settlement'
  | 'reconciliation'
