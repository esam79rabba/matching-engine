interface ModalProps {
  title: string
  message: string
  onConfirm: () => void
  onCancel: () => void
}

export default function Modal({ title, message, onConfirm, onCancel }: ModalProps) {
  return (
    <div className="dialog-overlay" onClick={(e) => { if (e.target === e.currentTarget) onCancel() }}>
      <div className="dialog">
        <h3>{title}</h3>
        <p>{message}</p>
        <div className="dialog-actions">
          <button className="btn" onClick={onCancel}>Cancel</button>
          <button className="btn btn-danger" onClick={onConfirm}>Confirm</button>
        </div>
      </div>
    </div>
  )
}
