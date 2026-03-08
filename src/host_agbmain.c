#include <pthread.h>
#include <setjmp.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "host_agbmain.h"
#include "host_crt0.h"
#include "host_runtime_stubs.h"
#include "gpu_regs.h"
#include "intro.h"
#include "main.h"

struct HostAgbMainState
{
    jmp_buf escape;
    pthread_t irq_thread;
    atomic_bool irq_thread_should_run;
    bool8 active;
    u16 scanline;
    u32 exit_after_cb2_calls;
    enum HostAgbMainExitReason exit_reason;
};

static struct HostAgbMainState sHostAgbMainState = {0};

static void HostMaybePressSoftResetKeys(void)
{
    if (!sHostAgbMainState.active)
        return;

    if (sHostAgbMainState.exit_after_cb2_calls != 0
     && gMain.callback2 == CB2_InitCopyrightScreenAfterBootup
     && gMain.state >= sHostAgbMainState.exit_after_cb2_calls)
    {
        REG_KEYINPUT = KEYS_MASK & ~(A_BUTTON | B_BUTTON | START_BUTTON | SELECT_BUTTON);
    }
}

static void HostAbortPthread(const char *op, int err)
{
    fprintf(stderr, "host AgbMain pthread %s failed: %s\n", op, strerror(err));
    abort();
}

static void HostAdvanceScanline(void)
{
    u16 dispstat;
    u16 flags = 0;
    u16 vcountCompare;

    sHostAgbMainState.scanline++;
    if (sHostAgbMainState.scanline >= 228)
        sHostAgbMainState.scanline = 0;

    REG_VCOUNT = sHostAgbMainState.scanline;
    dispstat = GetGpuReg(REG_OFFSET_DISPSTAT);
    vcountCompare = dispstat >> 8;

    if ((dispstat & DISPSTAT_HBLANK_INTR) != 0 && REG_VCOUNT < DISPLAY_HEIGHT)
        flags |= INTR_FLAG_HBLANK;

    if ((dispstat & DISPSTAT_VCOUNT_INTR) != 0 && REG_VCOUNT == vcountCompare)
        flags |= INTR_FLAG_VCOUNT;

    if ((dispstat & DISPSTAT_VBLANK_INTR) != 0 && REG_VCOUNT == DISPLAY_HEIGHT)
        flags |= INTR_FLAG_VBLANK;

    if (flags != 0)
        HostInterruptRaise(flags);

    HostInterruptDispatchAll();
    HostMaybePressSoftResetKeys();
}

static void *HostIrqThreadMain(void *unused)
{
    static const struct timespec sleep_time = {
        .tv_sec = 0,
        .tv_nsec = 100000,
    };

    (void)unused;

    while (atomic_load(&sHostAgbMainState.irq_thread_should_run))
    {
        HostAdvanceScanline();
        nanosleep(&sleep_time, NULL);
    }

    return NULL;
}

enum HostAgbMainExitReason HostRunAgbMainUntilSoftReset(u32 exit_after_cb2_calls)
{
    int thread_err;

    if (sHostAgbMainState.active)
    {
        fprintf(stderr, "host AgbMain runner is already active\n");
        abort();
    }

    sHostAgbMainState.active = TRUE;
    sHostAgbMainState.scanline = DISPLAY_HEIGHT;
    sHostAgbMainState.exit_after_cb2_calls = exit_after_cb2_calls;
    sHostAgbMainState.exit_reason = HOST_AGBMAIN_EXIT_NONE;
    atomic_store(&sHostAgbMainState.irq_thread_should_run, TRUE);

    HostCrt0Init();
    REG_VCOUNT = DISPLAY_HEIGHT + 1;
    REG_KEYINPUT = KEYS_MASK;

    thread_err = pthread_create(&sHostAgbMainState.irq_thread, NULL, HostIrqThreadMain, NULL);
    if (thread_err != 0)
        HostAbortPthread("create", thread_err);

    if (setjmp(sHostAgbMainState.escape) == 0)
    {
        AgbMain();
        sHostAgbMainState.exit_reason = HOST_AGBMAIN_EXIT_RETURNED;
    }

    atomic_store(&sHostAgbMainState.irq_thread_should_run, FALSE);
    thread_err = pthread_join(sHostAgbMainState.irq_thread, NULL);
    if (thread_err != 0)
        HostAbortPthread("join", thread_err);

    sHostAgbMainState.active = FALSE;
    REG_KEYINPUT = KEYS_MASK;
    return sHostAgbMainState.exit_reason;
}

void HostAgbMainOnCb2InitCopyrightScreenAfterBootup(void)
{
    if (!sHostAgbMainState.active)
        return;

    if (sHostAgbMainState.exit_after_cb2_calls != 0
     && gHostStubCb2InitCopyrightCalls >= sHostAgbMainState.exit_after_cb2_calls)
    {
        REG_KEYINPUT = KEYS_MASK & ~(A_BUTTON | B_BUTTON | START_BUTTON | SELECT_BUTTON);
    }
}

void HostAgbMainOnSoftReset(u32 resetFlags)
{
    (void)resetFlags;

    if (!sHostAgbMainState.active)
        return;

    sHostAgbMainState.exit_reason = HOST_AGBMAIN_EXIT_SOFT_RESET;
    longjmp(sHostAgbMainState.escape, 1);
}
