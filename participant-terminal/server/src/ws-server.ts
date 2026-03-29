import { WebSocketServer, WebSocket } from 'ws'
import type { Server } from 'http'

export type WsMessageType =
  | 'execution'
  | 'status'
  | 'trade'
  | 'position'
  | 'reject'
  | 'cancelReject'
  | 'error'
  | 'autoTradeStatus'

export interface WsMessage {
  type: WsMessageType
  data: unknown
}

export class WsBroadcaster {
  private wss: WebSocketServer

  constructor(server: Server) {
    this.wss = new WebSocketServer({ server })

    this.wss.on('connection', (ws) => {
      ws.on('error', () => { /* ignore client errors */ })
    })
  }

  broadcast(type: WsMessageType, data: unknown): void {
    const msg = JSON.stringify({ type, data })
    for (const client of this.wss.clients) {
      if (client.readyState === WebSocket.OPEN) {
        client.send(msg)
      }
    }
  }
}
