# GDB Remote Serial Protocol (RSP)

Симулятор должен реализовывать подмножество протокола GDB RSP. 

## Основные команды

| Команда                | Описание              | Ответ                                   |
|------------------------|-----------------------|-----------------------------------------|
| `g`                    | Read registers        | hex-представление регистров             |
| `G`                    | Write registers       | `OK`                                    |
| `m addr,len`           | Read memory           | hex-байты                               |
| `M addr,len:XX...`     | Write memory          | `OK`                                    |
| `c`                    | Continue              | `S05` (при остановке сигналом)          |
| `s`                    | Step                  | `S05`                                   |
| `z type,addr,kind`     | Remove breakpoint     | `OK`                                    |
| `Z type,addr,kind`     | Insert breakpoint     | `OK`                                    |
| `qSupported`           | Supported features    | например `PacketSize=3fff;...`          |
| `?`                    | Last signal           | `S05`                                   |

## Минимальный gdbstub

Пример skeleton-реализации обработчика пакетов:

```go
type GDBStub struct {
    conn        net.Conn
    sim         *Simulator
    breakpoints map[uint32]bool
}

func (g *GDBStub) handlePacket(packet string) string {
    switch packet {
    case 'g':
        return g.readRegisters()
    case 'G':
        return g.writeRegisters(packet[1:])
    case 'm':
        // m addr,len
        parts := strings.Split(packet[1:], ",")
        addr, _ := strconv.ParseUint(parts, 16, 32)
        length, _ := strconv.ParseUint(parts, 16, 32)[1]
        return g.readMemory(uint32(addr), int(length))
    case 'c':
        g.sim.Continue()
        return ""
    case 's':
        g.sim.Step()
        return "S05"
    case 'Z':
        return "OK"
    case 'z':
        return "OK"
    default:
        return ""
    }
}
```

Обработка сигналов:

```go
var signalMap = map[int]string{
    5:  "SIGTRAP",
    11: "SIGSEGV",
    15: "SIGTERM",
}

func (g *GDBServer) handleSignal(sig int) string {
    return fmt.Sprintf("S%02x", sig)
}
```