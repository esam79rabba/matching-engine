const BASE_URL =
  window.location.hostname === 'localhost'
    ? 'http://localhost:8180'
    : `http://${window.location.hostname}:18080`

function getHeaders(): Record<string, string> {
  const headers: Record<string, string> = { 'Content-Type': 'application/json' }
  const token = localStorage.getItem('ops_token')
  if (token) headers['Authorization'] = `Bearer ${token}`
  return headers
}

export async function apiGet<T>(path: string): Promise<T | null> {
  try {
    const res = await fetch(`${BASE_URL}${path}`, { headers: getHeaders() })
    if (res.status === 401) {
      localStorage.removeItem('ops_token')
      window.location.reload()
      return null
    }
    return await res.json()
  } catch {
    return null
  }
}

export async function apiPost<T>(path: string, body?: unknown): Promise<T | null> {
  try {
    const res = await fetch(`${BASE_URL}${path}`, {
      method: 'POST',
      headers: getHeaders(),
      body: body ? JSON.stringify(body) : undefined,
    })
    if (res.ok) return await res.json()
    return null
  } catch {
    return null
  }
}

export async function apiPut<T>(path: string, body: unknown): Promise<T | null> {
  try {
    const res = await fetch(`${BASE_URL}${path}`, {
      method: 'PUT',
      headers: getHeaders(),
      body: JSON.stringify(body),
    })
    return await res.json()
  } catch {
    return null
  }
}
