#include "debugger/debugger.h"
#include <stdio.h>

void debugger_init(Debugger* dbg)
{
    dbg->num_breakpoints = 0;
}

int debugger_add_breakpoint(Debugger* dbg, uint32_t addr)
{
    /* Check if already exists */
    for (int i = 0; i < dbg->num_breakpoints; i++) {
        if (dbg->breakpoints[i] == addr)
            return 0;  /* Already set */
    }

    if (dbg->num_breakpoints >= DEBUGGER_MAX_BREAKPOINTS) {
        fprintf(stderr, "Breakpoint limit reached (%d)\n", DEBUGGER_MAX_BREAKPOINTS);
        return -1;
    }

    dbg->breakpoints[dbg->num_breakpoints++] = addr;
    return 0;
}

int debugger_remove_breakpoint(Debugger* dbg, uint32_t addr)
{
    for (int i = 0; i < dbg->num_breakpoints; i++) {
        if (dbg->breakpoints[i] == addr) {
            /* Shift remaining entries */
            for (int j = i; j < dbg->num_breakpoints - 1; j++) {
                dbg->breakpoints[j] = dbg->breakpoints[j + 1];
            }
            dbg->num_breakpoints--;
            return 0;
        }
    }
    return -1;  /* Not found */
}

int debugger_check(const Debugger* dbg, uint32_t pc)
{
    for (int i = 0; i < dbg->num_breakpoints; i++) {
        if (dbg->breakpoints[i] == pc)
            return 1;
    }
    return 0;
}

void debugger_list(const Debugger* dbg)
{
    if (dbg->num_breakpoints == 0) {
        printf("No breakpoints set\n");
        return;
    }
    printf("Breakpoints:\n");
    for (int i = 0; i < dbg->num_breakpoints; i++) {
        printf("  [%d] 0x%08X\n", i, dbg->breakpoints[i]);
    }
}
