# Participant Terminal

A trading terminal that connects to the matching engine as a FIX participant. Provides order entry, order book display, trade blotter, position tracking, and an automated trading mode.

## Quick Start

```bash
# Install dependencies
cd server && npm install && cd ../ui && npm install && cd ..

# Start both server and UI
./start.sh

# Or start individually:
cd server && npx tsx src/index.ts    # Backend on :3001
cd ui && npx vite --port 5174        # Frontend on :5174
```

## Configuration

Environment variables:

| Variable | Default | Description |
|----------|---------|-------------|
| `FIX_HOST` | `localhost` | Exchange FIX acceptor host |
| `FIX_PORT` | `9876` | Exchange FIX acceptor port |
| `SENDER_COMP_ID` | `PARTICIPANT1` | FIX SenderCompID |
| `TARGET_COMP_ID` | `EXCHANGE` | FIX TargetCompID |
| `EXCHANGE_URL` | `http://localhost:8180` | Exchange REST API URL |
| `PORT` | `3001` | Backend server port |

## Architecture

- **Server** (`server/`): Express + raw TCP FIX client + WebSocket broadcaster
- **UI** (`ui/`): React + Vite SPA with dark theme

The server maintains a raw TCP socket to the exchange FIX acceptor, builds/parses FIX 4.4 messages with SOH delimiters, and exposes a REST API + WebSocket for the React frontend.

## Pages

- **Dashboard** - Connection status, summary stats, instrument list
- **Order Entry** - Submit orders via FIX (limit, market, IOC, FOK)
- **Order Book** - Live L2 book from exchange (click price to prefill order)
- **Orders** - Active and historical orders with cancel support
- **Trades** - Fill blotter with running P&L
- **Positions** - Net position per instrument with unrealized P&L
- **Market Data** - Streaming instrument prices
- **Settings** - Connection config and auto-trader controls
