# Gateway Service

API Gateway for the Online Laboratory System with GDB Debugging Support.

## Overview

This service is the entry point for the online laboratory system that allows clients to submit binary files (firmware) for simulation on virtual boards. It supports:

- **REST API** for job management
- **SSE (Server-Sent Events)** for real-time event streaming
- **GDB Debugging** support through worker connections
- **Authentication** via API keys

## Architecture

```
┌─────────────┐     ┌─────────────────────────────────────┐     ┌──────────────┐
│   Client    │────▶│         API Gateway (Go)            │────▶│   Postgres   │
│  (browser)  │◀────│  - REST API                         │     │  - audit     │
│             │     │  - SSE streams                      │     │  - history   │
│             │     │  - Publish to KeyDB                 │     └──────────────┘
└─────────────┘     └───────────────┬─────────────────────┘
                                    │
                                    ▼
                    ┌─────────────────────────────┐
                    │         KeyDB               │
                    │  - Queues: pending/processing│
                    │  - Hash: job:{id}           │
                    │  - Pub/Sub: events          │
                    └─────────────────────────────┘
```

## API Endpoints

### Jobs

| Method | Endpoint | Description |
|--------|----------|-------------|
| POST | `/v1/jobs` | Create a new simulation job |
| GET | `/v1/jobs` | List user's jobs |
| GET | `/v1/jobs/{job_id}` | Get job status |
| DELETE | `/v1/jobs/{job_id}` | Cancel a job |
| GET | `/v1/jobs/{job_id}/gdb-info` | Get GDB connection info |
| GET | `/v1/jobs/{job_id}/events` | SSE stream for job events |

### Health

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/health/live` | Liveness probe |
| GET | `/health/ready` | Readiness probe |
| GET | `/health/details` | Detailed health info |
| GET | `/metrics` | Prometheus metrics |

## Configuration

Configuration is loaded from `config.yaml` and can be overridden with environment variables:

| Variable | Description |
|----------|-------------|
| `KEYDB_ADDR` | KeyDB address (default: localhost:6379) |
| `DB_DSN` | PostgreSQL connection string |
| `HTTP_PORT` | HTTP server port (default: 8080) |
| `LOG_LEVEL` | Logging level (default: info) |

## Quick Start

### Prerequisites

- Go 1.21+
- PostgreSQL 14+
- KeyDB 6.2+ (or Redis)

### Running Locally

```bash
# Install dependencies
make deps

# Build the binary
make build

# Run the service
make run
```

### Running with Docker

```bash
# Build and start all services
make docker-build
make docker-up

# View logs
make docker-logs

# Stop services
make docker-down
```

## API Usage Examples

### Create a Job

```bash
curl -X POST http://localhost:8080/v1/jobs \
  -H "X-API-Key: lab_apikey_123" \
  -H "Content-Type: application/json" \
  -d '{
    "binary_b64": "AAAAAGV4YW1wbGUgYmluYXJ5IGNvbnRlbnQ=",
    "debug": false,
    "timeout_seconds": 30
  }'
```

Response:
```json
{
  "job_id": "01H2X5J4K6",
  "sha256": "e3b0c44298fc1c149afbf4c8996fb924",
  "debug": false,
  "status_url": "http://localhost:8080/v1/jobs/01H2X5J4K6",
  "events_url": "http://localhost:8080/v1/jobs/01H2X5J4K6/events"
}
```

### Subscribe to Events (SSE)

```javascript
const eventSource = new EventSource(
  'http://localhost:8080/v1/jobs/01H2X5J4K6/events',
  { headers: { 'X-API-Key': 'lab_apikey_123' } }
);

eventSource.addEventListener('status', (event) => {
  console.log('Status:', JSON.parse(event.data));
});

eventSource.addEventListener('log', (event) => {
  console.log('Log:', JSON.parse(event.data));
});
```

### Create Debug Job and Connect GDB

```bash
# Create debug job
curl -X POST http://localhost:8080/v1/jobs \
  -H "X-API-Key: lab_apikey_123" \
  -H "Content-Type: application/json" \
  -d '{
    "binary_b64": "...",
    "debug": true,
    "timeout_seconds": 3600
  }'

# Get GDB info
curl http://localhost:8080/v1/jobs/01H2X5J4K6/gdb-info \
  -H "X-API-Key: lab_apikey_123"

# Connect with GDB
gdb ./firmware.elf
(gdb) target remote <gdb_host>:<gdb_port>
```

## Project Structure

```
gateway/
├── cmd/
│   └── gateway/
│       └── main.go          # Application entry point
├── internal/
│   ├── config/
│   │   └── config.go        # Configuration loading
│   ├── handlers/
│   │   ├── jobs.go          # Job HTTP handlers
│   │   ├── events.go        # SSE event handlers
│   │   └── health.go        # Health check handlers
│   ├── middleware/
│   │   ├── auth.go          # API key authentication
│   │   └── logging.go       # Request logging
│   ├── models/
│   │   └── job.go           # Data models
│   ├── repository/
│   │   ├── postgres.go      # PostgreSQL operations
│   │   └── keydb.go         # KeyDB operations
│   └── service/
│       └── job_service.go   # Business logic
├── pkg/
│   └── sse/
│       └── sse.go           # SSE broker implementation
├── config.yaml              # Configuration file
├── docker-compose.yml       # Docker Compose setup
├── Dockerfile               # Container build file
├── Makefile                 # Build automation
├── go.mod                   # Go module file
└── README.md                # This file
```

## Development

### Running Tests

```bash
make test
```

### Code Formatting

```bash
make fmt
```

### Linting

```bash
make lint
```

## License

MIT License
