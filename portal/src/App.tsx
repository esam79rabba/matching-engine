import { useState, useCallback, useEffect } from 'react'
import { apiPost, apiGet } from './api'
import type { Page, AuthResponse, MeResponse } from './types'
import Sidebar from './components/Sidebar'
import Dashboard from './pages/Dashboard'
import Instruments from './pages/Instruments'
import Sessions from './pages/Sessions'
import KillSwitch from './pages/KillSwitch'
import Metrics from './pages/Metrics'
import EOD from './pages/EOD'
import AuditLog from './pages/AuditLog'
import AdminInstruments from './pages/AdminInstruments'
import AdminParticipants from './pages/AdminParticipants'

function App() {
  const [token, setToken] = useState(() => localStorage.getItem('ops_token') ?? '')
  const [userRole, setUserRole] = useState('')
  const [page, setPage] = useState<Page>('dashboard')
  const [loginError, setLoginError] = useState('')
  const [username, setUsername] = useState('')
  const [password, setPassword] = useState('')
  const [toasts, setToasts] = useState<{ id: number; msg: string; type: 'success' | 'error' }[]>([])

  const canWrite = userRole === 'admin' || userRole === 'operator'

  // Fetch user role on mount if token exists
  useEffect(() => {
    if (token) {
      apiGet<MeResponse>('/api/v1/auth/me').then((me) => {
        if (me?.role) {
          setUserRole(me.role)
        } else {
          // Token invalid
          setToken('')
          localStorage.removeItem('ops_token')
        }
      })
    }
  }, [token])

  const showToast = useCallback((msg: string, type: 'success' | 'error') => {
    const id = Date.now()
    setToasts((prev) => [...prev, { id, msg, type }])
    setTimeout(() => {
      setToasts((prev) => prev.filter((t) => t.id !== id))
    }, 3000)
  }, [])

  const handleLogin = async () => {
    const data = await apiPost<AuthResponse>('/api/v1/auth/login', { username, password })
    if (data?.token) {
      localStorage.setItem('ops_token', data.token)
      setToken(data.token)
      setLoginError('')
      setUsername('')
      setPassword('')
      const me = await apiGet<MeResponse>('/api/v1/auth/me')
      setUserRole(me?.role ?? 'viewer')
    } else {
      setLoginError('Invalid credentials')
    }
  }

  const handleLogout = () => {
    setToken('')
    setUserRole('')
    localStorage.removeItem('ops_token')
  }

  const handleKeyDown = (e: React.KeyboardEvent) => {
    if (e.key === 'Enter') handleLogin()
  }

  // Login screen
  if (!token) {
    return (
      <div className="login-wrap">
        <div className="login-box" onKeyDown={handleKeyDown}>
          <h2>Ops Dashboard</h2>
          <div className="login-error">{loginError}</div>
          <input
            placeholder="Username"
            autoComplete="username"
            value={username}
            onChange={(e) => setUsername(e.target.value)}
          />
          <input
            type="password"
            placeholder="Password"
            autoComplete="current-password"
            value={password}
            onChange={(e) => setPassword(e.target.value)}
          />
          <button onClick={handleLogin}>Sign In</button>
        </div>
      </div>
    )
  }

  // Authenticated shell
  const renderPage = () => {
    switch (page) {
      case 'dashboard':
        return <Dashboard />
      case 'instruments':
        return <Instruments canWrite={canWrite} showToast={showToast} />
      case 'sessions':
        return <Sessions />
      case 'killswitch':
        return <KillSwitch canWrite={canWrite} showToast={showToast} />
      case 'metrics':
        return <Metrics />
      case 'eod':
        return <EOD canWrite={canWrite} showToast={showToast} />
      case 'audit':
        return <AuditLog />
      case 'admin-instruments':
        return userRole === 'admin' ? (
          <AdminInstruments showToast={showToast} />
        ) : (
          <div className="empty">Admin access required</div>
        )
      case 'admin-participants':
        return userRole === 'admin' ? (
          <AdminParticipants showToast={showToast} />
        ) : (
          <div className="empty">Admin access required</div>
        )
      default:
        return <div className="empty">Unknown page</div>
    }
  }

  return (
    <>
      <div className="app">
        <Sidebar
          activePage={page}
          userRole={userRole}
          onNavigate={setPage}
          onLogout={handleLogout}
        />
        <div className="main">
          <div className="refresh-bar">
            <div className="refresh-dot" />
            Auto-refresh: 5s
          </div>
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
