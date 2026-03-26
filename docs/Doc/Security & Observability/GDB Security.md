# Безопасность GDB-подключений

GDB-порт — это прямой доступ к управлению симулятором, поэтому его нужно защищать. 

## Защита портов

Возможные меры:

1. Ограничение по IP (white-list клиента, создавшего задание). 
2. Пароль для GDB-сессии, генерируемый на воркере. 
3. Использование SSH-туннеля или VPN для доступа к воркерам. 

Пример настройки firewall (iptables):

```go
func setupGDBFirewall(workerIP string, port int, allowedIPs []string) {
    for _, ip := range allowedIPs {
        exec.Command("iptables", "-A", "INPUT", 
            "-p", "tcp", "--dport", strconv.Itoa(port),
            "-s", ip, "-j", "ACCEPT").Run()
    }
    exec.Command("iptables", "-A", "INPUT",
        "-p", "tcp", "--dport", strconv.Itoa(port),
        "-j", "DROP").Run()
}
```

## Аутентификация GDB-клиентов

Варианты:

- **IP whitelist** — подключаться может только IP клиента, создавшего задание. 
- **Пароль** — генерируется воркером, сохраняется в `gdb_password` и отправляется клиенту через SSE. 
- **SSH-туннель** — GDB подключается к локальному порту SSH-клиента. 

Пример генерации пароля:

```go
func (w *Worker) generateGDBPassword() string {
    bytes := make([]byte, 16)
    rand.Read(bytes)
    return hex.EncodeToString(bytes)
}
```

Пароль сохраняется в KeyDB и отправляется клиенту отдельным событием `gdb_auth`. 

## Ограничения по времени

- Debug-задания имеют увеличенный таймаут выполнения (например, до 1 часа). 
- При отсутствии активности GDB (нет команд) воркер может остановить симуляцию, например по таймауту 5 минут. 