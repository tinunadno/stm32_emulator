# Docker Compose для системы

Пример `docker-compose.yml` для запуска всей платформы с поддержкой GDB. 

```yaml
version: '3.8'

services:
  keydb:
    image: eqalpha/keydb:latest
    ports:
      - "6379:6379"

  postgres:
    image: postgres:14
    environment:
      POSTGRES_DB: lab
      POSTGRES_USER: lab
      POSTGRES_PASSWORD: secret

  gateway:
    build: ./gateway
    ports:
      - "8080:8080"
      - "8081:8081"  # метрики
    environment:
      KEYDB_ADDR: keydb:6379
      DB_DSN: postgres://lab:secret@postgres:5432/lab?sslmode=disable
    depends_on:
      - keydb
      - postgres

  worker:
    build: ./worker
    # Пробрасываем диапазон портов для GDB
    ports:
      - "1234-1244:1234-1244"
    # Для реального IP воркера можно использовать host network
    network_mode: host
    environment:
      KEYDB_ADDR: keydb:6379
      WORKER_ID: ${HOSTNAME}
      DEBUG_ENABLED: "true"
      PUBLIC_IP: ${PUBLIC_IP}
    depends_on:
      - keydb
    deploy:
      replicas: 3
```

При использовании `network_mode: host` важно корректно указать `PUBLIC_IP`, который будет отдаваться клиенту для подключения GDB. 