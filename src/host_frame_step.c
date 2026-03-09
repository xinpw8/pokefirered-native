#include "host_frame_step.h"

#include "gba/gba.h"
#include "gpu_regs.h"
#include "host_crt0.h"
#include "host_dma.h"
#include "host_renderer.h"
#include "main.h"

void HostFrameStepRun(HostFrameStepLogicFn logicFn, void *userdata)
{
    int scanline;
    u16 dispstat;
    u16 flags;
    u16 vcountCompare;
    bool8 vblankIrqWillDispatch;
    bool8 hblankIrqWillDispatch;

    /*
     * Frame ordering matches the real GBA main loop:
     *   1. Game logic (callbacks) — produces new register/palette/OAM values
     *   2. VBlank ISR — commits buffered registers, transfers palette & OAM
     *   3. Display period — renders scanlines using committed values
     *
     * On the real GBA, the main loop runs game logic, then calls
     * WaitForVBlank which blocks until VBlank.  VBlank ISR fires and
     * commits the values, then the display period renders with them.
     */

    /* === Phase 1: Game logic === */
    REG_VCOUNT = DISPLAY_HEIGHT + 1;
    if (logicFn != NULL)
        logicFn(userdata);

    /* === Phase 2: VBlank === */
    REG_VCOUNT = DISPLAY_HEIGHT;
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
        dispstat = GetGpuReg(REG_OFFSET_DISPSTAT);
        vcountCompare = dispstat >> 8;
        if ((dispstat & DISPSTAT_VCOUNT_INTR) && scanline == vcountCompare)
        {
            HostInterruptRaise(INTR_FLAG_VCOUNT);
            HostInterruptDispatchAll();
        }
    }

    /* === Phase 3: visible display period === */
    HostRendererStartFrame();

    for (scanline = 0; scanline < DISPLAY_HEIGHT; scanline++)
    {
        REG_VCOUNT = scanline;
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
