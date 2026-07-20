# Edge-Healthd Web UI

A lightweight, real-time web dashboard for monitoring edge device health without SSH, cloud connectivity, or authentication.

## Overview

The Web UI provides a local dashboard that displays health status from edge-healthd's state.json file. It's designed for:
- On-site technicians with physical access
- Quick health checks without SSH
- Mobile-friendly viewing from any browser on the local network

## Stack

- **Frontend**: Preact + Signals + TypeScript + Tailwind CSS
- **Backend**: Go microserver with embedded assets; HTTPS in the shipped deployment (plain HTTP for local development)
- **Communication**: WebSocket for real-time updates (HTTP polling fallback)

## Features

- Real-time health monitoring via WebSocket
- HTTPS with a self-signed cert in the shipped systemd unit; plain HTTP when `-tls-cert`/`-tls-key` are omitted (development)
- Dark/light theme with system preference detection
- Offline support with localStorage persistence
- Mobile-responsive design
- Security headers, RFC1918 source-IP allowlist, CSRF guard on mutating endpoints

## Building

### Requirements

- Go 1.22+
- Node.js 20+
- npm 10+
- CMake 3.20+

### Native Build

```bash
# Enable web UI and build
cmake -B build -DEDGE_WEB_UI=ON
cmake --build build

# Binary output
ls -lh build/edge-healthd-ui
```

### Cross-Compile

```bash
# ARM64 (Raspberry Pi 5, i.MX93)
cmake -B build -DEDGE_WEB_UI=ON -DEDGE_WEB_GOARCH=arm64
cmake --build build

# RISC-V 64 (VisionFive2)
cmake -B build -DEDGE_WEB_UI=ON -DEDGE_WEB_GOARCH=riscv64
cmake --build build

# Verify
file build/edge-healthd-ui
```

## Development

### Using Docker Compose

The easiest way to develop is with Docker Compose:

```bash
cd web/docker
docker-compose up

# Frontend dev server: http://localhost:5173 (HMR enabled)
# Go server: http://localhost:8080
# Mock data is auto-generated
```

### Manual Development

```bash
# Terminal 1: Start mock data generator
cd web/mock
python generate_state.py --output /tmp/state.json --interval 5 --rotate

# Terminal 2: Start Go server
cd web/server
go run . -listen :8080 -state /tmp/state.json -local-only=false

# Terminal 3: Start frontend dev server
cd web/ui
npm install
npm run dev
```

Then open http://localhost:5173 in your browser.

## Configuration

### Server Flags

| Flag | Default | Description |
|------|---------|-------------|
| `-listen` | `:8443` | Listen address. HTTPS when `-tls-cert`/`-tls-key` are set, plain HTTP otherwise. |
| `-state` | `/data/edge/health/state.json` | Path to health state file. Yocto installs typically override this to `/run/health/state.json` via the systemd unit. |
| `-local-only` | `true` | Restrict access to private IPs (loopback + RFC1918 + IPv6 ULA/link-local). |
| `-tls-cert` | _empty_ | Path to TLS certificate (PEM). Must be set together with `-tls-key`; enables HTTPS. |
| `-tls-key` | _empty_ | Path to TLS private key (PEM). Must be set together with `-tls-cert`. |
| `-allowed-origins` | _empty_ | Extra Origins for WebSocket + mutating endpoints (comma-separated `host[:port]` or full URLs). Same-origin is always allowed; this is only needed for dev proxies (Vite). Leave empty in production. |
| `-version` | _flag_ | Print version and exit. |

### Environment Variables

None required. All configuration is via command-line flags.

## API Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/` | GET | Serve SPA (index.html) |
| `/api/health` | GET | Current health state JSON |
| `/api/trigger` | POST | Wake the daemon collection cycle. Requires `X-Edge-Health: 1` header and a same-origin (or `-allowed-origins`) Origin. |
| `/ws/health` | GET | WebSocket for real-time updates (same-origin only) |

## Security

### Local-Only Access

By default, the server only accepts connections from private IP addresses:
- `127.0.0.0/8` (loopback)
- `10.0.0.0/8` (private)
- `172.16.0.0/12` (private)
- `192.168.0.0/16` (private)
- `fe80::/10` (link-local IPv6)
- `fd00::/8` (ULA IPv6)

Disable with `-local-only=false` for development.

### Cross-Origin / CSRF Protections

- **WebSocket upgrades** enforce a same-origin `Origin` header. Use `-allowed-origins` to whitelist a dev-proxy Origin (e.g. `http://localhost:5173` for Vite).
- **`POST /api/trigger`** additionally requires the custom header `X-Edge-Health: 1`. Browsers cannot set this on a cross-origin POST without a CORS preflight, and the server never advertises `Access-Control-Allow-Headers`, so the preflight fails and the request never reaches the daemon. This blocks LAN-resident CSRF where a webpage opened by an operator could otherwise call `/api/trigger`.
- `LocalOnlyMiddleware` (source-IP allowlist) does **not** defend against this class of CSRF on its own, because the abusive request originates from the operator's browser, which has a private IP.

### Security Headers

All responses include:
- `Content-Security-Policy: default-src 'self'`
- `X-Content-Type-Options: nosniff`
- `X-Frame-Options: DENY`
- `X-XSS-Protection: 1; mode=block`
- `Referrer-Policy: strict-origin-when-cross-origin`
- `Permissions-Policy` (restrictive)

## Systemd Service

The shipped unit (`web/systemd/edge-healthd-ui.service`) runs HTTPS on `:8443`,
reads state from `/run/health/state.json`, and loads its cert/key from
`/etc/edge/tls/{cert,key}.pem`. A fresh install therefore needs a TLS cert in
place before the unit will come up cleanly.

For production targets, install via the Yocto recipe — `/usr/bin/` is typically
read-only on edge gateways and manual installs won't work.

For a writable host (dev VM, workstation):

```bash
# 1. Install binary and unit (writable rootfs only)
sudo install -m 755 build/edge-healthd-ui /usr/bin/
sudo install -m 644 web/systemd/edge-healthd-ui.service /lib/systemd/system/

# 2. Generate a self-signed cert (skips if a valid one already exists)
sudo edge-healthd-gen-cert            # installed by `cmake --install`
#   — or, from a source tree:
sudo scripts/gen-tls-cert.sh

# 3. Reload and enable
sudo systemctl daemon-reload
sudo systemctl enable --now edge-healthd-ui

# 4. Check status
systemctl status edge-healthd-ui
journalctl -u edge-healthd-ui -f
```

## Performance Targets

| Metric | Target | Actual |
|--------|--------|--------|
| Bundle size (gzip) | < 500KB | ~50KB |
| Server memory | < 30MB | ~15MB |
| First paint | < 500ms | ~200ms |
| CPU idle | < 1% | <0.5% |

## Project Layout

| Directory | Purpose |
|-----------|---------|
| `server/` | Go microserver — serves the embedded SPA, `/api/health`, `/api/trigger`, and `/ws/health`; bridges to the daemon over D-Bus. |
| `ui/` | Preact + TypeScript + Tailwind frontend; built with Vite, output embedded into the Go binary at build time. |
| `docker/` | Local development containers (Vite HMR + Go server + mock data). |
| `mock/` | Mock `state.json` generator for UI development without a running daemon. |
| `systemd/` | Production systemd unit for `edge-healthd-ui`. |

Build integration lives alongside these directories: it invokes `npm` and `go build`, embeds the built assets into the Go binary, and installs the binary plus systemd unit.

## License

MIT
