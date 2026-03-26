# Как запустить задание с отладкой

Запуск задания с включенным GDB-режимом. 

## авить запрос с debug=true

```bash
curl -X POST https://api.lab.example.com/v1/jobs \
  -H "X-API-Key: lab_apikey_123" \
  -H "Content-Type: application/json" \
  -d '{
    "binary_b64": "AAAAAGV4YW1wbGUgYmluYXJ5IGNvbnRlbnQ=",
    "debug": true,
    "timeout_seconds": 3600
  }'
```

Сохраните `job_id`. 

## 2. Получить GDB-информацию

Через REST:

```bash
curl -H "X-API-Key: lab_apikey_123" \
  https://api.lab.example.com/v1/jobs/01H2X5J4K6/gdb-info
```

Или дождаться SSE-события с полем `gdb`. 

## 3. Подключиться GDB

```bash
gdb ./firmware.elf
(gdb) target remote 203.0.113.100:12345
(gdb) break main
(gdb) continue
```

Для автоматизации см. [[How-To/Connect GDB|gdb-connect.sh]]. 