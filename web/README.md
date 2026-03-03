# Edge-Healthd Web UI

A lightweight, real-time web dashboard for monitoring edge device health without SSH, cloud connectivity, or authentication.

## Overview

The Web UI provides a local dashboard that displays health status from edge-healthd's state.json file. It's designed for:
- On-site technicians with physical access
- Quick health checks without SSH
- Mobile-friendly viewing from any browser on the local network

## Stack

- **Frontend**: Preact + Signals + TypeScript + Tailwind CSS
- **Backend**: Go microserver with embedded assets
- **Communication**: WebSocket for real-time updates (HTTP polling fallback)

## Features

- Real-time health monitoring via WebSocket
- Dark/light theme with system preference detection
- Offline support with localStorage persistence
- Mobile-responsive design
- Security headers and local-only access restriction

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
| `-listen` | `:8080` | HTTP listen address |
| `-state` | `/data/edge/health/state.json` | Path to health state file |
| `-local-only` | `true` | Restrict access to private IPs |

### Environment Variables

None required. All configuration is via command-line flags.

## API Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/` | GET | Serve SPA (index.html) |
| `/api/health` | GET | Current health state JSON |
| `/ws/health` | GET | WebSocket for real-time updates |

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

### Security Headers

All responses include:
- `Content-Security-Policy: default-src 'self'`
- `X-Content-Type-Options: nosniff`
- `X-Frame-Options: DENY`
- `X-XSS-Protection: 1; mode=block`
- `Referrer-Policy: strict-origin-when-cross-origin`
- `Permissions-Policy` (restrictive)

## Systemd Service

Install and enable:

```bash
# Install binary
sudo install -m 755 build/edge-healthd-ui /usr/bin/

# Install service file
sudo install -m 644 web/systemd/edge-healthd-ui.service /lib/systemd/system/

# Reload and enable
sudo systemctl daemon-reload
sudo systemctl enable --now edge-healthd-ui

# Check status
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

## Project Structure

```
web/
├── CMakeLists.txt          # CMake build integration
├── README.md               # This file
├── server/                 # Go microserver
│   ├── go.mod
│   ├── main.go             # Entry point
│   ├── config.go           # Configuration
│   ├── server.go           # HTTP server
│   ├── handlers.go         # API handlers
│   ├── websocket.go        # WebSocket hub
│   ├── watcher.go          # inotify file watcher
│   ├── middleware.go       # Security middleware
│   ├── embed.go            # Static asset embedding
│   └── dist/               # Embedded frontend (built)
├── ui/                     # Preact frontend
│   ├── package.json
│   ├── tsconfig.json
│   ├── vite.config.ts
│   ├── tailwind.config.js
│   ├── index.html
│   └── src/
│       ├── main.tsx
│       ├── App.tsx
│       ├── types/          # TypeScript types
│       ├── state/          # Preact Signals
│       ├── hooks/          # React hooks
│       ├── components/     # UI components
│       └── views/          # Page views
├── docker/                 # Development containers
│   ├── Dockerfile.dev
│   └── docker-compose.yml
├── mock/                   # Mock data generator
│   ├── generate_state.py
│   └── sample_states/
└── systemd/                # Systemd service
    └── edge-healthd-ui.service
```

## License

MIT
