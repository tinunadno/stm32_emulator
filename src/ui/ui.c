#include "ui/ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ======================================================================
 * Command handlers
 * ====================================================================== */

static void cmd_help(Simulator* sim, const char* args);
static void cmd_load(Simulator* sim, const char* args);
static void cmd_run(Simulator* sim, const char* args);
static void cmd_stop(Simulator* sim, const char* args);
static void cmd_step(Simulator* sim, const char* args);
static void cmd_reset(Simulator* sim, const char* args);
static void cmd_reg(Simulator* sim, const char* args);
static void cmd_mem(Simulator* sim, const char* args);
static void cmd_break(Simulator* sim, const char* args);
static void cmd_del_break(Simulator* sim, const char* args);
static void cmd_uart_send(Simulator* sim, const char* args);
static void cmd_quit(Simulator* sim, const char* args);

/* ======================================================================
 * Command table
 * To add a new command, simply add an entry here.
 * ====================================================================== */

static const Command commands[] = {
    {"help",    "Show this help message",                  cmd_help},
    {"load",    "Load binary: load <path>",                cmd_load},
    {"run",     "Run until breakpoint or error",           cmd_run},
    {"stop",    "Stop execution",                          cmd_stop},
    {"step",    "Step N instructions: step [N]",           cmd_step},
    {"reset",   "Reset the simulator",                     cmd_reset},
    {"reg",     "Display registers",                       cmd_reg},
    {"mem",     "Read memory: mem <addr> [count]",         cmd_mem},
    {"break",   "Set breakpoint: break <addr>",            cmd_break},
    {"delete",  "Delete breakpoint: delete <addr>",        cmd_del_break},
    {"uart",    "Send char to UART: uart <char>",          cmd_uart_send},
    {"quit",    "Exit the simulator",                      cmd_quit},
    {NULL, NULL, NULL}  /* Sentinel */
};

/* ======================================================================
 * Helper: parse hex/decimal number
 * ====================================================================== */

static int parse_uint32(const char* str, uint32_t* out)
{
    if (!str || *str == '\0') return -1;

    /* Skip whitespace */
    while (isspace((unsigned char)*str)) str++;
    if (*str == '\0') return -1;

    char* end;
    unsigned long val = strtoul(str, &end, 0);  /* Auto-detect base (0x for hex) */
    if (end == str) return -1;
    *out = (uint32_t)val;
    return 0;
}

/* ======================================================================
 * Command implementations
 * ====================================================================== */

static void cmd_help(Simulator* sim, const char* args)
{
    (void)sim; (void)args;
    printf("Available commands:\n");
    for (const Command* c = commands; c->name != NULL; c++) {
        printf("  %-10s %s\n", c->name, c->help);
    }
}

static void cmd_load(Simulator* sim, const char* args)
{
    if (!args || *args == '\0') {
        printf("Usage: load <path>\n");
        return;
    }
    /* Skip leading whitespace */
    while (isspace((unsigned char)*args)) args++;
    simulator_load(sim, args);
}

static void cmd_run(Simulator* sim, const char* args)
{
    (void)args;
    printf("Running...\n");
    simulator_run(sim);
    printf("Stopped at PC=0x%08X (cycles=%llu)\n",
           sim->core.state.r[REG_PC],
           (unsigned long long)sim->core.state.cycles);
}

static void cmd_stop(Simulator* sim, const char* args)
{
    (void)args;
    simulator_halt(sim);
    printf("Halted\n");
}

static void cmd_step(Simulator* sim, const char* args)
{
    uint32_t count = 1;
    if (args && *args != '\0') {
        if (parse_uint32(args, &count) != 0) count = 1;
    }

    for (uint32_t i = 0; i < count; i++) {
        Status s = simulator_step(sim);
        if (s != STATUS_OK) {
            if (s == STATUS_BREAKPOINT_HIT) {
                printf("Breakpoint at step %u\n", i + 1);
            } else {
                printf("Error at step %u: status=%d\n", i + 1, s);
            }
            break;
        }
    }

    const CoreState* st = core_get_state(&sim->core);
    printf("PC=0x%08X  cycles=%llu\n",
           st->r[REG_PC], (unsigned long long)st->cycles);
}

static void cmd_reset(Simulator* sim, const char* args)
{
    (void)args;
    simulator_reset(sim);
}

