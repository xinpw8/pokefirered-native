#include <string.h>

#include "gba/gba.h"
#include "host_timer.h"
#include "host_crt0.h"

#define TIMER_CASCADE 0x04

struct TimerState {
    u32   counter;       /* current 16-bit counter value (u32 for overflow math) */
    u32   prescaleAccum; /* accumulated sub-prescaler CPU cycles */
    u16   reload;        /* captured from CNT_L at enable time */
    u16   shadowCntH;    /* last-seen CNT_H for edge detection */
    bool8 enabled;
    bool8 cascade;       /* bit 2 of CNT_H */
};

static struct TimerState sTimers[4];

static const u8 sPrescalerShifts[4] = { 0, 6, 8, 10 }; /* 1, 64, 256, 1024 */

static const u16 sTimerIntrFlags[4] = {
    INTR_FLAG_TIMER0,
    INTR_FLAG_TIMER1,
    INTR_FLAG_TIMER2,
    INTR_FLAG_TIMER3,
};

void HostTimerReset(void)
{
    memset(sTimers, 0, sizeof(sTimers));
}

/*
 * Advance a single timer by the given number of ticks.
 * Returns TRUE if the timer overflowed (for cascade propagation).
 */
static bool8 AdvanceTimer(int i, u32 ticks)
{
    struct TimerState *t = &sTimers[i];
    bool8 overflowed = FALSE;

    t->counter += ticks;

    while (t->counter > 0xFFFF)
    {
        overflowed = TRUE;
        t->counter -= (0x10000 - t->reload);

        if (t->shadowCntH & TIMER_INTR_ENABLE)
            HostInterruptRaise(sTimerIntrFlags[i]);
    }

    return overflowed;
}

void HostTimerSync(u32 cyclesElapsed)
{
    bool8 prevOverflowed = FALSE;
    int i;

    for (i = 0; i < 4; i++)
    {
        struct TimerState *t = &sTimers[i];
        volatile u16 *cntL = &REG_TMCNT_L(i);
        volatile u16 *cntH = &REG_TMCNT_H(i);
        u16 curCntH = *cntH;
        bool8 wasEnabled = t->enabled;
        bool8 nowEnabled = (curCntH & TIMER_ENABLE) != 0;

        /* Rising edge: timer just got enabled */
        if (nowEnabled && !wasEnabled)
        {
            t->reload       = *cntL;
            t->counter      = t->reload;
            t->prescaleAccum = 0;
            t->enabled      = TRUE;
            t->cascade      = (curCntH & TIMER_CASCADE) != 0;
            t->shadowCntH   = curCntH;
        }
        /* Falling edge: timer just got disabled */
        else if (!nowEnabled && wasEnabled)
        {
            *cntL           = (u16)t->counter;
            t->enabled      = FALSE;
            t->shadowCntH   = curCntH;
            prevOverflowed  = FALSE;
            continue;
        }

        /* If not enabled, nothing to do */
        if (!t->enabled)
        {
            prevOverflowed = FALSE;
            continue;
        }

        t->shadowCntH = curCntH;

        /* Cascade mode: increment by 1 if previous timer overflowed */
        if (t->cascade)
        {
            if (prevOverflowed)
                prevOverflowed = AdvanceTimer(i, 1);
            else
                prevOverflowed = FALSE;
        }
        else
        {
            /* Normal prescaled counting */
            u8 shift = sPrescalerShifts[curCntH & 0x03];
            u32 totalCycles = t->prescaleAccum + cyclesElapsed;
            u32 ticks = totalCycles >> shift;

            t->prescaleAccum = totalCycles & ((1u << shift) - 1);

            if (ticks > 0)
                prevOverflowed = AdvanceTimer(i, ticks);
            else
                prevOverflowed = FALSE;
        }

        /* Write current counter back to IO register */
        *cntL = (u16)t->counter;
    }
}
