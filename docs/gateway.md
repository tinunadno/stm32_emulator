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

### 3.2 Simulator Worker с GDB-поддержкой (Go)

| Аспект | Детали |
|--------|--------|
| **Язык** | Go 1.21+ |
| **Порт** | динамический (GDB), обычно 1234-1244 |
| **Concurrency** | 1 задача на воркер (для отладки критично) |

**Обязанности:**

- Heartbeat регистрация в KeyDB
- Pull задач из очереди `jobs:pending`
- **Для debug-задач**: запуск симулятора с встроенным GDB-сервером
- **Для обычных задач**: запуск без отладки
- Публикация событий (логи, телеметрия, статусы)
- Обработка команд остановки
- **Проброс GDB-соединения** между симулятором и клиентом

### 3.3 GDB-сервер (встроенный в симулятор)

| Аспект | Детали |
|--------|--------|
| **Реализация** | Встроенный gdbstub или внешний gdbserver |
| **Протокол** | GDB Remote Serial Protocol |
| **Порт** | TCP, динамический (рандомный из диапазона) |

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
    "port": 12345,
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
  "gdb_port": 12345,
  "connection_string": "target remote worker-01.lab.internal:12345",
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
# Воркер забрал задание, запустил GDB-сервер на порту 12345
HSET job:01H2X5J4K6 
    state=running 
    worker_id=worker-01 
    started_at=2024-01-01T12:00:05Z
    gdb_port=12345
    gdb_host=192.168.1.100
    gdb_connected=false

PUBLISH events:job:01H2X5J4K6 '{
    "type":"status",
    "state":"running",
    "debug":true,
    "gdb":{"host":"192.168.1.100","port":12345}
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

### 6.2 Реализация GDB-сервера в симуляторе

```go
type GDBServer struct {
    port        int
    simulator   *Simulator
    connections chan net.Conn
    stopChan    chan bool
}

func (s *Simulator) StartWithGDB(port int) error {
    // Запускаем GDB-сервер
    gdbServer := &GDBServer{
        port:      port,
        simulator: s,
    }
    
    go gdbServer.Listen()
    
    // Ожидаем подключения отладчика (опционально)
    select {
    case <-gdbServer.connections:
        log.Info("GDB client connected")
    case <-time.After(30 * time.Second):
        // Продолжаем даже без отладчика
        log.Warn("No GDB client connected within timeout")
    }
    
    return nil
}

func (g *GDBServer) Listen() {
    listener, err := net.Listen("tcp", fmt.Sprintf(":%d", g.port))
    if err != nil {
        log.Error(err)
        return
    }
    
    for {
        conn, err := listener.Accept()
        if err != nil {
            return
        }
        
        // Обрабатываем GDB протокол
        go g.handleGDBConnection(conn)
    }
}

func (g *GDBServer) handleGDBConnection(conn net.Conn) {
    defer conn.Close()
    
    // GDB Remote Serial Protocol implementation
    buffer := make([]byte, 4096)
    for {
        n, err := conn.Read(buffer)
        if err != nil {
            return
        }
        
        // Парсим команды GDB ($g, $m, $p, etc)
        cmd := string(buffer[:n])
        response := g.processGDBCommand(cmd)
        
        conn.Write([]byte(response))
    }
}
```

### 6.3 Интеграция с симулятором

```go
func (w *Worker) executeDebugJob(ctx context.Context, jobID string) {
    // Выбираем случайный порт из диапазона
    port := w.getFreePort(1234, 1244)
    
    // Обновляем статус с информацией о GDB
    w.redis.HSet(ctx, "job:"+jobID,
        "state", "running",
        "gdb_port", port,
        "gdb_host", w.getExternalIP(),
        "gdb_connected", false)
    
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
    
    // Запускаем симулятор с GDB
    sim := NewSimulator(jobID)
    
    // Канал для отслеживания GDB-подключения
    gdbConnected := make(chan bool)
    
    // Запускаем GDB-сервер в отдельной горутине
    go func() {
        err := sim.StartGDB(port)
        if err == nil {
            gdbConnected <- true
            w.redis.HSet(ctx, "job:"+jobID, "gdb_connected", true)
        }
    }()
    
    // Ждем подключения отладчика или таймаут
    select {
    case <-gdbConnected:
        log.Info("Debugger connected, starting simulation")
    case <-time.After(30 * time.Second):
        log.Warn("No debugger connected, starting anyway")
    }
    
    // Запускаем симуляцию (может быть заблокирована отладчиком)
    err := sim.Run()
    
    // Завершаем
    w.finishJob(ctx, jobID, err)
}
```

### 6.4 Безопасность GDB-подключений

```go
// Опционально: аутентификация GDB-клиента
func (g *GDBServer) authenticate(conn net.Conn) bool {
    // Отправляем запрос пароля
    conn.Write([]byte("$password#00"))
    
    buffer := make([]byte, 256)
    n, _ := conn.Read(buffer)
    
    // Проверяем пароль из метаданных задания
    expected := g.simulator.jobData["gdb_password"]
    return strings.TrimSpace(string(buffer[:n])) == expected
}
```

---

## 7) Клиентская часть (GDB)

### 7.1 Процесс отладки для разработчика

1. **Разработчик отправляет задание** с флагом `debug: true` через REST API

2. **Получает информацию о GDB** через SSE или эндпоинт `/gdb-info`

