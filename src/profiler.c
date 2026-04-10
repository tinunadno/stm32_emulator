#include "profiler.h"
#include <string.h>
#include <stdio.h>

static const char* slot_name(uint32_t slot)
{
    switch (slot) {
    case  2: return "NMI";
    case  3: return "HardFault";
    case 15: return "SysTick";
    case 44: return "TIM2 (IRQ28)";
    case 53: return "USART1 (IRQ37)";
    default: {
        /* External IRQ: slot >= 16 */
        static char buf[16];
        if (slot >= 16)
            snprintf(buf, sizeof(buf), "IRQ%u", slot - 16);
        else
            snprintf(buf, sizeof(buf), "EXC%u", slot);
        return buf;
    }
    }
}

void profiler_init(Profiler* p)
{
    memset(p, 0, sizeof(Profiler));
}

void profiler_reset(Profiler* p)
{
    memset(p, 0, sizeof(Profiler));
}

void profiler_enter(Profiler* p, uint32_t slot, uint64_t cycle)
{
    if (slot >= PROF_NUM_SLOTS) return;
    p->slots[slot].entry_cycle = cycle;
}

void profiler_exit(Profiler* p, uint32_t slot, uint64_t cycle)
{
    if (slot >= PROF_NUM_SLOTS) return;
    ProfSlot* s = &p->slots[slot];
    if (s->entry_cycle == 0) return;

    uint64_t elapsed = cycle - s->entry_cycle;
    s->total_cycles += elapsed;
    s->call_count++;
    if (elapsed > s->max_cycles)
        s->max_cycles = elapsed;
    s->entry_cycle = 0;
}

void profiler_count_instr(Profiler* p, uint32_t idx, const char* name)
{
    if (idx >= PROF_INSTR_SLOTS) return;
    ProfInstrSlot* s = &p->instrs[idx];
    if (!s->name) s->name = name;
    s->count++;
}

/* Comparison for qsort — sort descending by count */
static int instr_cmp(const void* a, const void* b)
{
    const ProfInstrSlot* sa = (const ProfInstrSlot*)a;
    const ProfInstrSlot* sb = (const ProfInstrSlot*)b;
    if (sb->count > sa->count) return  1;
    if (sb->count < sa->count) return -1;
    return 0;
}

void profiler_print_instrs(const Profiler* p)
{
    /* Copy non-empty entries, sort, print */
    ProfInstrSlot sorted[PROF_INSTR_SLOTS];
    int n = 0;
    for (int i = 0; i < PROF_INSTR_SLOTS; i++) {
        if (p->instrs[i].count > 0)
            sorted[n++] = p->instrs[i];
    }
    if (n == 0) {
        printf("  (no instruction data)\n");
        return;
    }

    /* simple insertion sort (n is small) */
    for (int i = 1; i < n; i++) {
        ProfInstrSlot key = sorted[i];
        int j = i - 1;
        while (j >= 0 && instr_cmp(&sorted[j], &key) > 0) {
            sorted[j + 1] = sorted[j];
            j--;
        }
        sorted[j + 1] = key;
    }

    printf("%-16s %12s\n", "Instruction", "Count");
    printf("%-16s %12s\n", "----------------", "------------");
    for (int i = 0; i < n; i++) {
        printf("%-16s %12llu\n",
               sorted[i].name,
               (unsigned long long)sorted[i].count);
    }
}

void profiler_print(const Profiler* p)
{
    printf("%-20s %8s %12s %10s %10s\n",
           "Handler", "Calls", "Total cyc", "Avg cyc", "Max cyc");
    printf("%-20s %8s %12s %10s %10s\n",
           "--------------------", "--------",
           "------------", "----------", "----------");

    int any = 0;
    for (uint32_t i = 0; i < PROF_NUM_SLOTS; i++) {
        const ProfSlot* s = &p->slots[i];
        if (s->call_count == 0) continue;
        uint64_t avg = s->total_cycles / s->call_count;
        printf("%-20s %8llu %12llu %10llu %10llu\n",
               slot_name(i),
               (unsigned long long)s->call_count,
               (unsigned long long)s->total_cycles,
               (unsigned long long)avg,
               (unsigned long long)s->max_cycles);
        any = 1;
    }
    if (!any)
        printf("  (no data — run firmware first)\n");

    printf("\n--- Instructions (by count) ---\n");
    profiler_print_instrs(p);
}
