import net from 'net'
import { EventEmitter } from 'events'

export interface ExecutionReport {
  clOrdId: string
  orderId: string
  execType: string
  ordStatus: string
  symbol: string
  side: string
  price: number
  quantity: number
  cumQty: number
  leavesQty: number
  lastPx: number
  lastQty: number
  text: string
  transactTime: string
}

export interface MarketDataSnapshot {
  symbol: string
  bids: Array<{ price: number; qty: number }>
  asks: Array<{ price: number; qty: number }>
}

export interface BusinessReject {
  refMsgType: string
  refSeqNum: number
  reason: string
  text: string
}

function buildFixMessage(fields: Record<number, string>): string {
  let body = ''
  for (const [tag, value] of Object.entries(fields)) {
    const tagNum = parseInt(tag)
    if (tagNum !== 8 && tagNum !== 9 && tagNum !== 10) {
      body += `${tag}=${value}\x01`
    }
  }
  const header = `8=FIX.4.4\x01` + `9=${body.length}\x01`
  const raw = header + body
  let sum = 0
  for (let i = 0; i < raw.length; i++) {
    sum += raw.charCodeAt(i)
  }
  const checksum = (sum % 256).toString().padStart(3, '0')
  return raw + `10=${checksum}\x01`
}

interface FixField {
  tag: string
  value: string
}

function parseFixFields(raw: string): FixField[] {
  const result: FixField[] = []
  const parts = raw.split('\x01')
  for (const part of parts) {
    const eqIdx = part.indexOf('=')
    if (eqIdx > 0) {
      result.push({ tag: part.substring(0, eqIdx), value: part.substring(eqIdx + 1) })
    }
  }
  return result
}

function parseFixMessage(raw: string): Record<string, string> {
  const fields: Record<string, string> = {}
  const parts = raw.split('\x01')
  for (const part of parts) {
    const eqIdx = part.indexOf('=')
    if (eqIdx > 0) {
      fields[part.substring(0, eqIdx)] = part.substring(eqIdx + 1)
    }
  }
  return fields
}

export class FixClient extends EventEmitter {
  private socket: net.Socket | null = null
  private seqNum = 1
  private buffer = ''
  private heartbeatInterval: ReturnType<typeof setInterval> | null = null
  private _connected = false

  constructor(
    private host: string,
    private port: number,
    private senderCompId: string,
    private targetCompId: string = 'EXCHANGE'
  ) {
    super()
  }

  get connected(): boolean {
    return this._connected
  }

  connect(): Promise<void> {
    return new Promise((resolve, reject) => {
      if (this.socket) {
        this.disconnect()
      }

      this.seqNum = 1
      this.buffer = ''
      this.socket = new net.Socket()

      const timeout = setTimeout(() => {
        reject(new Error(`Connection timeout to ${this.host}:${this.port}`))
        this.socket?.destroy()
      }, 10000)

      this.socket.on('connect', () => {
        clearTimeout(timeout)
        this.sendLogon()
      })

      this.socket.on('data', (data: Buffer) => {
        const raw = data.toString('latin1')
        console.log(`[FIX-RX] ${raw.replace(/\x01/g, '|').substring(0, 200)}`)
        this.buffer += raw
        this.processBuffer()
      })

      this.socket.on('error', (err: Error) => {
        clearTimeout(timeout)
        this._connected = false
        this.emit('disconnected', err.message)
        reject(err)
      })

      this.socket.on('close', () => {
        this._connected = false
        this.stopHeartbeat()
        this.emit('disconnected', 'Connection closed')
      })

      // Store resolve for logon ack
      this.once('logon', () => {
        this._connected = true
        this.startHeartbeat()
        resolve()
      })

      this.socket.connect(this.port, this.host)
    })
  }

  disconnect(): void {
    if (this.socket) {
      if (this._connected) {
        const now = new Date().toISOString().replace(/[-:T]/g, '').substring(0, 17)
        const msg = buildFixMessage({
          35: '5', // Logout
          49: this.senderCompId,
          56: this.targetCompId,
          34: String(this.seqNum++),
          52: now,
        })
        try { this.socket.write(msg, 'latin1') } catch { /* ignore */ }
      }
      this.socket.destroy()
      this.socket = null
    }
    this._connected = false
    this.stopHeartbeat()
    this.emit('disconnected', 'Manual disconnect')
  }

