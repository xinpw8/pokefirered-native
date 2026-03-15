#include "host_frame_step.h"

#include <time.h>
#include <stdio.h>
#include "gba/gba.h"
#include "gpu_regs.h"
#include "host_crt0.h"
#include "host_dma.h"
#include "host_renderer.h"
#include "host_timer.h"
#include "main.h"

#define CYCLES_PER_SCANLINE 1232
#define SCANLINES_PER_FRAME 228
#define CYCLES_PER_FRAME    (CYCLES_PER_SCANLINE * SCANLINES_PER_FRAME)

void HostFrameStepRun(HostFrameStepLogicFn logicFn, void *userdata, bool8 renderFrame)
{
    int scanline;
    u16 dispstat;
    u16 flags;
    u16 vcountCompare;
    bool8 vblankIrqWillDispatch;
    bool8 hblankIrqWillDispatch;

    REG_VCOUNT = DISPLAY_HEIGHT + 1;
    HostTimerSync(CYCLES_PER_SCANLINE);
    if (logicFn != NULL)
        logicFn(userdata);

    REG_VCOUNT = DISPLAY_HEIGHT;
    HostTimerSync(CYCLES_PER_SCANLINE);
    dispstat = GetGpuReg(REG_OFFSET_DISPSTAT);
    flags = 0;
    vblankIrqWillDispatch = FALSE;
    if (dispstat & DISPSTAT_VBLANK_INTR)
    {
        flags |= INTR_FLAG_VBLANK;
        if (REG_IME != 0 && (REG_IE & INTR_FLAG_VBLANK) != 0)
            vblankIrqWillDispatch = TRUE;
    }
    vcountCompare = dispstat >> 8;
    if ((dispstat & DISPSTAT_VCOUNT_INTR) && DISPLAY_HEIGHT == vcountCompare)
        flags |= INTR_FLAG_VCOUNT;

    if (!vblankIrqWillDispatch)
        HostDmaTriggerVBlank();

    if (flags)
    {
        HostInterruptRaise(flags);
        HostInterruptDispatchAll();
    }

    for (scanline = DISPLAY_HEIGHT + 2; scanline < 228; scanline++)
    {
        REG_VCOUNT = scanline;
        HostTimerSync(CYCLES_PER_SCANLINE);
        dispstat = GetGpuReg(REG_OFFSET_DISPSTAT);
        vcountCompare = dispstat >> 8;
        if ((dispstat & DISPSTAT_VCOUNT_INTR) && scanline == vcountCompare)
        {
            HostInterruptRaise(INTR_FLAG_VCOUNT);
            HostInterruptDispatchAll();
        }
    }

    if (renderFrame)
        HostRendererStartFrame();

    for (scanline = 0; scanline < DISPLAY_HEIGHT; scanline++)
    {
        REG_VCOUNT = scanline;
        HostTimerSync(CYCLES_PER_SCANLINE);
        if (renderFrame)
            HostRendererRenderScanline(scanline);

        dispstat = GetGpuReg(REG_OFFSET_DISPSTAT);
        flags = 0;
        hblankIrqWillDispatch = FALSE;

        if (dispstat & DISPSTAT_HBLANK_INTR)
        {
            flags |= INTR_FLAG_HBLANK;
            if (REG_IME != 0 && (REG_IE & INTR_FLAG_HBLANK) != 0)
                hblankIrqWillDispatch = TRUE;
        }
        vcountCompare = dispstat >> 8;
        if ((dispstat & DISPSTAT_VCOUNT_INTR) && scanline == vcountCompare)
            flags |= INTR_FLAG_VCOUNT;

        if (!hblankIrqWillDispatch)
            HostDmaTriggerHBlank();

        if (flags)
        {
            HostInterruptRaise(flags);
            HostInterruptDispatchAll();
        }
    }
}

/* --- Profiling accumulators for RunFast --- */
static unsigned long sProfCount = 0;
static double sAccLogic = 0, sAccTimer = 0, sAccVblank = 0;

static inline double pfrn_clock(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e9 + ts.tv_nsec;
}

void HostFrameStepRunFast(HostFrameStepLogicFn logicFn, void *userdata)
{
    u16 dispstat;
    u16 flags;
    bool8 vblankIrqWillDispatch;
    double t0, t1, t2, t3;

    REG_VCOUNT = DISPLAY_HEIGHT + 1;
    t0 = pfrn_clock();
    if (logicFn != NULL)
        logicFn(userdata);
    t1 = pfrn_clock();

    REG_VCOUNT = DISPLAY_HEIGHT;
    HostTimerSync(CYCLES_PER_FRAME);
    t2 = pfrn_clock();

    dispstat = GetGpuReg(REG_OFFSET_DISPSTAT);
    flags = 0;
    vblankIrqWillDispatch = FALSE;

    if (dispstat & DISPSTAT_VBLANK_INTR)
    {
        flags |= INTR_FLAG_VBLANK;
        if (REG_IME != 0 && (REG_IE & INTR_FLAG_VBLANK) != 0)
            vblankIrqWillDispatch = TRUE;
    }

    if (!vblankIrqWillDispatch)
        HostDmaTriggerVBlank();

    if (flags)
    {
        HostInterruptRaise(flags);
        HostInterruptDispatchAll();
    }
    t3 = pfrn_clock();

    REG_VCOUNT = 0;

    sAccLogic  += (t1 - t0);
    sAccTimer  += (t2 - t1);
    sAccVblank += (t3 - t2);
    sProfCount++;

    if (sProfCount % 60000 == 0) {
        double total = sAccLogic + sAccTimer + sAccVblank;
        fprintf(stderr, "PFRN_PROF frames=%lu logic=%.0fns(%.1f%%) timer=%.0fns(%.1f%%) vblank=%.0fns(%.1f%%) total=%.0fns => %.0f FPS\n",
            sProfCount,
            sAccLogic/sProfCount, 100.0*sAccLogic/total,
            sAccTimer/sProfCount, 100.0*sAccTimer/total,
            sAccVblank/sProfCount, 100.0*sAccVblank/total,
            total/sProfCount, 1e9/(total/sProfCount));
    }
}
