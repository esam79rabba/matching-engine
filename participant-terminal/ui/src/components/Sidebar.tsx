import type { Page } from '../types'

interface Props {
  activePage: Page
  connected: boolean
  onNavigate: (page: Page) => void
}

const navItems: Array<{ page: Page; label: string; section?: string }> = [
  { page: 'dashboard', label: 'Dashboard', section: 'Overview' },
  { page: 'order-entry', label: 'Order Entry', section: 'Trading' },
  { page: 'order-book', label: 'Order Book' },
  { page: 'orders', label: 'Orders' },
  { page: 'trades', label: 'Trades' },
  { page: 'positions', label: 'Positions' },
  { page: 'market-data', label: 'Market Data', section: 'Data' },
  { page: 'settings', label: 'Settings', section: 'Config' },
]

export default function Sidebar({ activePage, connected, onNavigate }: Props) {
  let lastSection = ''

  return (
    <div className="sidebar">
      <div className="sidebar-brand">Participant Terminal</div>
      {navItems.map((item) => {
        const showSection = item.section && item.section !== lastSection
        if (item.section) lastSection = item.section
        return (
          <div key={item.page}>
            {showSection && <div className="sidebar-section">{item.section}</div>}
            <button
              className={`nav-link${activePage === item.page ? ' active' : ''}`}
              onClick={() => onNavigate(item.page)}
            >
              {item.label}
            </button>
          </div>
        )
      })}
      <div className={`connection-badge ${connected ? 'connected' : 'disconnected'}`}>
        <div className={`connection-dot ${connected ? 'on' : 'off'}`} />
        {connected ? 'FIX Connected' : 'Disconnected'}
      </div>
    </div>
  )
}
