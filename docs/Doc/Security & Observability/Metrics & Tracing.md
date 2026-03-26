# Observability: метрики и трейсы

Для контроля нагрузки и отладки самой платформы используются метрики и OpenTelemetry-трейсинг. 

## Метрики

Примеры метрик:

- `debug_jobs_total` — количество созданных debug-заданий. 
- `gdb_connections_active` — число активных GDB-подключений. 
- `gdb_session_duration_seconds` — длительность GDB-сессий (histogram). 

Регистрация метрик:

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
```

## Трейсинг GDB-операций

Для анализа поведения GDB-сессий можно добавлять события в трейсы:

```go
func (g *GDBServer) traceGDBCommand(cmd string) {
    span := trace.SpanFromContext(context.Background())
    span.AddEvent("gdb_command", trace.WithAttributes(
        attribute.String("command", cmd),
        attribute.String("job.id", g.simulator.jobID),
    ))
}
```

## Логирование

Логи могут содержать события вроде `gdb_connected`, `gdb_command`, с полями `job_id`, `client_ip`, `gdb_port` и т.п. 