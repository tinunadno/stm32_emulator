#ifndef STM32_DEBUGGER_H
#define STM32_DEBUGGER_H

#include <stdint.h>

#define DEBUGGER_MAX_BREAKPOINTS 64

/**
 * Simple breakpoint debugger.
 * Maintains a list of addresses where execution should halt.
 */
typedef struct {
    uint32_t breakpoints[DEBUGGER_MAX_BREAKPOINTS];
    int      num_breakpoints;
} Debugger;

void debugger_init(Debugger* dbg);

/** Add a breakpoint at the given address. Returns 0 on success, -1 if full. */
int  debugger_add_breakpoint(Debugger* dbg, uint32_t addr);

/** Remove a breakpoint. Returns 0 on success, -1 if not found. */
int  debugger_remove_breakpoint(Debugger* dbg, uint32_t addr);

/** Check if PC matches any breakpoint. Returns 1 if hit. */
int  debugger_check(const Debugger* dbg, uint32_t pc);

/** List all breakpoints (prints to stdout). */
void debugger_list(const Debugger* dbg);

#endif /* STM32_DEBUGGER_H */
