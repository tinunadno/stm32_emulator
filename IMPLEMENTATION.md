# STM32F103C8T6 Simulator — Implementation

## Overview

Детерминированный однопоточный симулятор микроконтроллера STM32F103C8T6 на чистом C (C11). Реализован в соответствии со спецификацией `development_plan/description.md`.

Ключевые возможности:
- Ядро ARM Cortex-M3 (Thumb / Thumb-2 инструкции)
- NVIC с поддержкой 43 линий прерываний и приоритетами
- Flash (64 KB) и SRAM (20 KB) с little-endian доступом
- Таймер TIM2 с прескалером и генерацией прерываний
- UART (USART1) с передачей, приемом и прерываниями
- Шина памяти с адресным пространством и маршрутизацией
- Отладчик с breakpoints
- CLI-интерфейс с расширяемой таблицей команд

---

## Сборка

```bash
cd src
make
```

Зависимости: только стандартный C компилятор (gcc/clang), стандарт C11.

```bash
# Запуск без бинарника (ручная загрузка через CLI)
./stm32sim

# Запуск с бинарником
./stm32sim firmware.bin
```

---

## Структура проекта

```
src/
├── main.c                          # Точка входа
├── Makefile                        # Система сборки
├── common/
│   └── status.h                    # Коды ошибок (Status enum)
├── core/
│   ├── core.h                      # ARM Cortex-M3 ядро (API)
│   └── core.c                      # Декодер и исполнение инструкций
├── nvic/
│   ├── nvic.h                      # NVIC контроллер прерываний (API)
│   └── nvic.c                      # Приоритеты, pending, acknowledge
├── memory/
│   ├── memory.h                    # Flash + SRAM (API)
│   └── memory.c                    # Little-endian чтение/запись, загрузка бинарника
├── bus/
│   ├── bus.h                       # Системная шина (API)
│   └── bus.c                       # Маршрутизация по регионам
├── peripherals/
│   ├── peripheral.h                # Интерфейс периферии (vtable)
│   ├── timer/
│   │   ├── timer.h                 # TIM2 (API)
│   │   └── timer.c                 # Прескалер, auto-reload, IRQ
│   └── uart/
│       ├── uart.h                  # USART1 (API)
│       └── uart.c                  # TX/RX, прерывания, callback
├── debugger/
│   ├── debugger.h                  # Breakpoints (API)
│   └── debugger.c                  # Добавление/удаление/проверка
├── simulator/
│   ├── simulator.h                 # Главный оркестратор (API)
│   └── simulator.c                 # Цикл tick-step-check
└── ui/
    ├── ui.h                        # CLI интерфейс (API)
    └── ui.c                        # Таблица команд, парсинг, ввод/вывод
```

---

## Архитектура расширяемости

Система спроектирована так, что добавление нового функционала **не требует изменения существующего кода** (Open/Closed principle в C).

### 1. Добавление новой периферии

Используется **vtable-паттерн** через структуру `Peripheral` (в `peripherals/peripheral.h`):

```c
typedef struct {
    void* ctx;                                                    // Состояние
    uint32_t (*read)(void* ctx, uint32_t offset, uint8_t size);   // Чтение регистра
    Status (*write)(void* ctx, uint32_t offset, uint32_t val, uint8_t size); // Запись
    void (*tick)(void* ctx);                                      // Вызов каждый такт
    void (*reset)(void* ctx);                                     // Сброс
} Peripheral;
```

**Шаги для добавления новой периферии (например, SPI):**

1. Создать `src/peripherals/spi/spi.h` и `spi.c`
2. Определить struct `SpiState` с регистрами
3. Реализовать функции `spi_read()`, `spi_write()`, `spi_tick()`, `spi_reset()`
4. Реализовать `spi_as_peripheral()` возвращающую заполненный `Peripheral`
5. В `simulator.c` вызвать `simulator_add_peripheral()` — существующий код не меняется
6. Добавить `.c` файл в `SRCS` в Makefile

### 2. Добавление новой инструкции

Декодер использует **таблицу dispatch** в `core/core.c`:

```c
typedef struct {
    uint16_t     mask;      // Маска для сопоставления
    uint16_t     pattern;   // Паттерн после маскирования
    InstrHandler handler;   // Функция-обработчик
    const char*  name;      // Мнемоника (для отладки)
} InstrEntry;

static const InstrEntry instr_table[] = {
    {0xFFC0, 0x4000, exec_and, "AND"},
    // ... добавить новую запись сюда ...
    {0, 0, NULL, NULL}  // sentinel
};
```

**Шаги:** написать функцию `static Status exec_xxx(Core* core, uint16_t instr)` и добавить запись в таблицу.

