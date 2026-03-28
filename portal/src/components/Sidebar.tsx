import type { Page } from '../types'

interface SidebarProps {
  activePage: Page
  userRole: string
  onNavigate: (page: Page) => void
  onLogout: () => void
}

const opsPages: { id: Page; label: string }[] = [
  { id: 'dashboard', label: 'Dashboard' },
  { id: 'instruments', label: 'Instruments' },
  { id: 'sessions', label: 'FIX Sessions' },
  { id: 'killswitch', label: 'Kill Switch' },
  { id: 'metrics', label: 'Metrics' },
  { id: 'eod', label: 'EOD Status' },
  { id: 'audit', label: 'Audit Log' },
]

const adminPages: { id: Page; label: string }[] = [
  { id: 'admin-instruments', label: 'Instruments' },
  { id: 'admin-participants', label: 'Participants' },
  { id: 'admin-session-config', label: 'Session Config' },
]

export default function Sidebar({ activePage, userRole, onNavigate, onLogout }: SidebarProps) {
  return (
    <div className="sidebar">
      <div className="sidebar-title">Operations</div>
      {opsPages.map(({ id, label }) => (
        <button
          key={id}
          className={`nav-link ${activePage === id ? 'active' : ''}`}
          onClick={() => onNavigate(id)}
        >
          {label}
        </button>
      ))}
      {userRole === 'admin' && (
        <>
          <div className="sidebar-title admin-title">Admin</div>
          {adminPages.map(({ id, label }) => (
            <button
              key={id}
              className={`nav-link ${activePage === id ? 'active' : ''}`}
              onClick={() => onNavigate(id)}
            >
              {label}
            </button>
          ))}
        </>
      )}
      <div className="env-badge">PILOT</div>
      <button className="nav-link logout" onClick={onLogout}>
        Logout
      </button>
    </div>
  )
}
