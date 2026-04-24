#ifndef STM32_PROFILER_H
#define STM32_PROFILER_H

#include <stdint.h>
#include <stdio.h>

/*
 * IRQ slot layout (matches ARM exception numbering):
 *   slot 15        = SysTick (core exception 15)
 *   slot 16 + irq  = external IRQ N  (e.g. slot 44 = IRQ28 = TIM2)
 */
#define PROF_NUM_SLOTS 96

typedef struct {
    uint64_t call_count;
    uint64_t total_cycles;
    uint64_t max_cycles;
    uint64_t entry_cycle;   /* set on enter, cleared on exit */
} ProfSlot;

/*
 * Instruction slot layout:
 *   0..69  = 16-bit Thumb (by index in instr_table)
 *   70     = BL
 *   71     = MOVW
 *   72     = MOVT
 *   73     = AND.W/TST.W
 *   74     = BIC.W
 *   75     = ORR.W/MOV.W
 *   76     = ORN.W/MVN.W
 *   77     = EOR.W/TEQ.W
 *   78     = ADD.W/CMN.W
 *   79     = ADC.W
 *   80     = SBC.W
 *   81     = SUB.W/CMP.W
 *   82     = RSB.W
 *   83     = LDR/STR.W (32-bit load/store)
 *   84     = LDRS.W (signed load)
 *   85     = UMULL
 *   86     = SMULL
 */
#define PROF_INSTR_BL      70
#define PROF_INSTR_MOVW    71
#define PROF_INSTR_MOVT    72
#define PROF_INSTR_AND_W   73
#define PROF_INSTR_BIC_W   74
#define PROF_INSTR_ORR_W   75
#define PROF_INSTR_ORN_W   76
#define PROF_INSTR_EOR_W   77
#define PROF_INSTR_ADD_W   78
#define PROF_INSTR_ADC_W   79
#define PROF_INSTR_SBC_W   80
#define PROF_INSTR_SUB_W   81
#define PROF_INSTR_RSB_W   82
#define PROF_INSTR_LDSTR_W 83
#define PROF_INSTR_LDRS_W  84
#define PROF_INSTR_UMULL   85
#define PROF_INSTR_SMULL   86
#define PROF_INSTR_SLOTS   96

typedef struct {
    const char* name;
    uint64_t    count;
} ProfInstrSlot;

typedef struct {
    ProfSlot      slots[PROF_NUM_SLOTS];
    ProfInstrSlot instrs[PROF_INSTR_SLOTS];
} Profiler;

void profiler_init(Profiler* p);
void profiler_reset(Profiler* p);
void profiler_enter(Profiler* p, uint32_t slot, uint64_t cycle);
void profiler_exit(Profiler* p, uint32_t slot, uint64_t cycle);
void profiler_count_instr(Profiler* p, uint32_t idx, const char* name);
void profiler_print(const Profiler* p);
void profiler_print_instrs(const Profiler* p);

#endif /* STM32_PROFILER_H */
