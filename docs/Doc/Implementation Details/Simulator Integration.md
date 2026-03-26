# Интеграция симулятора с GDB

Симулятор может интегрироваться с GDB либо через встроенный gdbstub, либо через backend, например QEMU. 

## Встроенный GDB-сервер

Пример структуры:

```go
type GDBServer struct {
    port        int
    simulator   *Simulator
    connections chan net.Conn
    stopChan    chan bool
}
```

Запуск симулятора с GDB:

```go
func (s *Simulator) StartWithGDB(port int) error {
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
        log.Warn("No GDB client connected within timeout")
    }

    return nil
}
```

Обработка подключений:

```go
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
        go g.handleGDBConnection(conn)
    }
}
```

## Использование QEMU как backend

Если используется QEMU:

```go
func (s *Simulator) startQEMUWithGDB(port int) error {
    cmd := exec.Command("qemu-system-arm",
        "-kernel", s.binaryPath,
        "-machine", "stm32-p103",
        "-nographic",
        "-s", fmt.Sprintf("-tcp::%d", port),
        "-S",
    )

    err := cmd.Start()
    return err
}
```

В этом случае GDB подключается к GDB-stub QEMU, а не к кастомной реализации. 