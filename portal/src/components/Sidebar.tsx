import type { Page } from '../types'

interface SidebarProps {
  activePage: Page
  userRole: string
  onNavigate: (page: Page) => void
  onLogout: () => void
}

interface NavSection {
  title: string
  pages: { id: Page; label: string }[]
  adminOnly?: boolean
}

const sections: NavSection[] = [
  {
    title: 'Operations',
    pages: [
      { id: 'dashboard', label: 'Dashboard' },
      { id: 'instruments', label: 'Instruments' },
      { id: 'sessions', label: 'FIX Sessions' },
      { id: 'killswitch', label: 'Kill Switch' },
      { id: 'metrics', label: 'Metrics' },
      { id: 'eod', label: 'EOD Status' },
      { id: 'audit', label: 'Audit Log' },
    ],
  },
  {
    title: 'Trading',
    pages: [
      { id: 'order-entry', label: 'Order Entry' },
      { id: 'order-book', label: 'Order Book' },
    ],
  },
  {
    title: 'History',
    pages: [
      { id: 'trade-history', label: 'Trade History' },
      { id: 'order-history', label: 'Order History' },
    ],
  },
  {
    title: 'Accounts',
    pages: [
      { id: 'account-credit', label: 'Credit / Exposure' },
      { id: 'positions', label: 'Positions' },
    ],
  },
  {
    title: 'Compliance',
    pages: [
      { id: 'surveillance', label: 'Surveillance Alerts' },
      { id: 'trade-audit', label: 'Trade Audit Trail' },
    ],
  },
  {
    title: 'Settlement',
    pages: [
      { id: 'settlement', label: 'Settlement Status' },
      { id: 'reconciliation', label: 'Reconciliation' },
    ],
  },
  {
    title: 'Admin',
    adminOnly: true,
    pages: [
      { id: 'admin-instruments', label: 'Instruments' },
      { id: 'admin-participants', label: 'Participants' },
      { id: 'admin-session-config', label: 'Session Config' },
    ],
  },
]

export default function Sidebar({ activePage, userRole, onNavigate, onLogout }: SidebarProps) {
  return (
    <div className="sidebar">
      {sections.map((section) => {
        if (section.adminOnly && userRole !== 'admin') return null
        return (
          <div key={section.title}>
            <div className={`sidebar-title ${section.adminOnly ? 'admin-title' : ''}`}>
              {section.title}
            </div>
            {section.pages.map(({ id, label }) => (
              <button
                key={id}
                className={`nav-link ${activePage === id ? 'active' : ''}`}
                onClick={() => onNavigate(id)}
              >
                {label}
              </button>
            ))}
          </div>
        )
      })}
      <div className="env-badge">PILOT</div>
      <button className="nav-link logout" onClick={onLogout}>
        Logout
      </button>
    </div>
  )
}
