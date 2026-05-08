#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "simulator/simulator.h"
#include "peripherals/uart/uart.h"
#include "ui/ui.h"
#include "gdb_stub/gdb_stub.h"

/* Silent UART output for --json batch mode (bytes are captured by uart_logger) */
static void null_uart_output(char c, void* user_data)
{
    (void)c;
    (void)user_data;
}

static void usage(const char* prog)
{
    fprintf(stderr,
            "Usage: %s [binary.bin] [--gdb [port]] [--max-cycles N] [--json]\n"
            "\n"
            "  binary.bin      STM32 binary to load into flash\n"
            "  --gdb           Start GDB RSP server (default port %d)\n"
            "  --gdb PORT      Start GDB RSP server on PORT\n"
            "  --max-cycles N  Stop after N instructions (batch mode)\n"
            "  --json          Batch mode: print JSON report to stdout on exit\n"
            "\n"
            "Examples:\n"
            "  %s firmware.bin                         # interactive CLI\n"
            "  %s firmware.bin --gdb                   # GDB server on port %d\n"
            "  %s firmware.bin --max-cycles 10000000 --json\n",
            prog,
            GDB_STUB_DEFAULT_PORT,
            prog, prog, GDB_STUB_DEFAULT_PORT, prog);
}

/* Write one JSON string value to stdout, with proper escaping. */
static void json_write_str(const char* s, size_t len)
{
    putchar('"');
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        switch (c) {
            case '"':  fputs("\\\"", stdout); break;
            case '\\': fputs("\\\\", stdout); break;
            case '\n': fputs("\\n",  stdout); break;
            case '\r': fputs("\\r",  stdout); break;
            case '\t': fputs("\\t",  stdout); break;
            default:
                if (c < 0x20)
                    printf("\\u%04x", c);
                else
                    putchar(c);
        }
    }
    putchar('"');
}

static const char* handler_name(uint32_t slot, char* buf, size_t buf_sz)
{
    switch (slot) {
        case 2:  return "NMI";
        case 3:  return "HardFault";
        case 15: return "SysTick";
        case 44: return "TIM2 (IRQ28)";
        case 53: return "USART1 (IRQ37)";
        default:
            if (slot >= 16)
                snprintf(buf, buf_sz, "IRQ%u", slot - 16);
            else
                snprintf(buf, buf_sz, "EXC%u", slot);
            return buf;
    }
}

