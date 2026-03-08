/*
 * pfr_play.c — Interactive player for pokefirered-native
 *
 * Opens an SDL2 window and runs the game's boot chain (copyright →
 * intro → title → main menu) with live keyboard input.
 *
 * Frame model: simulated scanlines + interrupt dispatch + callback
 * stepping + subsystem updates, matching the validated render_test
 * harness. HostDisplayPresent() handles rendering, display, input
 * polling, and 60 Hz frame pacing via VSYNC.
 *
 * Keyboard mapping (defined in host_display.c):
 *   Z=A  X=B  RShift=Select  Enter=Start
 *   Arrows=D-pad  S=R  A=L  Escape=Quit
 */

#include <stdio.h>
#include <string.h>

#include "global.h"
#include "gba/gba.h"
#include "main.h"
#include "gpu_regs.h"
#include "palette.h"
#include "task.h"
#include "sprite.h"
#include "bg.h"
#include "dma3.h"
#include "malloc.h"
#include "load_save.h"
#include "host_memory.h"
#include "host_renderer.h"
#include "host_display.h"
#include "host_crt0.h"
#include "host_intro_stubs.h"
#include "host_title_screen_stubs.h"

/* Stub for oak_speech's renamed entry point */
void UpstreamStartNewGameScene(void) { }

/* ── Interrupt handlers ────────────────────────────────────── */

static void PlayVBlankHandler(void)
{
    if (gMain.vblankCallback)
        gMain.vblankCallback();
    gMain.intrCheck |= INTR_FLAG_VBLANK;
}

static void PlayHBlankHandler(void)
{
    if (gMain.hblankCallback)
        gMain.hblankCallback();
    gMain.intrCheck |= INTR_FLAG_HBLANK;
}

static void IntrDummy(void) { }

/* ── Key reading (mirrors main.c's static ReadKeys) ────────── */

static void PlayReadKeys(void)
{
    u16 keyInput = REG_KEYINPUT ^ KEYS_MASK;

    gMain.newKeysRaw = keyInput & ~gMain.heldKeysRaw;
    gMain.newKeys = gMain.newKeysRaw;
    gMain.newAndRepeatedKeys = gMain.newKeysRaw;

    if (keyInput != 0 && gMain.heldKeys == keyInput)
    {
        gMain.keyRepeatCounter--;
        if (gMain.keyRepeatCounter == 0)
        {
            gMain.newAndRepeatedKeys = keyInput;
            gMain.keyRepeatCounter = gKeyRepeatContinueDelay;
        }
    }
    else
    {
        gMain.keyRepeatCounter = gKeyRepeatStartDelay;
    }

    gMain.heldKeysRaw = keyInput;
    gMain.heldKeys = gMain.heldKeysRaw;

    if (gSaveBlock2Ptr->optionsButtonMode == OPTIONS_BUTTON_MODE_L_EQUALS_A)
    {
        if (JOY_NEW(L_BUTTON))
            gMain.newKeys |= A_BUTTON;
        if (JOY_HELD(L_BUTTON))
            gMain.heldKeys |= A_BUTTON;
    }

    if (JOY_NEW(gMain.watchedKeysMask))
        gMain.watchedKeysPressed = TRUE;
}

/* ── Frame simulation ──────────────────────────────────────── */

static void StepFrame(void)
{
    int scanline;

    /* 1. Read key state from REG_KEYINPUT (set by SDL PollInput last frame) */
    PlayReadKeys();

    /* 2. Simulate 228 scanlines with interrupt dispatch */
    for (scanline = 0; scanline < 228; scanline++)
    {
        u16 dispstat, flags, vcountCompare;

        REG_VCOUNT = scanline;
        dispstat = GetGpuReg(REG_OFFSET_DISPSTAT);
        flags = 0;

        if ((dispstat & DISPSTAT_HBLANK_INTR) && scanline < DISPLAY_HEIGHT)
            flags |= INTR_FLAG_HBLANK;

        vcountCompare = dispstat >> 8;
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

    /* 3. Game logic (mirrors AgbMain's main loop) */
    if (gMain.callback1)
        gMain.callback1();
    if (gMain.callback2)
        gMain.callback2();

    RunTasks();
    AnimateSprites();
    BuildOamBuffer();
    UpdatePaletteFade();
    CopyBufferedValuesToGpuRegs();
}

/* ── Main ──────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    int i;

    (void)argc;
    (void)argv;

    /* Initialize host subsystems */
    HostMemoryInit();
    HostRendererInit();

    if (!HostDisplayInit())
    {
        fprintf(stderr, "pfr_play: Failed to initialize display (SDL2 required)\n");
        return 1;
    }

    /* Boot chain */
    HostCrt0Init();
    REG_KEYINPUT = KEYS_MASK; /* all keys released */

    /* ── AgbMain initialization sequence ── */

    InitGpuRegManager();

    /* Interrupt dispatch table */
    for (i = 0; i < 14; i++)
        gIntrTable[i] = IntrDummy;
    gIntrTable[3] = PlayHBlankHandler;
    gIntrTable[4] = PlayVBlankHandler;

    REG_IME = 1;
    REG_IE |= INTR_FLAG_VBLANK;

    InitKeys();
    ClearDma3Requests();
    ResetBgs();
    InitHeap(gHeap, HEAP_SIZE);

    /* Save block pointers (required by copyright screen) */
    gSaveBlock2Ptr = &gSaveBlock2;
    gSaveBlock1Ptr = &gSaveBlock1;
    gSaveBlock2.encryptionKey = 0;

    gMain.callback1 = NULL;
    gMain.callback2 = NULL;
    gMain.vblankCallback = NULL;
    gMain.state = 0;

    HostIntroStubReset();
    HostTitleScreenStubReset();

    {
        extern void CB2_InitCopyrightScreenAfterBootup(void);
        SetMainCallback2(CB2_InitCopyrightScreenAfterBootup);
    }

    /* ── Main loop ── */

    for (;;)
    {
        StepFrame();

        /* HostDisplayPresent: render → present → poll input → vsync pacing.
         * Returns FALSE on ESC / window close. */
        if (!HostDisplayPresent())
            break;
    }

    HostDisplayDestroy();
    return 0;
}
