#include "peripherals/systick/systick.h"
#include <string.h>
#include <stdio.h>

static void systick_overflow_event(void* ctx);

/* ---- helpers ---- */

static int systick_running(const SysTickState* st)
{
    return (st->csr & SYST_CSR_ENABLE) && (st->rvr > 0);
}

static void systick_schedule_next(SysTickState* st)
{
    if (!systick_running(st))
        return;

    st->generation++;
    uint64_t fire_at = *st->sim_cycle + (uint64_t)st->rvr + 1;
    event_queue_schedule(st->eq, fire_at, systick_overflow_event, st);
}

/* Current value: counts DOWN from RVR to 0 */
static uint32_t systick_cvr_now(const SysTickState* st)
{
    if (!systick_running(st))
        return 0;

    uint64_t elapsed = *st->sim_cycle - st->start_cycle;
    uint64_t period  = (uint64_t)st->rvr + 1;
    uint32_t offset  = (uint32_t)(elapsed % period);
    return st->rvr - offset;
}

/* ---- event callback ---- */

static void systick_overflow_event(void* ctx)
{
    SysTickState* st = (SysTickState*)ctx;

    if (!systick_running(st))
        return;

    /* Set COUNTFLAG */
    st->csr |= SYST_CSR_COUNTFLAG;

    /* Reset start reference */
    st->start_cycle = *st->sim_cycle;

    /* Raise exception if TICKINT set */
    if (st->csr & SYST_CSR_TICKINT)
        nvic_set_systick_pending(st->nvic);

    systick_schedule_next(st);
}

/* ---- public API ---- */

void systick_init(SysTickState* st, NVIC* nvic,
                  EventQueue* eq, const uint64_t* sim_cycle)
{
    memset(st, 0, sizeof(SysTickState));
    st->nvic      = nvic;
    st->eq        = eq;
    st->sim_cycle = sim_cycle;
}

Peripheral systick_as_peripheral(SysTickState* st)
{
    Peripheral p;
    p.ctx   = st;
    p.read  = systick_read;
    p.write = systick_write;
    p.tick  = NULL;
    p.reset = systick_reset;
    return p;
}

void systick_reset(void* ctx)
{
    SysTickState* st = (SysTickState*)ctx;
    NVIC*           nvic      = st->nvic;
    EventQueue*     eq        = st->eq;
    const uint64_t* sim_cycle = st->sim_cycle;

    memset(st, 0, sizeof(SysTickState));
    st->nvic      = nvic;
    st->eq        = eq;
    st->sim_cycle = sim_cycle;
}

uint32_t systick_read(void* ctx, uint32_t offset, uint8_t size)
{
    SysTickState* st = (SysTickState*)ctx;
    (void)size;

    switch (offset) {
    case SYST_CSR_OFFSET: {
        uint32_t val = st->csr;
        st->csr &= ~SYST_CSR_COUNTFLAG;  /* COUNTFLAG cleared on read */
        return val;
    }
    case SYST_RVR_OFFSET:
        return st->rvr & 0x00FFFFFFU;
    case SYST_CVR_OFFSET:
        return systick_cvr_now(st);
    case SYST_CALIB_OFFSET:
        return SYST_CALIB_VALUE;
    default:
        fprintf(stderr, "SysTick: read from unknown offset 0x%02X\n", offset);
        return 0;
    }
}

Status systick_write(void* ctx, uint32_t offset, uint32_t value, uint8_t size)
{
    SysTickState* st = (SysTickState*)ctx;
    (void)size;

    switch (offset) {
    case SYST_CSR_OFFSET: {
        uint32_t was_running = systick_running(st);
        st->csr = (value & ~SYST_CSR_COUNTFLAG);  /* COUNTFLAG is read-only */

        /* Sync NVIC TICKINT enable */
        nvic_enable_systick(st->nvic, !!(st->csr & SYST_CSR_TICKINT));

        if (systick_running(st) && !was_running) {
            st->start_cycle = *st->sim_cycle;
            systick_schedule_next(st);
        } else if (!systick_running(st) && was_running) {
            st->generation++;  /* Invalidate pending event */
        }
        break;
    }
    case SYST_RVR_OFFSET:
        st->rvr = value & 0x00FFFFFFU;
        /* Reload takes effect on next overflow — no immediate reschedule */
        break;
    case SYST_CVR_OFFSET:
        /* Any write clears CVR and COUNTFLAG, restarts count */
        st->csr &= ~SYST_CSR_COUNTFLAG;
        st->start_cycle = *st->sim_cycle;
        if (systick_running(st)) {
            st->generation++;
            systick_schedule_next(st);
        }
        break;
    case SYST_CALIB_OFFSET:
        break;  /* Read-only, ignore writes */
    default:
        fprintf(stderr, "SysTick: write to unknown offset 0x%02X\n", offset);
        return STATUS_ERROR;
    }
    return STATUS_OK;
}
