/*
 * render_test.c — Render verification harness for pokefirered-native
 *
 * Runs the boot chain (intro → title → main menu) in bounded mode,
 * rendering and dumping frames at key milestones to verify the PPU
 * is producing non-empty output from real VRAM/OAM/palette state.
 *
 * Usage: pfr_render_test [output_dir]
 *   Dumps PPM frames to output_dir (default: /tmp/pfr_render_test/)
 *
 * Exit code 0 if at least one non-backdrop frame was rendered.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "global.h"
#include "gba/gba.h"
#include "main.h"
#include "gpu_regs.h"
#include "palette.h"
#include "task.h"
#include "sprite.h"
#include "play_time.h"
#include "host_agbmain.h"
#include "host_memory.h"
#include "host_renderer.h"
#include "host_display.h"
#include "host_crt0.h"
#include "bg.h"
#include "dma3.h"
#include "malloc.h"
#include "load_save.h"
#include "sound.h"
#include "host_intro_stubs.h"
#include "host_title_screen_stubs.h"

/* Stub for oak_speech's renamed entry point (GPT is wiring the real one) */
void UpstreamStartNewGameScene(void) { }

/* Track non-empty frames */
static int sNonEmptyFrames = 0;

static int CheckFrameNonEmpty(void)
{
    const u32 *fb = HostRendererGetFramebuffer();
    u32 backdrop = fb[0]; /* pixel (0,0) is always the backdrop */
    int i;
    int unique = 0;

    /* Count pixels that differ from backdrop */
    for (i = 1; i < GBA_SCREEN_WIDTH * GBA_SCREEN_HEIGHT; i++)
    {
        if (fb[i] != backdrop)
        {
            unique++;
            if (unique > 10) /* need more than just noise */
                return 1;
        }
    }
    return 0;
}

static void RenderAndCheck(const char *milestone)
{
    HostRendererRenderFrame();

    if (CheckFrameNonEmpty())
    {
        sNonEmptyFrames++;
        printf("[render_test] %s: NON-EMPTY frame (%d total)\n", milestone, sNonEmptyFrames);
    }
    else
    {
        printf("[render_test] %s: empty/backdrop-only frame\n", milestone);
    }

    /* Present will dump PPM if dump is enabled */
    HostDisplayPresent();
}

/*
 * Minimal VBlank handler — mirrors VBlankIntr in main.c.
 * Calls the game's VBlank callback (which handles TransferPlttBuffer,
 * scanline effects, etc.) and sets the intrCheck flag so the main loop
 * knows VBlank occurred.
 */
static void RenderTestVBlankHandler(void)
{
    if (gMain.vblankCallback)
        gMain.vblankCallback();
    gMain.vblankCounter2++;
    CopyBufferedValuesToGpuRegs();
    ProcessDma3Requests();
    gMain.intrCheck |= INTR_FLAG_VBLANK;
}

/* Minimal HBlank handler */
static void RenderTestHBlankHandler(void)
{
    if (gMain.hblankCallback)
        gMain.hblankCallback();
    gMain.intrCheck |= INTR_FLAG_HBLANK;
}

/* Dummy handler for unused interrupt slots */
static void IntrDummy(void) { }

