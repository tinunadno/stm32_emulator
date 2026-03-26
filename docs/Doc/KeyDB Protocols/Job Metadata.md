# KeyDB: Job Metadata

Hash `job:{job_id}` хранит состояние задания и параметры GDB. 

## Поля для debug-режима

| Поле           | Тип     | Описание                                   |
|---------------|---------|--------------------------------------------|
| debug         | boolean | Флаг отладки                              |
| gdb_port      | integer | Порт GDB-сервера на воркере               |
| gdb_host      | string  | IP/хост воркера                            |
| gdb_password  | string  | (опционально) пароль для GDB-сессии       |
| gdb_connected | boolean | Подключился ли отладчик                   |

## Примеры

Создание debug-задания:

```redis
MULTI
HSET job:01H2X5J4K6 
    state queued 
    user_id user123 
    sha256 e3b0c44... 
    created_at 2024-01-01T12:00:00Z
    debug true
LPUSH jobs:pending 01H2X5J4K6
EXEC
```

Назначение порта воркером:

```redis
HSET job:01H2X5J4K6 
    state running 
    worker_id worker-01 
    started_at 2024-01-01T12:00:05Z
    gdb_port 12345
    gdb_host 192.168.1.100
    gdb_connected false

PUBLISH events:job:01H2X5J4K6 '{
    "type":"status",
    "state":"running",
    "debug":true,
    "gdb":{"host":"192.168.1.100","port":12345}
}'
```

Каналы событий описаны в [[KeyDB Protocols/Events Channels|Events Channels]]. 