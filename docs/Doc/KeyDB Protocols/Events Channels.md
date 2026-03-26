# KeyDB: Events Channels

События о ходе симуляции и GDB-сессии публикуются через Pub/Sub-каналы. 

## Канал events:job:{job_id}

Используется для:

- статусов (`queued`, `running`, `finished`, `error`);
- логов выполнения;
- телеметрии периферии;
- событий GDB (подключение, отключение, пароль и т.п.). 

Пример события о GDB:

```json
{
  "type": "gdb_auth",
  "password": "<generated_password>"
}
```

Gateway подписывается на этот канал и транслирует данные клиентам через SSE. 