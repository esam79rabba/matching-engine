const BASE = ''

export async function apiGet<T>(path: string): Promise<T | null> {
  try {
    const res = await fetch(`${BASE}${path}`)
    if (!res.ok) return null
    return await res.json() as T
  } catch {
    return null
  }
}

export async function apiPost<T>(path: string, body?: unknown): Promise<T | null> {
  try {
    const res = await fetch(`${BASE}${path}`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: body ? JSON.stringify(body) : undefined,
    })
    return await res.json() as T
  } catch {
    return null
  }
}

export function createWs(onMessage: (msg: { type: string; data: unknown }) => void): WebSocket {
  const proto = window.location.protocol === 'https:' ? 'wss' : 'ws'
  const wsUrl = `${proto}://${window.location.hostname}:3001`
  const ws = new WebSocket(wsUrl)

  ws.onmessage = (evt) => {
    try {
      const msg = JSON.parse(evt.data)
      onMessage(msg)
    } catch { /* ignore parse errors */ }
  }

  ws.onclose = () => {
    // Reconnect after 3 seconds
    setTimeout(() => {
      const newWs = createWs(onMessage)
      Object.assign(ws, newWs)
    }, 3000)
  }

  return ws
}