static void cmd_reg(Simulator* sim, const char* args)
{
    (void)args;
    const CoreState* st = core_get_state(&sim->core);
    static const char* reg_names[] = {
        "R0", "R1", "R2",  "R3",  "R4",  "R5", "R6", "R7",
        "R8", "R9", "R10", "R11", "R12", "SP", "LR", "PC"
    };

    for (int i = 0; i < 16; i++) {
        printf("%-4s= 0x%08X", reg_names[i], st->r[i]);
        if ((i & 3) == 3) printf("\n");
        else              printf("  ");
    }

    printf("xPSR= 0x%08X  [%c%c%c%c]  cycles=%llu\n",
           st->xpsr,
           (st->xpsr & XPSR_N) ? 'N' : '-',
           (st->xpsr & XPSR_Z) ? 'Z' : '-',
           (st->xpsr & XPSR_C) ? 'C' : '-',
           (st->xpsr & XPSR_V) ? 'V' : '-',
           (unsigned long long)st->cycles);
}

static void cmd_mem(Simulator* sim, const char* args)
{
    if (!args || *args == '\0') {
        printf("Usage: mem <addr> [count]\n");
        return;
    }

    uint32_t addr;
    if (parse_uint32(args, &addr) != 0) {
        printf("Invalid address\n");
        return;
    }

    /* Find optional count argument */
    uint32_t count = 64;  /* Default: 64 bytes */
    const char* p = args;
    while (*p && !isspace((unsigned char)*p)) p++;
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p) parse_uint32(p, &count);
    if (count > 1024) count = 1024;

    /* Dump memory in hex */
    for (uint32_t i = 0; i < count; i += 16) {
        printf("0x%08X: ", addr + i);
        for (uint32_t j = 0; j < 16 && (i + j) < count; j++) {
            printf("%02X ", bus_read(&sim->bus, addr + i + j, 1));
        }
        printf(" |");
        for (uint32_t j = 0; j < 16 && (i + j) < count; j++) {
            uint8_t c = (uint8_t)bus_read(&sim->bus, addr + i + j, 1);
            printf("%c", (c >= 0x20 && c < 0x7F) ? c : '.');
        }
        printf("|\n");
    }
}

static void cmd_break(Simulator* sim, const char* args)
{
    if (!args || *args == '\0') {
        debugger_list(&sim->debugger);
        return;
    }

    uint32_t addr;
    if (parse_uint32(args, &addr) != 0) {
        printf("Invalid address\n");
        return;
    }

    if (debugger_add_breakpoint(&sim->debugger, addr) == 0) {
        printf("Breakpoint set at 0x%08X\n", addr);
    }
}

static void cmd_del_break(Simulator* sim, const char* args)
{
    if (!args || *args == '\0') {
        printf("Usage: delete <addr>\n");
        return;
    }

    uint32_t addr;
    if (parse_uint32(args, &addr) != 0) {
        printf("Invalid address\n");
        return;
    }

    if (debugger_remove_breakpoint(&sim->debugger, addr) == 0) {
        printf("Breakpoint removed at 0x%08X\n", addr);
    } else {
        printf("No breakpoint at 0x%08X\n", addr);
    }
}

static void cmd_uart_send(Simulator* sim, const char* args)
{
    if (!args || *args == '\0') {
        printf("Usage: uart <char>\n");
        return;
    }
    while (isspace((unsigned char)*args)) args++;
    if (*args) {
        uart_incoming_char(&sim->uart, *args);
        printf("Sent '%c' to UART\n", *args);
    }
}

static int quit_flag = 0;

static void cmd_quit(Simulator* sim, const char* args)
{
    (void)sim; (void)args;
    quit_flag = 1;
}

/* ======================================================================
 * Main CLI loop
 * ====================================================================== */

void ui_run(Simulator* sim)
{
    char line[256];
    quit_flag = 0;

    printf("STM32F103C8T6 Simulator\n");
    printf("Type 'help' for available commands\n\n");

    while (!quit_flag) {
        printf("stm32> ");
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin)) {
            break;  /* EOF */
        }

        /* Strip trailing newline */
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';

        /* Skip empty lines */
        if (len == 0) continue;

        /* Split into command and arguments */
        char* cmd = line;
        while (isspace((unsigned char)*cmd)) cmd++;
        char* args = cmd;
        while (*args && !isspace((unsigned char)*args)) args++;
        if (*args) {
            *args = '\0';
            args++;
            while (isspace((unsigned char)*args)) args++;
        }

        /* Look up command in table */
        int found = 0;
        for (const Command* c = commands; c->name != NULL; c++) {
            if (strcmp(cmd, c->name) == 0) {
                c->handler(sim, args);
                found = 1;
                break;
            }
        }

        if (!found) {
            printf("Unknown command: '%s'. Type 'help' for list.\n", cmd);
        }
    }

    printf("Goodbye.\n");
}