static void print_json_report(const Simulator* sim, const char* halt_reason)
{
    const CoreState*  cs = core_get_state(&sim->core);
    const UartLogger* ul = &sim->uart_logger;
    const Profiler*   pr = &sim->profiler;

    printf("{\n");
    printf("  \"halt_reason\": \"%s\",\n", halt_reason);
    printf("  \"cycles\": %llu,\n", (unsigned long long)sim->cycle);

    /* --- registers --- */
    printf("  \"registers\": {\n");
    for (int i = 0; i <= 12; i++)
        printf("    \"r%d\": %lu,\n", i, (unsigned long)cs->r[i]);
    printf("    \"sp\": %lu,\n",  (unsigned long)cs->r[REG_SP]);
    printf("    \"lr\": %lu,\n",  (unsigned long)cs->r[REG_LR]);
    printf("    \"pc\": %lu,\n",  (unsigned long)cs->r[REG_PC]);
    printf("    \"xpsr\": %lu\n", (unsigned long)cs->xpsr);
    printf("  },\n");

    /* --- uart_tx: concatenated TX bytes as a JSON string --- */
    {
        int      total     = ul->count < UART_LOG_MAX_EVENTS ? ul->count : UART_LOG_MAX_EVENTS;
        int      start_idx = (ul->count >= UART_LOG_MAX_EVENTS) ? ul->head : 0;
        char     tx_buf[UART_LOG_MAX_EVENTS];
        int      tx_len = 0;
        for (int i = 0; i < total; i++) {
            int idx = (start_idx + i) % UART_LOG_MAX_EVENTS;
            if (ul->events[idx].dir == UART_LOG_TX)
                tx_buf[tx_len++] = (char)ul->events[idx].byte;
        }
        printf("  \"uart_tx\": ");
        json_write_str(tx_buf, (size_t)tx_len);
        printf(",\n");
    }

    /* --- uart_events --- */
    printf("  \"uart_events\": [");
    {
        int total     = ul->count < UART_LOG_MAX_EVENTS ? ul->count : UART_LOG_MAX_EVENTS;
        int start_idx = (ul->count >= UART_LOG_MAX_EVENTS) ? ul->head : 0;
        int first     = 1;
        for (int i = 0; i < total; i++) {
            int              idx = (start_idx + i) % UART_LOG_MAX_EVENTS;
            const UartLogEvent* e = &ul->events[idx];
            /* Printable char for display */
            char ch[2] = { (e->byte >= 0x20 && e->byte < 0x7f) ? (char)e->byte : '.', '\0' };
            if (!first) printf(",");
            printf("\n    {\"cycle\":%llu,\"dir\":\"%s\",\"byte\":%u,\"char\":",
                   (unsigned long long)e->tick,
                   e->dir == UART_LOG_TX ? "tx" : "rx",
                   (unsigned int)e->byte);
            json_write_str(ch, 1);
            printf("}");
            first = 0;
        }
    }
    printf("\n  ],\n");

    /* --- profiler_handlers --- */
    printf("  \"profiler_handlers\": [");
    {
        int  first = 1;
        char name_buf[32];
        for (uint32_t slot = 0; slot < PROF_NUM_SLOTS; slot++) {
            if (pr->slots[slot].call_count == 0) continue;
            uint64_t avg = pr->slots[slot].total_cycles / pr->slots[slot].call_count;
            const char* name = handler_name(slot, name_buf, sizeof(name_buf));
            if (!first) printf(",");
            printf("\n    {\"name\":\"%s\",\"calls\":%llu,"
                   "\"total_cycles\":%llu,\"avg_cycles\":%llu,\"max_cycles\":%llu}",
                   name,
                   (unsigned long long)pr->slots[slot].call_count,
                   (unsigned long long)pr->slots[slot].total_cycles,
                   (unsigned long long)avg,
                   (unsigned long long)pr->slots[slot].max_cycles);
            first = 0;
        }
    }
    printf("\n  ],\n");

    /* --- profiler_instrs --- */
    printf("  \"profiler_instrs\": [");
    {
        int first = 1;
        for (uint32_t i = 0; i < PROF_INSTR_SLOTS; i++) {
            if (pr->instrs[i].count == 0 || pr->instrs[i].name == NULL) continue;
            if (!first) printf(",");
            printf("\n    {\"name\":\"%s\",\"count\":%llu}",
                   pr->instrs[i].name,
                   (unsigned long long)pr->instrs[i].count);
            first = 0;
        }
    }
    printf("\n  ]\n");

    printf("}\n");
}

int main(int argc, char* argv[])
{
    Simulator sim;
    simulator_init(&sim);

    const char* binary     = NULL;
    int         gdb_port   = 0;
    uint64_t    max_cycles = 0;
    int         json_mode  = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--gdb") == 0) {
            gdb_port = GDB_STUB_DEFAULT_PORT;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                gdb_port = atoi(argv[++i]);
                if (gdb_port <= 0 || gdb_port > 65535) {
                    fprintf(stderr, "Invalid port: %s\n", argv[i]);
                    return 1;
                }
            }
        } else if (strcmp(argv[i], "--max-cycles") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "--max-cycles requires an argument\n");
                return 1;
            }
            max_cycles = strtoull(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--json") == 0) {
            json_mode = 1;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            return 0;
        } else if (argv[i][0] != '-') {
            binary = argv[i];
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    if (binary) {
        Status s = simulator_load(&sim, binary);
        if (s != STATUS_OK) {
            fprintf(stderr, "Failed to load '%s'\n", binary);
            return 1;
        }
    }

    if (gdb_port) {
        GdbStub stub;
        gdb_stub_init(&stub, &sim, gdb_port);
        gdb_stub_run(&stub);
    } else if (json_mode) {
        /* Suppress live UART output — bytes are captured by uart_logger */
        uart_set_output(&sim.uart, null_uart_output, NULL);

        const char* halt_reason = "max_cycles";
        uint64_t    limit       = (max_cycles > 0) ? max_cycles : UINT64_MAX;
        for (uint64_t i = 0; i < limit; i++) {
            Status s = simulator_step(&sim);
            if (s == STATUS_HALTED || sim.halted) {
                halt_reason = "halted";
                break;
            }
            if (s == STATUS_INVALID_INSTRUCTION) {
                halt_reason = "invalid_instruction";
                break;
            }
            if (s != STATUS_OK && s != STATUS_BREAKPOINT_HIT) {
                halt_reason = "error";
                break;
            }
        }
        print_json_report(&sim, halt_reason);
    } else {
        ui_run(&sim);
    }

    return 0;
}