  sendNewOrder(order: {
    clOrdId: string
    symbol: string
    side: 'buy' | 'sell'
    orderType: 'limit' | 'market' | 'ioc' | 'fok'
    price?: number
    quantity: number
    timeInForce?: string
  }): void {
    const now = new Date().toISOString().replace(/[-:T]/g, '').substring(0, 17)
    const sideVal = order.side === 'buy' ? '1' : '2'

    let ordType = '2' // Limit
    let tif = '0' // Day
    if (order.orderType === 'market') {
      ordType = '1'
    } else if (order.orderType === 'ioc') {
      ordType = '2'
      tif = '3' // IOC
    } else if (order.orderType === 'fok') {
      ordType = '2'
      tif = '4' // FOK
    }

    if (order.timeInForce) {
      tif = order.timeInForce
    }

    const fields: Record<number, string> = {
      35: 'D', // NewOrderSingle
      49: this.senderCompId,
      56: this.targetCompId,
      34: String(this.seqNum++),
      52: now,
      11: order.clOrdId,
      55: order.symbol,
      54: sideVal,
      40: ordType,
      38: String(order.quantity),
      59: tif,
      60: now,
    }

    if (order.price !== undefined && ordType !== '1') {
      fields[44] = String(order.price)
    }

    this.send(fields)
  }

  sendCancel(origClOrdId: string, clOrdId: string, symbol: string, side: string): void {
    const now = new Date().toISOString().replace(/[-:T]/g, '').substring(0, 17)
    const sideVal = side === 'buy' ? '1' : '2'

    this.send({
      35: 'F', // OrderCancelRequest
      49: this.senderCompId,
      56: this.targetCompId,
      34: String(this.seqNum++),
      52: now,
      41: origClOrdId,
      11: clOrdId,
      55: symbol,
      54: sideVal,
      60: now,
    })
  }

  sendMarketDataRequest(symbol: string, depth: number = 20): void {
    const now = new Date().toISOString().replace(/[-:T]/g, '').substring(0, 17)
    const reqId = `MDR-${Date.now()}`

    // Build the raw message manually to handle repeating group (duplicate tag 269)
    let body = ''
    body += `35=V\x01`
    body += `49=${this.senderCompId}\x01`
    body += `56=${this.targetCompId}\x01`
    body += `34=${this.seqNum++}\x01`
    body += `52=${now}\x01`
    body += `262=${reqId}\x01`    // MDReqID
    body += `263=0\x01`           // SubscriptionRequestType: 0=Snapshot
    body += `264=${depth}\x01`    // MarketDepth
    body += `267=2\x01`           // NoMDEntryTypes
    body += `269=0\x01`           // MDEntryType: Bid
    body += `269=1\x01`           // MDEntryType: Offer
    body += `146=1\x01`           // NoRelatedSym
    body += `55=${symbol}\x01`    // Symbol

    const header = `8=FIX.4.4\x01` + `9=${body.length}\x01`
    const raw = header + body
    let sum = 0
    for (let i = 0; i < raw.length; i++) {
      sum += raw.charCodeAt(i)
    }
    const checksum = (sum % 256).toString().padStart(3, '0')
    const msg = raw + `10=${checksum}\x01`

    if (!this.socket || !this._connected) {
      throw new Error('Not connected to exchange')
    }
    this.socket.write(msg, 'latin1')
    this.emit('messageSent', { 35: 'V', 55: symbol, 262: reqId })
  }

  sendModify(
    origClOrdId: string,
    clOrdId: string,
    symbol: string,
    side: string,
    price: number,
    qty: number
  ): void {
    const now = new Date().toISOString().replace(/[-:T]/g, '').substring(0, 17)
    const sideVal = side === 'buy' ? '1' : '2'

    this.send({
      35: 'G', // OrderCancelReplaceRequest
      49: this.senderCompId,
      56: this.targetCompId,
      34: String(this.seqNum++),
      52: now,
      41: origClOrdId,
      11: clOrdId,
      55: symbol,
      54: sideVal,
      40: '2', // Limit
      44: String(price),
      38: String(qty),
      60: now,
    })
  }

  private send(fields: Record<number, string>): void {
    if (!this.socket || !this._connected) {
      throw new Error('Not connected to exchange')
    }
    const msg = buildFixMessage(fields)
    this.socket.write(msg, 'latin1')
    this.emit('messageSent', fields)
  }

  private sendLogon(): void {
    const now = new Date().toISOString().replace(/[-:T]/g, '').substring(0, 17)
    const msg = buildFixMessage({
      35: 'A', // Logon
      49: this.senderCompId,
      56: this.targetCompId,
      34: String(this.seqNum++),
      52: now,
      108: '30', // HeartBtInt
      98: '0',  // EncryptMethod = None
    })
    this.socket!.write(msg, 'latin1')
  }

  private startHeartbeat(): void {
    this.heartbeatInterval = setInterval(() => {
      if (this._connected && this.socket) {
        const now = new Date().toISOString().replace(/[-:T]/g, '').substring(0, 17)
        const msg = buildFixMessage({
          35: '0', // Heartbeat
          49: this.senderCompId,
          56: this.targetCompId,
          34: String(this.seqNum++),
          52: now,
        })
        try { this.socket.write(msg, 'latin1') } catch { /* ignore */ }
      }
    }, 30000)
  }

  private stopHeartbeat(): void {
    if (this.heartbeatInterval) {
      clearInterval(this.heartbeatInterval)
      this.heartbeatInterval = null
    }
  }

