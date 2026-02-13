Я отвечу как архитектор встроенных систем и эмуляторов ARM Cortex-M с опытом разработки симуляторов микроконтроллеров промышленного уровня.

Ниже — **готовый итоговый `SYSTEM_SPEC.md`**, переписанный как строгая, однозначная и машиночитаемая спецификация для реализации системы в Cursor.
Документ исключает двусмысленности, фиксирует API, ответственность модулей, порядок вызовов и ограничения.

---

# SYSTEM_SPEC.md

## STM32F103C8T6 Simulator — Полная формальная спецификация реализации

---

# 1. Цель системы

Реализовать детерминированный однопоточный симулятор микроконтроллера STM32F103C8T6 с:

* ядром ARM Cortex-M3 (Thumb)
* NVIC
* Flash и SRAM
* Таймером TIM2
* UART
* Шиной памяти
* CLI интерфейсом
* Отладчиком (breakpoints)

Симуляция выполняется строго в одном потоке.
Асинхронность запрещена.

---

# 2. Общие архитектурные правила

## 2.1. Язык и стандарты

* Язык: C++17
* Запрещено использование глобального состояния
* Все зависимости передаются через конструкторы
* Используется RAII
* Исключения запрещены
* Ошибки возвращаются через enum Status

---

## 2.2. Главный принцип выполнения

Система работает по модели:

```
tick_peripherals()
core_step()
check_interrupts()
check_breakpoints()
```

Порядок НЕ может быть изменен.

---

# 3. Архитектура проекта

```
/src
  core/
  nvic/
  memory/
  bus/
  peripherals/
    timer/
    uart/
  debugger/
  simulator/
  ui/
```

---

# 4. Интерфейсы модулей

Все модули обязаны реализовать строгие интерфейсы.

---

# 5. Core (ARM Cortex-M3)

## 5.1. Обязанности

* Выполнение инструкций Thumb
* Управление регистрами R0-R15
* Управление xPSR
* Вход/выход из прерываний
* Генерация обращений к шине

---

## 5.2. Структура состояния

```cpp
struct CoreState {
    uint32_t r[16];      // R0-R15
    uint32_t xpsr;
    bool thumb_mode;     // всегда true
    bool interruptible;
    uint32_t current_irq;
    uint64_t cycles;
};
```

---

## 5.3. Публичный API

```cpp
class IBus;
class INVIC;

class Core {
public:
    Core(IBus& bus, INVIC& nvic);

    void reset();
    Status step();
    const CoreState& state() const;

private:
    Status execute(uint16_t instruction);
    void enter_interrupt(uint32_t irq);
};
```

---

## 5.4. Поведение step()

Алгоритм:

1. PC должен находиться в Flash или SRAM
2. Считать 16-битную инструкцию
3. Декодировать
4. Исполнить
5. Обновить PC
6. Увеличить cycles
7. Проверить прерывания

---

# 6. NVIC

## 6.1. Ограничения

* Поддержка 43 IRQ
* Приоритет меньше — выше

---

## 6.2. Состояние

```cpp
struct NVICState {
    bool pending[43];
    bool active[43];
    bool enabled[43];
    uint8_t priority[43];
    uint8_t current_priority;
};
```

---

## 6.3. API

```cpp
class NVIC : public INVIC {
public:
    void set_pending(uint32_t irq);
    void clear_pending(uint32_t irq);
    bool get_pending_irq(uint32_t& irq_out);
    void acknowledge(uint32_t irq);
};
```

---

# 7. Memory

## 7.1. Карта памяти

| Регион | Адрес      | Размер    |
| ------ | ---------- | --------- |
| Flash  | 0x08000000 | 64 KB     |
| SRAM   | 0x20000000 | 20 KB     |
| Periph | 0x40000000 | через Bus |

---

## 7.2. API

```cpp
class Memory {
public:
    uint32_t read(uint32_t addr, uint8_t size);
    Status write(uint32_t addr, uint32_t value, uint8_t size);
    Status load_binary(const std::string& path);
};
```

