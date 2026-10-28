# Exchange Operations Portal

React + Vite UI for exchange operators: session view, risk admin, surveillance alerts, EOD controls. Talks to the daemon's REST API and WebSocket feed.

## Prerequisites

- Node.js 20+
- A running `matching-engine-daemon` (see the top-level [README](../README.md)) exposing REST on `18080` and WebSocket on `9880` by default.

## Dev

```bash
cd portal
npm install
npm run dev          # vite dev server on http://localhost:5173
```

The dev server proxies API calls to the daemon on `localhost:18080`. If the daemon is on another host or port, set it in `vite.config.ts` or point your browser at the deployed origin.

## Build

```bash
npm run build        # type-check + bundle to portal/dist
npm run preview      # preview the production bundle
```

## Auth

The portal signs in against the daemon's `/api/v1/auth/login`. Default users seeded by the daemon:

- `admin` / `${OPS_API_KEY}` (or `${PORTAL_PASSWORD}` when set)
- `operator` / `${OPS_API_KEY}`
- `viewer` / `viewer`

Set `OPS_API_KEY` and (ideally) `PORTAL_PASSWORD` in the daemon's environment; see the project [`.env.example`](../.env.example).

## Status

Prototype. Covers the ops flows the daemon exposes today; no automated tests yet — tracked in [`docs/delivery/OSS-READINESS-TRACKER.md`](../docs/delivery/OSS-READINESS-TRACKER.md).
