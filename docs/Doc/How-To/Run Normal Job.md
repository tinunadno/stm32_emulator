# Как запустить обычное задание

Обычное задание запускает симуляцию без GDB-отладки. 

## Пример запроса

```bash
curl -X POST https://api.lab.example.com/v1/jobs \
  -H "X-API-Key: lab_ap " \
  -H "Content-Type: application/json" \
  -d '{
    "binary_b64": "AAAAAGV4YW1wbGUgYmluYXJ5IGNvbnRlbnQ="
  }'
```

В ответ придет `job_id`, `status_url` и `events_url` для слежения за выполнением. 