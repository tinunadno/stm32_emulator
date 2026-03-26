# Конфигурация воркера

Пример конфигурации воркера в формате YAML. 

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

  ip_whitelist:
    enabled: true
    source_header: "X-Forwarded-For"

  network:
    public_ip: "auto"   # или конкретный внешний IP
    bind_interface: "eth0"

simulator:
  max_memory_mb: 256
  debug_timeout_seconds: 3600
```

Эта конфигурация задает диапазон GDB-портов, лимиты времени и методы аутентификации. 