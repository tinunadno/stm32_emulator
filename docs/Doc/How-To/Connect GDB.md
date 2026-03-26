# Скрипт подключения GDB

Удобный скрипт для автоматического подключения GDB к удаленному заданию. 

```bash
#!/bin/bash
# gdb-connect.sh - подключается к удаленному заданию

JOB_ID=$1
API_KEY=$2
API_ENDPOINT="https://api.lab.example.com/v1"

# Получаем информацию о GDB
GDB_INFO=$(curl -s -H "X-API-Key: $API_KEY" \
    $API_ENDPOINT/jobs/$JOB_ID/gdb-info)

HOST=$(echo $GDB_INFO | jq -r .gdb_host)
PORT=$(echo $GDB_IN -r .gdb_port)
ELF_FILE=${3:-./firmware.elf}

# Подключаемся
gdb $ELF_FILE -ex "target remote $HOST:$PORT"
```

Использование:

```bash
./gdb-connect.sh 01H2X5J4K6 lab_apikey_123 build/firmware.elf
```