3. **Запускает GDB локально** с тем же бинарником (должен быть собран с отладочными символами):

   ```bash
   $ gdb ./firmware.elf
   (gdb) target remote worker-01.lab.internal:12345
   Remote debugging using worker-01.lab.internal:12345
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
$ gdb build/firmware.elf
GNU gdb (Ubuntu 12.1-0ubuntu1) 12.1
...
Reading symbols from build/firmware.elf...

(gdb) target remote 192.168.1.100:12345
Remote debugging using 192.168.1.100:12345
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

Варианты:

1. **IP whitelist** (только IP клиента, который создал задание)
2. **GDB-пароль** (генерируется и передается через SSE)
3. **SSH-туннель** (клиент подключается через SSH-прокси)

Пример с паролем:

```go
func (w *Worker) generateGDBPassword() string {
    bytes := make([]byte, 16)
    rand.Read(bytes)
    return hex.EncodeToString(bytes)
}

// При создании задания с debug
password := generateGDBPassword()
redis.HSet(ctx, "job:"+jobID, "gdb_password", password)

// Передаем клиенту через SSE
publishEvent(jobID, map[string]interface{}{
    "type": "gdb_auth",
    "password": password,
})
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
  "gdb_port": 12345
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
│   IP: 203.0.113.42    │  Port: 12345      │
└─────────────────┘      └──────────────────┘
        │                         │
        │                         │
        ▼                         ▼
┌─────────────────────────────────────────┐
│           Требования к сети             │
│  - Прямая маршрутизация до воркера      │
│  - Открытые порты 1234-1244 (TCP)       │
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
    start: 1234
    end: 1244
  max_debug_time: 1h
  idle_timeout: 5m
  authentication:
    enabled: true
    type: "password"  # или "ip_whitelist"
  
  # Если используем IP whitelist
  ip_whitelist:
    enabled: true
    source_header: "X-Forwarded-For"  # или "X-Real-IP"
    
  network:
    public_ip: "auto"  # или конкретный IP для внешнего доступа
    bind_interface: "eth0"

simulator:
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
      - "1234-1244:1234-1244"
    # Важно: network_mode host для реального IP
    network_mode: host
    environment:
      KEYDB_ADDR: keydb:6379
      WORKER_ID: ${HOSTNAME}
      DEBUG_ENABLED: "true"
      PUBLIC_IP: ${PUBLIC_IP}  # внешний IP для GDB
    depends_on:
      - keydb
    # Запускаем несколько экземпляров
    deploy:
      replicas: 3
```

---

## 12) Спецификация реализации

### 12.1 Протокол GDB Remote Serial Protocol

Ключевые команды, которые должен поддерживать симулятор:

| Команда | Описание | Ответ |
|---------|----------|-------|
| `g` | Read registers | `xxxxxxxx...` (hex) |
| `G` | Write registers | `OK` |
| `m addr,len` | Read memory | hex bytes |
| `M addr,len:XX...` | Write memory | `OK` |
| `c` | Continue | `S05` (signal) |
| `s` | Step | `S05` |
| `z type,addr,kind` | Remove breakpoint | `OK` |
| `Z type,addr,kind` | Insert breakpoint | `OK` |
| `qSupported` | Query supported features | `PacketSize=3fff;qXfer:features:read+` |
| `?` | Last signal | `S05` |

### 12.2 Интеграция с эмулятором (пример для QEMU)

```go
// Если используем QEMU как бэкенд
func (s *Simulator) startQEMUWithGDB(port int) error {
    cmd := exec.Command("qemu-system-arm",
        "-kernel", s.binaryPath,
        "-machine", "stm32-p103",
        "-nographic",
        "-s", fmt.Sprintf("-tcp::%d", port),  // QEMU GDB stub
        "-S",  // Остановиться до подключения GDB
    )
    
    // Запускаем QEMU
    err := cmd.Start()
    
    // QEMU сам реализует GDB stub на порту
    return err
}
```

### 12.3 Обработка сигналов

```go
// Маппинг сигналов для GDB
var signalMap = map[int]string{
    5:  "SIGTRAP",  // breakpoint
    11: "SIGSEGV",  // segfault
    15: "SIGTERM",  // terminated
}

func (g *GDBServer) handleSignal(sig int) string {
    return fmt.Sprintf("S%02x", sig)
}
```

### 12.4 Пример реализации gdbstub (минимальный)

```go
// Минимальный gdbstub для встраивания в симулятор
type GDBStub struct {
    conn     net.Conn
    sim      *Simulator
    breakpoints map[uint32]bool
}

func (g *GDBStub) handlePacket(packet string) string {
    switch packet[0] {
    case 'g': // Read registers
        return g.readRegisters()
    case 'G': // Write registers
        return g.writeRegisters(packet[1:])
    case 'm': // Read memory
        // m addr,len
        parts := strings.Split(packet[1:], ",")
        addr, _ := strconv.ParseUint(parts[0], 16, 32)
        length, _ := strconv.ParseUint(parts[1], 16, 32)
        return g.readMemory(uint32(addr), int(length))
    case 'c': // Continue
        g.sim.Continue()
        return "" // Wait for stop
    case 's': // Step
        g.sim.Step()
        return "S05" // SIGTRAP
    case 'Z': // Insert breakpoint
        // Z0,addr,kind
        return "OK"
    case 'z': // Remove breakpoint
        return "OK"
    default:
        return ""
    }
}
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
gdb ./firmware.elf
(gdb) target remote 203.0.113.100:12345
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