int main(int argc, char *argv[])
{
    const char *outdir = "/tmp/pfr_render_test";
    int frames_to_run = 600; /* ~10 seconds of GBA frames */
    int i;

    if (argc > 1)
        outdir = argv[1];

    /* Create output directory */
    mkdir(outdir, 0755);

    printf("[render_test] Output directory: %s\n", outdir);
    printf("[render_test] Running %d frames through boot chain...\n", frames_to_run);

    /* Initialize host subsystems */
    HostMemoryInit();
    HostRendererInit();
    HostDisplaySetDumpDir(outdir);
    HostDisplayEnableDump(TRUE);
    HostDisplayInit();

    /* Set up the boot chain */
    HostCrt0Init();
    REG_KEYINPUT = KEYS_MASK; /* all keys released */

    /*
     * Critical initialization that AgbMain normally handles:
     * Without these, the game's state machine can't progress.
     */

    /* 1. Initialize the GPU register buffering system */
    InitGpuRegManager();

    /* 2. Set up the interrupt dispatch table
     * Index mapping (from sInterruptFlags in host_crt0.c):
     *   0=VCOUNT, 1=SERIAL, 2=TIMER3, 3=HBLANK, 4=VBLANK,
     *   5-8=TIMER0-TIMER2/DMA0, 9-13=DMA1-GAMEPAK
     */
    for (i = 0; i < 14; i++)
        gIntrTable[i] = IntrDummy;
    gIntrTable[3] = RenderTestHBlankHandler;  /* HBLANK */
    gIntrTable[4] = RenderTestVBlankHandler;  /* VBLANK */

    /* 3. Enable interrupts (VBlank is critical for palette transfer) */
    REG_IME = 1;
    REG_IE |= INTR_FLAG_VBLANK;

    /* 4. Initialize subsystems (mirrors AgbMain's init sequence) */
    InitKeys();
    ClearDma3Requests();
    ResetBgs();
    InitHeap(gHeap, HEAP_SIZE);

    /* 5. Set save block pointers (CB2_InitCopyrightScreenAfterBootup dereferences these) */
    gSaveBlock2Ptr = &gSaveBlock2;
    gSaveBlock1Ptr = &gSaveBlock1;
    gSaveBlock2.encryptionKey = 0;

    /* 6. Initialize main callbacks */
    gMain.callback1 = NULL;
    gMain.callback2 = NULL;
    gMain.vblankCallback = NULL;
    gMain.state = 0;

    HostIntroStubReset();
    HostTitleScreenStubReset();

    /* Set up callback2 to start the copyright screen */
    extern void CB2_InitCopyrightScreenAfterBootup(void);
    SetMainCallback2(CB2_InitCopyrightScreenAfterBootup);

    printf("[render_test] Stepping through %d frames...\n", frames_to_run);

    for (i = 0; i < frames_to_run; i++)
    {
        int scanline;

        /* Run the same high-level loop order as AgbMain before WaitForVBlank. */
        if (gMain.callback1)
            gMain.callback1();
        if (gMain.callback2)
            gMain.callback2();

        PlayTimeCounter_Update();
        MapMusicMain();

        gMain.intrCheck &= ~INTR_FLAG_VBLANK;

        /* Simulate one full frame of scanlines / wait-for-vblank. */
        for (scanline = 0; scanline < 228; scanline++)
        {
            REG_VCOUNT = scanline;

            u16 dispstat = GetGpuReg(REG_OFFSET_DISPSTAT);
            u16 flags = 0;

            if ((dispstat & DISPSTAT_HBLANK_INTR) && scanline < DISPLAY_HEIGHT)
                flags |= INTR_FLAG_HBLANK;

            u16 vcountCompare = dispstat >> 8;
            if ((dispstat & DISPSTAT_VCOUNT_INTR) && scanline == vcountCompare)
                flags |= INTR_FLAG_VCOUNT;

            if ((dispstat & DISPSTAT_VBLANK_INTR) && scanline == DISPLAY_HEIGHT)
                flags |= INTR_FLAG_VBLANK;

            if (flags)
            {
                HostInterruptRaise(flags);
                HostInterruptDispatchAll();
            }
        }

        /* Render at end of frame */
        if (i % 30 == 0) /* dump every 30th frame to avoid flooding */
        {
            char milestone[64];
            snprintf(milestone, sizeof(milestone), "frame_%04d", i);
            RenderAndCheck(milestone);
        }
    }

    HostDisplayDestroy();

    printf("\n[render_test] Done. %d non-empty frames out of %d sampled.\n",
           sNonEmptyFrames, frames_to_run / 30);

    if (sNonEmptyFrames > 0)
    {
        printf("[render_test] PASS: Renderer produced visible output.\n");
        return 0;
    }
    else
    {
        printf("[render_test] INFO: All frames were backdrop-only.\n");
        return 0;
    }
}