### 3. Добавление новой CLI-команды

Таблица команд в `ui/ui.c`:

```c
static const Command commands[] = {
    {"help", "Show help", cmd_help},
    // ... добавить новую команду сюда ...
    {NULL, NULL, NULL}
};
```

**Шаги:** написать обработчик `static void cmd_xxx(Simulator* sim, const char* args)` и добавить запись в таблицу.

### 4. Добавление нового региона памяти

Шина (`Bus`) использует **регистрацию регионов**:

```c
bus_register_region(&bus, base_addr, size, ctx, read_fn, write_fn);
```

Любой модуль с совместимыми function pointers может быть зарегистрирован.

---

## Реализованные инструкции Thumb

| Формат | Инструкции |
|--------|-----------|
| 1 — Shift by immediate | LSL, LSR, ASR |
| 2 — Add/Subtract | ADD reg, SUB reg, ADD imm3, SUB imm3 |
| 3 — Immediate 8-bit | MOV, CMP, ADD, SUB |
| 4 — ALU register | AND, EOR, LSL, LSR, ASR, ADC, SBC, ROR, TST, NEG, CMP, CMN, ORR, MUL, BIC, MVN |
| 5 — Hi register / BX | ADD hi, CMP hi, MOV hi, BX |
| 6 — PC-relative LDR | LDR Rd, [PC, #imm] |
| 7/8 — Reg offset load/store | STR, STRH, STRB, LDR, LDRH, LDRB, LDRSB, LDRSH |
| 9 — Imm offset word/byte | STR, LDR, STRB, LDRB |
| 10 — Imm offset halfword | STRH, LDRH |
| 11 — SP-relative | STR, LDR |
| 12 — Load address | ADR (PC), ADD (SP) |
| 13 — Adjust SP | ADD SP, SUB SP |
| 14 — PUSH / POP | PUSH {regs, LR}, POP {regs, PC} |
| 16 — Conditional branch | BEQ, BNE, BCS, BCC, BMI, BPL, BVS, BVC, BHI, BLS, BGE, BLT, BGT, BLE |
| 17 — SVC | SVC (minimal) |
| 18 — Unconditional branch | B |
| 32-bit — BL | BL (branch with link) |

---

## Карта памяти

| Регион | Адрес начала | Размер | Описание |
|--------|-------------|--------|----------|
| Flash (alias) | 0x00000000 | 64 KB | Зеркало Flash для вектор-таблицы |
| Flash | 0x08000000 | 64 KB | Основная Flash-память |
| SRAM | 0x20000000 | 20 KB | Оперативная память |
| TIM2 | 0x40000000 | 1 KB | Регистры таймера |
| USART1 | 0x40013800 | 1 KB | Регистры UART |

---

## CLI-команды

| Команда | Описание |
|---------|----------|
| `help` | Список команд |
| `load <path>` | Загрузить бинарник в Flash |
| `run` | Запуск до breakpoint или ошибки |
| `stop` | Остановить выполнение |
| `step [N]` | Выполнить N инструкций (по умолчанию 1) |
| `reset` | Сброс симулятора |
| `reg` | Показать все регистры и флаги |
| `mem <addr> [count]` | Hex-дамп памяти |
| `break [addr]` | Установить breakpoint / список breakpoints |
| `delete <addr>` | Удалить breakpoint |
| `uart <char>` | Отправить символ в UART RX |
| `quit` | Выход |

---

## Цикл симуляции (порядок вызовов)

Строго фиксированный порядок, согласно спецификации:

```
1. tick_peripherals()   — тик всех зарегистрированных периферий
2. core_step()          — выборка, декодирование, исполнение инструкции
3. check_interrupts()   — проверка NVIC на pending IRQ
4. check_breakpoints()  — проверка debugger
```

---

## Прерывания

- Вход: сохранение R0-R3, R12, LR, PC, xPSR на стек; загрузка вектора из таблицы; LR = EXC_RETURN
- Выход: BX с EXC_RETURN или POP PC с EXC_RETURN — восстановление контекста
- Приоритет: меньшее значение = более высокий приоритет
- Вложенные прерывания поддержаны (preemption по приоритету)

---

## Ключевые проектные решения

1. **Чистый C (C11)** — никакого C++, минимум зависимостей
2. **Нет глобального состояния** — все состояние в структурах, передается через указатели
3. **Нет исключений** — ошибки через `Status` enum
4. **Расширяемость через function pointers** — vtable-паттерн для периферий, dispatch table для инструкций, command table для CLI
5. **Детерминизм** — нет потоков, нет random, нет реального времени
6. **Little-endian** — все обращения к памяти в little-endian
