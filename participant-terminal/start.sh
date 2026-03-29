#!/bin/bash
set -e

echo "=== Participant Terminal ==="
echo "  FIX:          ${FIX_HOST:-localhost}:${FIX_PORT:-9876}"
echo "  Exchange API:  ${EXCHANGE_URL:-http://localhost:8180}"
echo "  Server:        http://localhost:3001"
echo "  UI:            http://localhost:5174"
echo ""

DIR="$(cd "$(dirname "$0")" && pwd)"

# Install dependencies if needed
if [ ! -d "$DIR/server/node_modules" ]; then
  echo "Installing server dependencies..."
  cd "$DIR/server" && npm install
fi
if [ ! -d "$DIR/ui/node_modules" ]; then
  echo "Installing UI dependencies..."
  cd "$DIR/ui" && npm install
fi

# Start both processes
cd "$DIR/server" && npx tsx src/index.ts &
SERVER_PID=$!

cd "$DIR/ui" && npx vite --port 5174 &
UI_PID=$!

trap "kill $SERVER_PID $UI_PID 2>/dev/null; exit" INT TERM

echo ""
echo "Both processes started. Press Ctrl+C to stop."
wait
