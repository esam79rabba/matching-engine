import type { ReactNode } from 'react'

interface Column<T> {
  key: string
  header: string
  render: (row: T) => ReactNode
}

interface Props<T> {
  columns: Column<T>[]
  data: T[]
  emptyText?: string
}

export default function Table<T>({ columns, data, emptyText = 'No data' }: Props<T>) {
  if (data.length === 0) {
    return <div className="empty">{emptyText}</div>
  }

  return (
    <table>
      <thead>
        <tr>
          {columns.map((col) => (
            <th key={col.key}>{col.header}</th>
          ))}
        </tr>
      </thead>
      <tbody>
        {data.map((row, i) => (
          <tr key={i}>
            {columns.map((col) => (
              <td key={col.key}>{col.render(row)}</td>
            ))}
          </tr>
        ))}
      </tbody>
    </table>
  )
}
