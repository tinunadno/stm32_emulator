# Архитектура с поддержкой GDB

Этот документ описывает, как в архитектуру встроена удаленная отладка через GDB. 

## Поток данных (debug-режим)

1. Клиент отправляет `POST /v1/jobs` с бинарником в base64 и `debug: true`. 
2. API Gateway:
   - декодирует бинарник, считает SHA-256;
   - сохраняет запись в PostgreSQL;
   - сохраняет метаданные задания в KeyDB (`job:{id}`, включая `debug=true`);
   - добавляет ID задания в очередь `jobs:pending`. 
1. Worker:
   - получает `job_id` через `BRPOPLPUSH jobs:pending jobs:processing`;
   - если `debug=true`, запускает симулятор с GDB-сервером на случайном TCP-порту из диапазона;
   - пишет в `job:{id}` поля `state=running`, `gdb_port`, `gdb_host`, `gdb_connected=false`;
   - публикует событие `status:running` в `events:job:{id}`. 
1. API Gateway:
   - подписывается на `events:job:{id}` и отдает события клиенту по SSE;
   - в событии `running` клиент получает `gdb_host` и `gdb_port`. 
1. Разработчик:
   - запускает GDB локально с ELF-файлом прошивки;
   - выполняет `target remote <gdb_host>:<gdb_port>` и отлаживает код. 

## Роли компонентов

- Gateway сообщает только метаданные GDB-сервера (host/port), а не проксирует трафик. 
- Worker содержит симулятор и GDB-сервер (gdbstub) или подключенный backend (например, QEMU). 
- KeyDB хранит состояние debug-заданий и служит для сигнализации. 

Подробности протокола — в [[Implementation Details/GDB RSP|GDB Remote Serial Protocol]].  
Режимы работы воркера — в [[Debug Flow/Worker Modes|Worker Modes]].