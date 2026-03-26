# REST API: /jobs

Этот документ описывает создание задач на симуляцию, в том числе с режимом отладки. 

## POST /v1/jobs

Создает новое задание.

### Тело запроса

```yaml
type: object
required:
  - binary_b64
properties:
  binary_b64:
    type: string
    description: Base64-encoded бинарный файл прошивки
  debug:
    type: boolean
    default: false
    description: Запустить с поддержкой GDB-отладки
  timeout_seconds:
    type: integer
    default: 30
    minimum: 5
    maximum: 300
```

### Ответ 202 Accepted

```json
{
  "job_id": "01H2X5J4K6",
  "sha256": "e3b0c44...",
  "debug": true,
  "status_url": "https://api.lab.example.com/v1/jobs/01H2X5J4K6",
  "events_url": "https://api.lab.example.com/v1/jobs/01H2X5J4K6/events"
}
```

Примеры запросов приведены в [[How-To/Run Normal Job|Run Normal Job]] и [[How-To/Run Debug Job|Run Debug Job]]. 