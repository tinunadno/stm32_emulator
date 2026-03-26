# REST API: GDB Info

Этот эндпоинт возвращает информацию для подключения GDB к конкретному заданию. 

## GET /v1/jobs/{job_id}/gdb-info

Пример ответа:

```json
{
  "job_id": "01H2X5J4K6",
  "debug_enabled": true,
  "gdb_host": "worker-01.lab.internal",
  "gdb_port": 12345,
  "connection_string": "target remote worker-01.lab.internal:12345",
  "status": "listening",
  "connected": false
}
```

Клиент может использовать поле `connection_string` напрямую в GDB:

```bash
gdb ./firmware.elf -ex "target remote worker-01.lab.internal:12345"
```

Этот эндпоинт дополняет SSE-поток, обеспечивая "pull" способ получить данные о GDB-сессии. 