  private processBuffer(): void {
    while (true) {
      // Find a complete FIX message: starts with 8=FIX and ends with 10=xxx\x01
      const startIdx = this.buffer.indexOf('8=FIX')
      if (startIdx === -1) {
        this.buffer = ''
        return
      }

      // Find the checksum tag which terminates the message
      const checksumPattern = '10='
      const csIdx = this.buffer.indexOf(checksumPattern, startIdx)
      if (csIdx === -1) return

      const endIdx = this.buffer.indexOf('\x01', csIdx)
      if (endIdx === -1) return

      const rawMsg = this.buffer.substring(startIdx, endIdx + 1)
      this.buffer = this.buffer.substring(endIdx + 1)

      const fields = parseFixMessage(rawMsg)
      this.handleMessage(fields, rawMsg)
    }
  }

  private handleMessage(fields: Record<string, string>, rawMsg?: string): void {
    const msgType = fields['35']
    console.log(`[FIX-MSG] type=${msgType} fields=${JSON.stringify(fields).substring(0, 200)}`)

    switch (msgType) {
      case 'A': // Logon
        console.log('[FIX] Logon ack received')
        this.emit('logon')
        break

      case '5': // Logout
        this._connected = false
        this.emit('disconnected', fields['58'] || 'Logout received')
        break

      case '0': // Heartbeat
        break

      case '1': // TestRequest
        this.sendHeartbeatResponse(fields['112'] || '')
        break

      case '8': // ExecutionReport
        this.handleExecutionReport(fields)
        break

      case '9': // OrderCancelReject
        this.emit('cancelReject', {
          clOrdId: fields['11'] || '',
          origClOrdId: fields['41'] || '',
          ordStatus: fields['39'] || '',
          text: fields['58'] || 'Cancel rejected',
        })
        break

      case 'j': // BusinessMessageReject
        this.emit('businessReject', {
          refMsgType: fields['372'] || '',
          refSeqNum: parseInt(fields['45'] || '0'),
          reason: fields['380'] || '',
          text: fields['58'] || '',
        })
        break

      case '3': // Reject
        this.emit('reject', {
          refSeqNum: parseInt(fields['45'] || '0'),
          reason: fields['373'] || '',
          text: fields['58'] || '',
        })
        break

      case 'W': // MarketDataSnapshotFullRefresh
        this.handleMarketData(fields, rawMsg ?? '')
        break

      default:
        this.emit('unknownMessage', { msgType, fields })
    }
  }

  private handleExecutionReport(fields: Record<string, string>): void {
    const report: ExecutionReport = {
      clOrdId: fields['11'] || '',
      orderId: fields['37'] || '',
      execType: fields['150'] || fields['39'] || '',
      ordStatus: fields['39'] || '',
      symbol: fields['55'] || '',
      side: fields['54'] === '1' ? 'buy' : 'sell',
      price: parseFloat(fields['44'] || '0'),
      quantity: parseInt(fields['38'] || '0'),
      cumQty: parseInt(fields['14'] || '0'),
      leavesQty: parseInt(fields['151'] || '0'),
      lastPx: parseFloat(fields['31'] || '0'),
      lastQty: parseInt(fields['32'] || '0'),
      text: fields['58'] || '',
      transactTime: fields['60'] || '',
    }
    this.emit('executionReport', report)
  }

  private handleMarketData(_fields: Record<string, string>, rawMsg: string): void {
    // Parse repeating groups from raw message (tags 269/270/271 repeat)
    const allFields = parseFixFields(rawMsg)
    const symbol = allFields.find(f => f.tag === '55')?.value ?? ''

    const snapshot: MarketDataSnapshot = {
      symbol,
      bids: [],
      asks: [],
    }

    // Walk through fields sequentially to extract repeating MD entries
    let currentType: string | null = null
    let currentPrice = 0
    for (const { tag, value } of allFields) {
      if (tag === '269') {
        currentType = value  // 0=Bid, 1=Offer
        currentPrice = 0
      } else if (tag === '270') {
        currentPrice = parseFloat(value) || 0
      } else if (tag === '271') {
        const qty = parseInt(value) || 0
        if (currentType === '0') {
          snapshot.bids.push({ price: currentPrice, qty })
        } else if (currentType === '1') {
          snapshot.asks.push({ price: currentPrice, qty })
        }
      }
    }

    this.emit('marketData', snapshot)
  }

  private sendHeartbeatResponse(testReqId: string): void {
    if (!this.socket || !this._connected) return
    const now = new Date().toISOString().replace(/[-:T]/g, '').substring(0, 17)
    const msg = buildFixMessage({
      35: '0', // Heartbeat
      49: this.senderCompId,
      56: this.targetCompId,
      34: String(this.seqNum++),
      52: now,
      112: testReqId,
    })
    this.socket.write(msg, 'latin1')
  }
}