Little-endian обязателен.

---

# 8. Bus Controller

## 8.1. Правило

Core НЕ обращается к Memory напрямую.
Только через Bus.

---

## 8.2. API

```cpp
class IPeripheral {
public:
    virtual uint32_t read(uint32_t offset, uint8_t size) = 0;
    virtual Status write(uint32_t offset, uint32_t value, uint8_t size) = 0;
};

class Bus : public IBus {
public:
    void register_region(uint32_t start, uint32_t size, IPeripheral* dev);

    uint32_t read(uint32_t addr, uint8_t size);
    Status write(uint32_t addr, uint32_t value, uint8_t size);
};
```

---

# 9. Timer (TIM2)

## 9.1. Реализовать регистры

* CR1
* SR
* CNT
* PSC
* ARR
* DIER

---

## 9.2. API

```cpp
class Timer : public IPeripheral {
public:
    Timer(INVIC& nvic, uint32_t irq);
    void tick();
};
```

tick() вызывается 1 раз на каждый шаг симулятора.

---

## 9.3. Логика

Если:

```
CNT >= ARR
```

Тогда:

```
CNT = 0
SR |= UIF
если DIER & UIE → nvic.set_pending()
```

---

# 10. UART

## 10.1. Регистры

* SR
* DR
* BRR
* CR1

---

## 10.2. API

```cpp
class UART : public IPeripheral {
public:
    UART(INVIC& nvic, uint32_t irq);
    void tick();
    void incoming_char(char c);
};
```

---

## 10.3. Передача

* Запись в DR запускает передачу
* По завершении:

  * SR |= TXE
  * SR |= TC
  * Если CR1 & TXEIE → set_pending

---

# 11. Debugger

## 11.1. Breakpoint

```cpp
class Debugger {
public:
    void add_breakpoint(uint32_t addr);
    bool check(uint32_t pc);
};
```

---

# 12. Simulator (главный orchestrator)

```cpp
class Simulator {
public:
    void step();
    void run();
    void reset();
};
```

---

## 12.1. Алгоритм step()

```
timer.tick()
uart.tick()
core.step()
если debugger.check(core.pc) → halt
```

---

# 13. UI

CLI обязателен.

Поддерживаемые команды:

```
run
stop
step [N]
reset
reg
mem
break
quit
```

Все команды синхронные.

---

# 14. Детерминизм

Симуляция должна быть полностью детерминированной:

* Нет потоков
* Нет случайных чисел
* Нет реального времени

---

# 15. Минимальный набор инструкций Cortex-M3

Обязательно реализовать:

* MOV
* LDR
* STR
* ADD
* SUB
* MUL
* AND
* ORR
* EOR
* B
* BEQ
* BNE

---

# 16. Критические запреты

* Нельзя вызывать периферию напрямую
* Нельзя модифицировать NVIC вне его API
* Нельзя менять порядок вызовов в step()
* Нельзя использовать исключения
* Нельзя использовать динамическую типизацию

---

# 17. Гарантии корректности

После reset():

```
SP = *(0x08000000)
PC = *(0x08000004)
```

---

# 18. Итоговая последовательность инициализации

```
Memory
NVIC
Bus
Timer
UART
Core
Debugger
Simulator
UI
```

---

# 19. Требования к тестированию

Обязательно:

* Unit-тест Timer overflow
* Unit-тест UART TX
* Unit-тест NVIC priority
* Интеграционный тест: прерывание от Timer вызывает обработчик

---

# 20. Завершенность реализации

Реализация считается завершенной, если:

* Загружается бинарник
* Таймер генерирует IRQ
* UART выводит символы
* Breakpoint останавливает выполнение
* run/step работают корректно

---

Если нужно — могу дополнительно:

* сформировать архитектурную диаграмму
* написать skeleton-код проекта
* подготовить CMakeLists.txt
* добавить UML
* подготовить реализацию минимального MVP

Скажи, что нужно дальше.
