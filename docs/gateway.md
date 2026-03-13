# Техническая документация: Система онлайн-лаборатории с поддержкой GDB-отладки

## Версия 2.0 (с поддержкой удаленной отладки)

---

## Оглавление

1. [Обзор системы](#1-обзор-системы)
2. [Архитектура с поддержкой GDB](#2-архитектура-с-поддержкой-gdb)
3. [Компоненты](#3-компоненты)
4. [API Gateway (Внешнее API)](#4-api-gateway-внешнее-api)
5. [Протоколы KeyDB](#5-протоколы-keydb)
6. [Воркер с GDB-сервером](#6-воркер-с-gdb-сервером)
7. [Клиентская часть (GDB)](#7-клиентская-часть-gdb)
8. [Хранилище данных](#8-хранилище-данных)
9. [Безопасность и аутентификация](#9-безопасность-и-аутентификация)
10. [Observability (OpenTelemetry)](#10-observability-opentelemetry)
11. [Деплой и масштабирование](#11-деплой-и-масштабирование)
12. [Спецификация реализации](#12-спецификация-реализации)

---

## 1) Обзор системы

### 1.1 Назначение

Система онлайн-лаборатории позволяет клиентам отправлять бинарные файлы (прошивки) для симуляции на виртуальных платах. Клиенты получают поток событий в реальном времени: логи выполнения, телеметрию GPIO/UART/I2C/SPI, статусы симуляции. **Поддерживается удаленная отладка через GDB**.

### 1.2 Ключевые особенности

- **Асинхронная обработка** через KeyDB как единую шину данных
- **Масштабируемость** горизонтальная (больше воркеров = больше параллельных симуляций)
- **Отказоустойчивость** (при падении воркера задача возвращается в очередь)
- **SSE для клиентов** (простота интеграции с браузерами)
- **GDB-отладка** (remote debugging через TCP-туннель)
- **Полная наблюдаемость** через OpenTelemetry

### 1.3 Принципиальные решения

- ✅ **Хэширование**: SHA-256 от декодированного бинарника (в аудит и БД)
- ✅ **Клиентский протокол**: SSE (`GET /events`) для потоковой передачи
- ✅ **Gateway**: только планирование и маршрутизация, ничего не исполняет
- ✅ **Коммуникация**: KeyDB (очереди + Pub/Sub), никаких прямых вызовов
- ✅ **Воркеры**: Pull-модель, сами забирают задачи из очереди
- ✅ **Отладка**: Воркер открывает GDB-порт, информация пробрасывается клиенту

---

## 2) Архитектура с поддержкой GDB

### 2.1 Диаграмма компонентов с отладкой

```
┌─────────────┐     ┌─────────────────────────────────────┐     ┌──────────────┐
│   Клиент    │────▶│         API Gateway (Go)            │────▶│   Postgres   │
│  (браузер)  │◀────│  - REST API                         │     │  - аудит     │
│             │     │  - SSE потоки                       │     │  - история   │
│             │     │  - Публикация в KeyDB               │     └──────────────┘
└─────────────┘     └───────────────┬─────────────────────┘
                                    │
                                    ▼
                    ┌─────────────────────────────┐
                    │         KeyDB               │
                    │  - Очереди: pending/processing│
                    │  - Hash: job:{id}, worker:{id}│
                    │  - Pub/Sub: events, commands │
                    └───────────────┬─────────────┘
                                    │
            ┌───────────────────────┼───────────────────────┐
            ▼                       ▼                       ▼
    ┌───────────────┐       ┌───────────────┐       ┌───────────────┐
    │  Worker 1     │       │  Worker 2     │       │  Worker N     │
    │  (Go)         │       │  (Go)         │       │  (Go)         │
    │  - Симуляция  │       │  - Симуляция  │       │  - Симуляция  │
    │  - GDB порт   │       │  - GDB порт   │       │  - GDB порт   │
    └───────────────┘       └───────────────┘       └───────────────┘
            │                        │                        │
            │                        │                        │
    ┌───────▼───────┐        ┌───────▼───────┐        ┌───────▼───────┐
    │  GDB клиент   │        │  GDB клиент   │        │  GDB клиент   │
    │  (разработчик)│        │  (разработчик)│        │  (разработчик)│
    └───────────────┘        └───────────────┘        └───────────────┘
```

### 2.2 Поток данных с отладкой

1. **Клиент** → `POST /v1/jobs` (base64 бинарник + флаг `debug: true`)
2. **Gateway**:
   - Декодирует, считает SHA-256
   - Сохраняет в Postgres
   - **KeyDB**: `HSET job:{id} debug=true ...` + `LPUSH jobs:pending {id}`
3. **Worker** (циклически):
   - `BRPOPLPUSH jobs:pending jobs:processing` → получает job_id
   - Если `debug=true`, запускает симулятор с GDB-сервером на случайном порту
   - **KeyDB**: `HSET job:{id} state=running gdb_port=XXXXX worker_ip=YYY`
   - Публикует события: `PUBLISH events:job:{id} {...}`
4. **Gateway** (для SSE клиента):
   - Подписка: `SUBSCRIBE events:job:{id}`
   - Трансляция событий клиенту
   - В событии `status:running` передается `gdb_host` и `gdb_port`
5. **Разработчик** (из GDB):

   ```
   (gdb) target remote <worker_ip>:<gdb_port>
   (gdb) continue
   ```

6. **Воркер** пробрасывает GDB-протокол напрямую между симулятором и клиентом

---

## 3) Компоненты

### 3.1 API Gateway (Go)

| Аспект | Детали |
|--------|--------|
| **Язык** | Go 1.21+ |
| **Фреймворк** | Chi/Router (или стандартный net/http) |
| **Порт** | 8080 (REST), 8081 (метрики) |
| **Регистрация** | Stateless, горизонтальное масштабирование |

**Обязанности:**

- Аутентификация клиентов (API Key → user_id)
- Валидация входных данных
- Поддержка флага `debug` в запросе
- Проброс информации о GDB-порте клиенту через SSE
- **НЕ участвует в GDB-трафике** (прямое соединение клиент-воркер)

### 3.2 Simulator Worker с GDB-поддержкой

| Аспект | Детали |
|--------|--------|
| **Язык** | Go 1.21+ (обертка) + C (симулятор) |
| **Порт** | динамический (GDB), по умолчанию 3333 |
| **Concurrency** | 1 задача на воркер (для отладки критично) |

**Обязанности:**

- Heartbeat регистрация в KeyDB
- Pull задач из очереди `jobs:pending`
- **Для debug-задач**: запуск C-симулятора с встроенным GDB-сервером (gdb_stub)
- **Для обычных задач**: запуск без отладки
- Публикация событий (логи, телеметрия, статусы)
- Обработка команд остановки
- **Проброс GDB-соединения** между симулятором и клиентом

### 3.3 GDB-сервер (встроенный в симулятор на C)

| Аспект | Детали |
|--------|--------|
| **Реализация** | Встроенный gdbstub на C (см. `src/gdb_stub/gdb_stub.c`) |
| **Протокол** | GDB Remote Serial Protocol (RSP) |
| **Порт** | TCP, по умолчанию 3333 (`GDB_STUB_DEFAULT_PORT`) |
| **Target** | ARM Cortex-M3 (регистры r0-r15, xpsr) |

### 3.4 KeyDB (Шина данных)

| Аспект | Детали |
|--------|--------|
| **Версия** | 6.2+ |
| **Порт** | 6379 |
| **Persistance** | AOF + RDB (опционально) |

**Новые структуры для отладки:**

- В Hash `job:{job_id}` добавляются поля: `debug`, `gdb_port`, `gdb_host`, `gdb_password` (если нужна аутентификация)

---

## 4) API Gateway (Внешнее API) - обновленная спецификация

### 4.1 Создание задания с поддержкой отладки

```yaml
/jobs:
  post:
    summary: Создать новое задание на симуляцию
    requestBody:
      required: true
      content:
        application/json:
          schema:
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
    responses:
      '202':
        description: Задание принято в обработку
        content:
          application/json:
            schema:
              type: object
              properties:
                job_id:
                  type: string
                sha256:
                  type: string
                debug:
                  type: boolean
                status_url:
                  type: string
                events_url:
                  type: string
```

### 4.2 События SSE для отладки

При запуске задания в debug-режиме, воркер публикует событие:

```json
{
  "type": "status",
  "state": "running",
  "worker_id": "worker-01",
  "debug": true,
  "gdb": {
    "host": "worker-01.lab.internal",
    "port": 3333,
    "protocol": "tcp"
  },
  "timestamp": "2024-01-01T12:00:05Z"
}
```

### 4.3 Информация о GDB для клиента

Gateway также предоставляет эндпоинт для получения информации о GDB-подключении:

```
GET /v1/jobs/{job_id}/gdb-info
```

Response:

```json
{
  "job_id": "01H2X5J4K6",
  "debug_enabled": true,
  "gdb_host": "worker-01.lab.internal",
  "gdb_port": 3333,
  "connection_string": "target remote worker-01.lab.internal:3333",
  "status": "listening",
  "connected": false
}
```

---

## 5) Протоколы KeyDB (обновленные)

### 5.1 Новые поля в метаданных задания

**Ключ**: `job:{job_id}` (Hash)

| Поле | Тип | Описание |
|------|-----|----------|
| `debug` | boolean | Флаг отладки |
| `gdb_port` | integer | Порт GDB-сервера на воркере |
| `gdb_host` | string | IP/хост воркера (для подключения) |
| `gdb_password` | string | (опционально) пароль для защищенного подключения |
| `gdb_connected` | boolean | Подключился ли отладчик |

### 5.2 Пример создания debug-задания

```redis
MULTI
HSET job:01H2X5J4K6 
    state=queued 
    user_id=user123 
    sha256=e3b0c44... 
    created_at=2024-01-01T12:00:00Z
    debug=true
LPUSH jobs:pending 01H2X5J4K6
EXEC
```

### 5.3 Воркер назначает порт

```redis
# Воркер забрал задание, запустил GDB-сервер на порту 3333
HSET job:01H2X5J4K6
    state=running
    worker_id=worker-01
    started_at=2024-01-01T12:00:05Z
    gdb_port=3333
    gdb_host=192.168.1.100
    gdb_connected=false

PUBLISH events:job:01H2X5J4K6 '{
    "type":"status",
    "state":"running",
    "debug":true,
    "gdb":{"host":"192.168.1.100","port":3333}
}'
```

---

## 6) Воркер с GDB-сервером

### 6.1 Два режима работы воркера

```
┌─────────────────────────────────────────────────┐
│                    Worker                        │
├─────────────────────────────────────────────────┤
│  ┌─────────────────────┐  ┌─────────────────────┐│
│  │   Normal Mode       │  │   Debug Mode        ││
│  │   - Запуск симуляции│  │   - Запуск симуляции││
│  │   - Публикация логов│  │   - Публикация логов││
│  │   - Обычный таймаут │  │   - GDB сервер на   ││
│  │                     │  │     порту X         ││
│  │                     │  │   - Ожидание GDB    ││
│  │                     │  │     подключения     ││
│  │                     │  │   - Бесконечный/    ││
│  │                     │  │     увеличенный     ││
│  │                     │  │     таймаут         ││
│  └─────────────────────┘  └─────────────────────┘│
└─────────────────────────────────────────────────┘
```

### 6.2 Архитектура GDB-сервера (C)

GDB-сервер реализован на C в `src/gdb_stub/gdb_stub.c`. Ключевые особенности:

- **Протокол:** GDB Remote Serial Protocol (RSP)
- **Порт по умолчанию:** 3333 (`GDB_STUB_DEFAULT_PORT`)
- **Target:** ARM Cortex-M3 (17 регистров: r0-r15, xpsr)
- **Breakpoints:** до 64 точек останова

```c
// gdb_stub.h - API GDB stub
typedef struct {
    Simulator* sim;
    int        server_fd;
    int        client_fd;
    int        port;
} GdbStub;

void gdb_stub_init(GdbStub* stub, Simulator* sim, int port);
void gdb_stub_run(GdbStub* stub);   // blocks, accepts connections
void gdb_stub_close(GdbStub* stub);
```

### 6.3 Интеграция Go-воркера с C-симулятором

Go-воркер запускает C-симулятор как subprocess:

```go
func (w *Worker) executeDebugJob(ctx context.Context, jobID string, firmwarePath string, clientIP string) {
    // Выбираем свободный порт из диапазона
    port := w.getFreePort(3333, 3343)
    
    // Настраиваем firewall для защиты GDB-порта
    setupGDBFirewall(port, []string{clientIP})
    defer cleanupGDBFirewall(port, clientIP)
    
    // Обновляем статус с информацией о GDB
    w.redis.HSet(ctx, "job:"+jobID,
        "state", "running",
        "gdb_port", port,
        "gdb_host", w.getExternalIP())
    
    // Публикуем событие для клиента
    w.publishEvent(jobID, map[string]interface{}{
        "type": "status",
        "state": "running",
        "debug": true,
        "gdb": map[string]interface{}{
            "host": w.getExternalIP(),
            "port": port,
        },
    })
    
    // Запускаем C-симулятор с GDB stub
    cmd := exec.CommandContext(ctx, w.config.SimulatorBinary,
        "-firmware", firmwarePath,
        "-gdb-port", strconv.Itoa(port))
    
    // Перенаправляем stdout симулятора в события
    stdout, _ := cmd.StdoutPipe()
    go w.streamOutputToEvents(jobID, stdout)
    
    // Запускаем процесс
    if err := cmd.Start(); err != nil {
        w.finishJob(ctx, jobID, err)
        return
    }
    
    // Ждем завершения процесса
    err := cmd.Wait()
    w.finishJob(ctx, jobID, err)
}

func (w *Worker) streamOutputToEvents(jobID string, reader io.Reader) {
    scanner := bufio.NewScanner(reader)
    for scanner.Scan() {
        line := scanner.Text()
        w.publishEvent(jobID, map[string]interface{}{
            "type": "log",
            "message": line,
        })
    }
}
```

### 6.4 Командная строка симулятора

C-симулятор принимает следующие аргументы:

```bash
./stm32sim -firmware ./firmware.bin -gdb-port 3333
```

| Флаг | Описание |
|------|----------|
| `-firmware` | Путь к бинарному файлу прошивки |
| `-gdb-port` | TCP порт для GDB RSP (по умолчанию 3333) |

---

## 7) Клиентская часть (GDB)

### 7.1 Процесс отладки для разработчика

1. **Разработчик отправляет задание** с флагом `debug: true` через REST API

2. **Получает информацию о GDB** через SSE или эндпоинт `/gdb-info`

3. **Запускает GDB локально** с тем же бинарником (должен быть собран с отладочными символами):

   ```bash
   $ arm-none-eabi-gdb ./firmware.elf
   (gdb) target remote worker-01.lab.internal:3333
   Remote debugging using worker-01.lab.internal:3333
   0x08000134 in main ()
   (gdb) break main.c:42
   (gdb) continue
   ```

4. **Отлаживает удаленно**:
   - Ставит брейкпоинты
   - Смотрит переменные (`print`, `info locals`)
   - Исследует память (`x/10x 0x20000000`)
   - Читает регистры (`info registers`)
   - Шагает по коду (`step`, `next`)

5. **Завершает отладку**:

   ```bash
   (gdb) detach
   (gdb) quit
   ```

### 7.2 Пример сессии GDB

```bash
$ arm-none-eabi-gdb build/firmware.elf
GNU gdb (GNU Arm Embedded Toolchain) 12.1
...
Reading symbols from build/firmware.elf...

(gdb) target remote 192.168.1.100:3333
Remote debugging using 192.168.1.100:3333
0x08000134 in Reset_Handler ()

(gdb) break main
Breakpoint 1 at 0x80001d0: file src/main.c, line 25.

(gdb) info breakpoints
Num     Type           Disp Enb Address    What
1       breakpoint     keep y   0x080001d0 in main at src/main.c:25

(gdb) continue
Continuing.

Breakpoint 1, main () at src/main.c:25
25     HAL_Init();

(gdb) next
26     SystemClock_Config();

(gdb) print SystemCoreClock
$1 = 16000000

(gdb) info registers r0
r0            0x20000100      536871168

(gdb) x/4x 0x20000100
0x20000100: 0x00000000 0x00000000 0x00000000 0x00000000

(gdb) continue
```

### 7.3 Скрипт-обертка для удобства

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
PORT=$(echo $GDB_INFO | jq -r .gdb_port)
ELF_FILE=${3:-./firmware.elf}

# Подключаемся
gdb $ELF_FILE -ex "target remote $HOST:$PORT"
```

---

## 8) Хранилище данных (обновленное)

### 8.1 PostgreSQL - новые поля

```sql
-- Таблица заданий (долгосрочное хранение)
CREATE TABLE jobs (
    job_id VARCHAR(26) PRIMARY KEY,
    user_id VARCHAR(255) NOT NULL,
    sha256 CHAR(64) NOT NULL,
    state VARCHAR(20) NOT NULL,
    worker_id VARCHAR(255),
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    started_at TIMESTAMPTZ,
    finished_at TIMESTAMPTZ,
    timeout_seconds INT NOT NULL DEFAULT 30,
    error_text TEXT,
    -- Новые поля для отладки
    debug_mode BOOLEAN DEFAULT false,
    gdb_port INT,
    gdb_host VARCHAR(255),
    gdb_connected BOOLEAN DEFAULT false,
    gdb_connected_at TIMESTAMPTZ,
    metadata JSONB,
    
    INDEX idx_user_id (user_id),
    INDEX idx_state (state),
    INDEX idx_debug (debug_mode)
);

-- Таблица для логирования GDB-сессий
CREATE TABLE debug_sessions (
    id BIGSERIAL PRIMARY KEY,
    job_id VARCHAR(26) NOT NULL,
    user_id VARCHAR(255) NOT NULL,
    gdb_port INT,
    client_ip INET,
    connected_at TIMESTAMPTZ,
    disconnected_at TIMESTAMPTZ,
    commands_executed INT DEFAULT 0,
    
    FOREIGN KEY (job_id) REFERENCES jobs(job_id),
    INDEX idx_job_id (job_id)
);
```

---

## 9) Безопасность и аутентификация (обновленная)

### 9.1 Защита GDB-портов

```go
// Firewall rules (пример для iptables)
func setupGDBFirewall(workerIP string, port int, allowedIPs []string) {
    for _, ip := range allowedIPs {
        exec.Command("iptables", "-A", "INPUT", 
            "-p", "tcp", "--dport", strconv.Itoa(port),
            "-s", ip, "-j", "ACCEPT").Run()
    }
    // Закрываем доступ для всех остальных
    exec.Command("iptables", "-A", "INPUT",
        "-p", "tcp", "--dport", strconv.Itoa(port),
        "-j", "DROP").Run()
}
```

### 9.2 Аутентификация GDB-клиентов

**Важно:** C-симулятор (`gdb_stub.c`) не поддерживает встроенную аутентификацию.
Безопасность обеспечивается на уровне сети:

1. **IP whitelist** (только IP клиента, который создал задание) - реализуется через iptables
2. **SSH-туннель** (клиент подключается через SSH-прокси)
3. **VPN** (доступ только из корпоративной сети)

Пример настройки iptables в Go-воркере:

```go
// Firewall rules (пример для iptables)
func setupGDBFirewall(port int, allowedIPs []string) {
    // Разрешаем доступ только для IP клиента, создавшего задание
    for _, ip := range allowedIPs {
        exec.Command("iptables", "-A", "INPUT",
            "-p", "tcp", "--dport", strconv.Itoa(port),
            "-s", ip, "-j", "ACCEPT").Run()
    }
    // Закрываем доступ для всех остальных
    exec.Command("iptables", "-A", "INPUT",
        "-p", "tcp", "--dport", strconv.Itoa(port),
        "-j", "DROP").Run()
}

func (w *Worker) executeDebugJob(ctx context.Context, jobID string, clientIP string) {
    port := w.getFreePort(3333, 3343)
    
    // Настраиваем firewall перед запуском симулятора
    setupGDBFirewall(port, []string{clientIP})
    defer cleanupGDBFirewall(port, clientIP)
    
    // Запускаем C-симулятор
    // ...
}
```

### 9.3 Ограничения по времени

```go
// Debug-задания имеют увеличенный, но не бесконечный таймаут
const (
    NormalJobTimeout = 30 * time.Second
    DebugJobTimeout  = 1 * time.Hour
)

// Автоматическое завершение при простое GDB
if time.Since(lastGDBActivity) > 5*time.Minute {
    log.Info("No GDB activity for 5 minutes, stopping")
    simulator.Stop()
}
```

---

## 10) Observability (OpenTelemetry) - обновленная

### 10.1 Новые метрики для отладки

```go
var (
    debugJobsTotal = prometheus.NewCounter(
        prometheus.CounterOpts{
            Name: "debug_jobs_total",
            Help: "Total number of debug jobs created",
        },
    )
    
    gdbConnectionsActive = prometheus.NewGauge(
        prometheus.GaugeOpts{
            Name: "gdb_connections_active",
            Help: "Number of active GDB connections",
        },
    )
    
    gdbSessionDuration = prometheus.NewHistogram(
        prometheus.HistogramOpts{
            Name:    "gdb_session_duration_seconds",
            Help:    "Duration of GDB debugging sessions",
            Buckets: []float64{60, 300, 600, 1800, 3600},
        },
    )
)

func init() {
    prometheus.MustRegister(debugJobsTotal, gdbConnectionsActive, gdbSessionDuration)
}
```

### 10.2 Трейсинг GDB-операций

```go
func (g *GDBServer) traceGDBCommand(cmd string) {
    span := trace.SpanFromContext(context.Background())
    span.AddEvent("gdb_command", trace.WithAttributes(
        attribute.String("command", cmd),
        attribute.String("job.id", g.simulator.jobID),
    ))
}
```

### 10.3 Логирование GDB-сессий

```json
{
  "ts": "2024-01-01T12:00:10Z",
  "level": "info",
  "service": "worker",
  "job_id": "01H2X5J4K6",
  "event": "gdb_connected",
  "client_ip": "203.0.113.42",
  "gdb_port": 3333
}

{
  "ts": "2024-01-01T12:05:23Z",
  "level": "debug",
  "service": "worker",
  "job_id": "01H2X5J4K6",
  "event": "gdb_command",
  "command": "$g",
  "response_size": 128
}
```

---

## 11) Деплой и масштабирование

### 11.1 Сетевые требования для GDB

```
┌─────────────────┐      ┌──────────────────┐
│   Разработчик   │      │     Воркер       │
│   (GDB клиент)  │─────▶│  (GDB сервер)    │
│   IP: 203.0.113.42    │  Port: 3333+      │
└─────────────────┘      └──────────────────┘
        │                         │
        │                         │
        ▼                         ▼
┌─────────────────────────────────────────┐
│           Требования к сети             │
│  - Прямая маршрутизация до воркера      │
│  - Открытые порты 3333-3343 (TCP)       │
│  - Firewall rules для конкретных IP     │
│  - Возможно, VPN/SSH туннель            │
└─────────────────────────────────────────┘
```

### 11.2 Конфигурация воркера для отладки (YAML)

```yaml
worker:
  id: "worker-01"
  concurrency: 1
  heartbeat_interval: 5s
  
debug:
  enabled: true
  gdb_port_range:
    start: 3333      # GDB_STUB_DEFAULT_PORT
    end: 3343
  max_debug_time: 1h
  idle_timeout: 5m
  # Примечание: аутентификация на уровне firewall/VPN,
  # C-симулятор не поддерживает password auth
    
  network:
    public_ip: "auto"  # или конкретный IP для внешнего доступа
    bind_interface: "eth0"

simulator:
  binary: "./stm32sim"  # Путь к C-симулятору
  max_memory_mb: 256
  debug_timeout_seconds: 3600  # 1 час для отладки
```

### 11.3 Docker Compose с поддержкой GDB

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
      - "3333-3343:3333-3343"
    # Важно: network_mode host для реального IP
    network_mode: host
    environment:
      KEYDB_ADDR: keydb:6379
      WORKER_ID: ${HOSTNAME}
      DEBUG_ENABLED: "true"
      PUBLIC_IP: ${PUBLIC_IP}  # внешний IP для GDB
      SIMULATOR_BINARY: "/app/stm32sim"
    depends_on:
      - keydb
    volumes:
      - ./stm32sim:/app/stm32sim:ro  # C-симулятор
    # Запускаем несколько экземпляров
    deploy:
      replicas: 3
```

---

## 12) Спецификация реализации

### 12.1 Протокол GDB Remote Serial Protocol

Команды, поддерживаемые симулятором (реализация в `src/gdb_stub/gdb_stub.c`):

| Команда | Описание | Ответ |
|---------|----------|-------|
| `?` | Last signal | `S05` |
| `g` | Read all registers (r0-r15, xpsr) | 136 hex chars |
| `G data` | Write all registers | `OK` |
| `p n` | Read single register n | 8 hex chars |
| `P n=vvvv` | Write single register | `OK` |
| `m addr,len` | Read memory (max 1024 bytes) | hex bytes |
| `M addr,len:XX...` | Write memory | `OK` |
| `c [addr]` | Continue (until breakpoint/interrupt) | `S05` |
| `s [addr]` | Single step | `S05` |
| `Z0,addr,kind` | Insert software breakpoint | `OK` / `E01` |
| `z0,addr,kind` | Remove software breakpoint | `OK` / `E01` |
| `H op` | Thread select (stubbed) | `OK` |
| `T id` | Is thread alive? | `OK` |
| `D` | Detach | `OK` |
| `k` | Kill | (connection closed) |
| `qSupported` | Query features | `PacketSize=1000;qXfer:features:read+` |
| `qXfer:features:read:target.xml:` | Read target XML | Cortex-M3 description |
| `qRcmd,hex` | Monitor command (hex-encoded) | `OK` |

### 12.2 Monitor Commands (qRcmd)

Симулятор поддерживает следующие monitor-команды через GDB:

```
(gdb) monitor halt        # Остановить симуляцию
(gdb) monitor reset       # Сброс симулятора
(gdb) monitor reset halt  # Сброс и остановка
```

### 12.3 Target XML (Cortex-M3)

GDB stub возвращает XML-описание целевой архитектуры:

```xml
<?xml version="1.0"?>
<target version="1.0">
  <architecture>arm</architecture>
  <feature name="org.gnu.gdb.arm.m-profile">
    <reg name="r0"  bitsize="32" regnum="0"/>
    <reg name="r1"  bitsize="32" regnum="1"/>
    ...
    <reg name="pc"  bitsize="32" regnum="15" type="code_ptr"/>
    <reg name="xpsr" bitsize="32" regnum="16"/>
  </feature>
</target>
```

### 12.4 Архитектура симулятора (C)

Симулятор реализован на C и состоит из следующих компонентов:

```
src/
├── core/          # ARM Cortex-M3 ядро (Thumb инструкции)
├── memory/        # Flash, SRAM
├── bus/           # Шина для memory-mapped I/O
├── nvic/          # Nested Vectored Interrupt Controller
├── debugger/      # Breakpoint manager (до 64 точек)
├── gdb_stub/      # GDB RSP сервер
├── peripherals/   # UART, Timer
└── simulator/     # Главный оркестратор
```

**Интеграция Go-воркера с C-симулятором:**

```go
// Воркер запускает C-симулятор как subprocess
func (w *Worker) executeDebugJob(ctx context.Context, jobID string, firmwarePath string) error {
    // Выбираем свободный порт для GDB
    port := w.getFreePort(3333, 3343)
    
    // Обновляем статус с информацией о GDB
    w.redis.HSet(ctx, "job:"+jobID,
        "state", "running",
        "gdb_port", port,
        "gdb_host", w.getExternalIP())
    
    // Публикуем событие для клиента
    w.publishEvent(jobID, map[string]interface{}{
        "type": "status",
        "state": "running",
        "debug": true,
        "gdb": map[string]interface{}{
            "host": w.getExternalIP(),
            "port": port,
        },
    })
    
    // Запускаем C-симулятор с GDB stub
    cmd := exec.CommandContext(ctx, "./stm32sim",
        "-firmware", firmwarePath,
        "-gdb-port", strconv.Itoa(port))
    
    // Перенаправляем логи симулятора в события
    stdout, _ := cmd.StdoutPipe()
    go w.streamOutput(jobID, stdout)
    
    return cmd.Run()
}
```

### 12.5 Сборка C-симулятора

```makefile
# Makefile для симулятора
CC = gcc
CFLAGS = -Wall -Wextra -O2 -g

SRCS = src/main.c src/core/core.c src/memory/memory.c src/bus/bus.c \
       src/nvic/nvic.c src/debugger/debugger.c src/gdb_stub/gdb_stub.c \
       src/simulator/simulator.c src/peripherals/timer/timer.c \
       src/peripherals/uart/uart.c src/ui/ui.c

OBJS = $(SRCS:.c=.o)

stm32sim: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) stm32sim
```

---

## Приложение A: Примеры использования

### A.1 Обычное задание (без отладки)

```bash
curl -X POST https://api.lab.example.com/v1/jobs \
  -H "X-API-Key: lab_apikey_123" \
  -H "Content-Type: application/json" \
  -d '{
    "binary_b64": "AAAAAGV4YW1wbGUgYmluYXJ5IGNvbnRlbnQ="
  }'
```

### A.2 Задание с отладкой

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

### A.3 Получение GDB-информации

```bash
# После того как задание перешло в running
curl -H "X-API-Key: lab_apikey_123" \
  https://api.lab.example.com/v1/jobs/01H2X5J4K6/gdb-info
```

### A.4 Подключение GDB

```bash
# Локально, с тем же бинарником (должен быть с символами)
arm-none-eabi-gdb ./firmware.elf
(gdb) target remote 203.0.113.100:3333
(gdb) break main
(gdb) continue
```

---

## Приложение B: Диаграмма последовательности с отладкой

```
Клиент          Gateway         KeyDB          Worker          GDB
  |                |              |              |              |
  | POST /jobs     |              |              |              |
  | (debug=true)   |              |              |              |
  |--------------->|              |              |              |
  |                | HSET job:... |              |              |
  |                |------------->|              |              |
  |                | LPUSH pending|              |              |
  |                |------------->|              |              |
  | 202 Accepted   |              |              |              |
  |<---------------|              |              |              |
  |                |              | BRPOPLPUSH   |              |
  |                |              |<-------------|              |
  |                |              |--------------|              |
  |                |              | job:...      |              |
  |                |              |              |              |
  |                |              |              | Start GDB    |
  |                |              |              | stub on port |
  |                |              |              |=============>|
  |                |              |              |              |
  |                |              | HSET gdb_port|              |
  |                |              |<-------------|              |
  |                |              |--------------|              |
  |                | PUBLISH event|              |              |
  |                |<-------------|              |              |
  | SSE: gdb_info  |              |              |              |
  |<---------------|              |              |              |
  |                |              |              |              |
  |                |              |              |              | target remote
  |                |              |              |<-------------|
  |                |              |              |--------------|
  |                |              |              | GDB protocol |
  |                |              |              |<------------>|
  |                |              |              |              |
  |                |              |              |              |
  |                |              |              | Simulation   |
  |                |              |              | controlled   |
  |                |              |              | by GDB       |
  |                |              |              |              |
```
