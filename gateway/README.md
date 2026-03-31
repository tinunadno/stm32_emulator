# STM32 Simulator Gateway

API Gateway for the STM32 simulator with GDB debugging support.

## Overview

This service provides a REST API for submitting firmware binaries for simulation and managing simulation jobs with GDB debugging support.
## Architecture
```
┌─────────────┐     ┌─────────────────────────────────────┐     ┌──────────────┐
│   Client   │────>│         API Gateway (Go)               │────>│   Postgres   │
│ (browser)  │     │  - REST API                          │     │  - audit     │
│           │     │  - SSE streams                       │     │  - history   │
│           │     │  - Job scheduling                   │     └──────────────┘
│           │     └───────────────┬─────────────────────────────┘
│           │                    │         KeyDB               │
│           │                    │  - Job queues         │
│           │                    │  - Pub/Sub events     │
│           │                    │  - Binary cache       │
└───────────┘                    └───────────────────────────┘
```
## Features
- **Job Management**: Create, monitor, and cancel simulation jobs
- **GDB Debugging**: Remote debugging via GDB Remote Serial Protocol
- **SSE Events**: Real-time job status updates via Server-Sent Events
- **Async Processing**: Non-blocking job execution via KeyDB queues
## API Endpoints
### Create Job
```http
POST /v1/jobs
Content-Type: application/json
X-API-Key: <your-api-key>

{
  "binary_b64": "<base64-encoded-firmware>",
  "debug": true,
  "timeout_seconds": 60
}
**Response** (202 Accepted):
```json
{
  "job_id": "01H2X5J4K6",
  "sha256": "e3b0c44...",
  "debug": true,
  "status_url": "/v1/jobs/01H2X5J4K6",
  "events_url": "/v1/jobs/01H2X5J4K6/events"
}
```
### Get Job Status
```http
GET /v1/jobs/{job_id}
X-API-Key: <your-api-key>
```
**Response** (200 OK):
```json
{
  "job_id": "01H2X5J4K6",
  "state": "running",
  "debug": true,
  "gdb_host": "192.168.1.100",
  "gdb_port": 3333
}
```
### Get GDB Info
```http
GET /v1/jobs/{job_id}/gdb-info
X-API-Key: <your-api-key>
```
**Response** (200 OK):
```json
{
  "job_id": "01H2X5J4K6",
  "debug_enabled": true,
  "gdb_host": "192.168.1.100",
  "gdb_port": 3333,
  "connection_string": "target remote 192.168.1.100:3333",
  "status": "listening",
  "connected": false
}
```
### SSE Events Stream
```http
GET /v1/jobs/{job_id}/events
Accept: text/event-stream
```
**Event Types:**
- `status`: Job state changes
- `log`: Simulation output
- `gdb`: GDB connection info
- `error`: Error messages
### Health Check
```http
GET /health
```
**Response** (200 OK):
```json
{
  "status": "healthy"
}
```
## Configuration
Configuration is loaded from `config.yaml` or environment variables:
```yaml
server:
  host: "0.0.0.0"
  port: 8080
database:
  host: "localhost"
  port: 5432
  user: "lab"
  password: "secret"
  database: "lab"
  sslmode: "disable"
keydb:
  addr: "localhost:6379"
  password: ""
  db: 0
worker:
  simulator_binary: "./stm32sim"
  gdb_port_range:
    start: 3333
    end: 3343
  debug_timeout_seconds: 3600
```
### Environment Variables
| Variable | Description | Default |
|----------|-------------|---------|
| `SERVER_HOST` | Server host | `0.0.0.0` |
| `SERVER_PORT` | Server port | `8080` |
| `KEYDB_ADDR` | KeyDB address | `localhost:6379` |
| `DB_DSN` | PostgreSQL DSN | - |
| `SIMULATOR_BINARY` | Path to simulator binary | `./stm32sim` |
## Running Locally
### Prerequisites
- Go 1.21+
- PostgreSQL 14+
- KeyDB (or Redis)
### Using Make
```bash
# Build
make build
# Run tests
make test
# Run locally
make run
```
### Using Docker
```bash
# Build image
make docker
# Run with docker-compose
docker-compose up -d
```
## GDB Debugging
1. Submit job with `debug: true`
2. Get GDB connection info from `/v1/jobs/{job_id}/gdb-info`
3. Connect with arm-none-eabi-gdb:
```bash
arm-none-eabi-gdb ./firmware.elf
(gdb) target remote <gdb_host>:<gdb_port>
```
## Project Structure
```
gateway/
├── cmd/gateway/main.go      # Entry point
├── internal/
│   ├── config/config.go     # Configuration
│   ├── handlers/             # HTTP handlers
│   │   ├── jobs.go
│   │   └── health.go
│   ├── models/job.go         # Data models
│   ├── repository/           # Data access
│   │   ├── postgres.go
│   │   └── keydb.go
│   └── service/               # Business logic
│       └── job_service.go
├── pkg/sse/sse.go            # SSE utilities
├── config.yaml               # Configuration file
├── Dockerfile
├── docker-compose.yml
├── Makefile
├── go.mod
└── README.md
```
## License
MIT
