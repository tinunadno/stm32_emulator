# SSE-события

Клиент может подписаться на поток событий по заданию через SSE. 

## Эндпоинт

`GET /v1/jobs/{job_id}/events`

Gateway подписывается на `events:job:{job_id}` в KeyDB и транслирует события клиенту как SSE. 

## Пример события статуса (debug)

```json
{
  "type": "status",
  "state": "running",
  "worker_id": "worker-01",
  "debug": true,
  "gdb": {
    "host": "worker-01.lab.internal",
    "port": 12345,
    "protocol": "tcp"
  },
  "timestamp": "2024-01-01T12:00:05Z"
}
```

Дополнительно могут приходить события логов и телеметрии с типами `log`, `telemetry` и т.п. 