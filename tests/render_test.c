/*
 * render_test.c — Golden-frame render harness for pokefirered-native
 *
 * Runs the boot chain headlessly, replays the same scripted input ranges
 * used by the mGBA golden capture, and dumps named PPMs for each manifest
 * milestone. This keeps the native capture path aligned with the golden
 * reference workflow used by pfr_golden_check.
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
#include "host_memory.h"
#include "host_renderer.h"
#include "host_display.h"
#include "host_crt0.h"
#include "host_capture_manifest.h"
#include "host_frame_step.h"
#include "bg.h"
#include "dma3.h"
#include "malloc.h"
#include "load_save.h"
#include "save.h"
#include "sound.h"
#include "host_intro_stubs.h"
#include "host_title_screen_stubs.h"
#include "host_oak_speech_stubs.h"
#include "host_new_game_stubs.h"
#include "host_sound_init.h"

static struct HostCaptureInputScript sInputScript;
static struct HostCaptureManifest sManifest;
static const char *sOutputDir = "/tmp/pfr_render_test";
static int sCapturedFrames;
static int sNonEmptyFrames;

/* Not exported in main.h but defined in main.c, set by InitKeys() */
extern u16 gKeyRepeatContinueDelay;

static void ApplyScriptedInput(u32 frame)
{
    u16 pressedMask = HostCaptureButtonsForFrame(&sInputScript, frame);
    REG_KEYINPUT = (u16)(KEYS_MASK & ~pressedMask);
}

static void RenderTestReadKeys(void)
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

static void DumpNamedFramebuffer(const char *label)
{
    const u32 *fb = HostRendererGetFramebuffer();
    char path[512];
    FILE *f;
    int i;

    snprintf(path, sizeof(path), "%s/%s.ppm", sOutputDir, label);
    f = fopen(path, "wb");
    if (f == NULL)
        return;

    fprintf(f, "P6\n%d %d\n255\n", GBA_SCREEN_WIDTH, GBA_SCREEN_HEIGHT);
    for (i = 0; i < GBA_SCREEN_WIDTH * GBA_SCREEN_HEIGHT; i++)
    {
        u32 pixel = fb[i];
        u8 rgb[3];
        rgb[0] = (pixel >> 16) & 0xFF;
        rgb[1] = (pixel >> 8) & 0xFF;
        rgb[2] = pixel & 0xFF;
        fwrite(rgb, 1, 3, f);
    }

    fclose(f);
}

static int CheckFrameNonEmpty(void)
{
    const u32 *fb = HostRendererGetFramebuffer();
    u32 backdrop = fb[0];
    int i;
    int unique = 0;

    for (i = 1; i < GBA_SCREEN_WIDTH * GBA_SCREEN_HEIGHT; i++)
    {
        if (fb[i] != backdrop)
        {
            unique++;
            if (unique > 10)
                return 1;
        }
    }
    return 0;
}

static void LogMilestoneGpuState(const char *milestone)
{
    int i, j, nz;

    printf("[render_test] %s regs DISPCNT=0x%04X BG0=0x%04X BG1=0x%04X BG2=0x%04X BG3=0x%04X BLDCNT=0x%04X\n",
           milestone,
           GetGpuReg(REG_OFFSET_DISPCNT),
           GetGpuReg(REG_OFFSET_BG0CNT),
           GetGpuReg(REG_OFFSET_BG1CNT),
           GetGpuReg(REG_OFFSET_BG2CNT),
           GetGpuReg(REG_OFFSET_BG3CNT),
           GetGpuReg(REG_OFFSET_BLDCNT));

    /* Palette group summary: 32 groups of 16 entries each.
     * Groups  0-15 = BG palettes, groups 16-31 = OBJ palettes.
     * For each source: hw PLTT (0x05000000), unfaded buffer, faded buffer. */
    printf("[render_test] %s hw_PLTT  groups (nonzero/16): BG[", milestone);
    for (i = 0; i < 32; i++)
    {
        nz = 0;
        for (j = 0; j < 16; j++)
        {
            if (((volatile u16 *)PLTT)[i * 16 + j] != 0)
                nz++;
        }
        if (i == 16)
            printf("] OBJ[");
        else if (i > 0 && i != 16)
            printf(" ");
        printf("%d", nz);
    }
    printf("]\n");

    printf("[render_test] %s unfaded  groups (nonzero/16): BG[", milestone);
    for (i = 0; i < 32; i++)
    {
        nz = 0;
        for (j = 0; j < 16; j++)
        {
            if (gPlttBufferUnfaded[i * 16 + j] != 0)
                nz++;
        }
        if (i == 16)
            printf("] OBJ[");
        else if (i > 0 && i != 16)
            printf(" ");
        printf("%d", nz);
    }
    printf("]\n");

    printf("[render_test] %s faded    groups (nonzero/16): BG[", milestone);
    for (i = 0; i < 32; i++)
    {
        nz = 0;
        for (j = 0; j < 16; j++)
        {
            if (gPlttBufferFaded[i * 16 + j] != 0)
                nz++;
        }
        if (i == 16)
            printf("] OBJ[");
        else if (i > 0 && i != 16)
            printf(" ");
        printf("%d", nz);
    }
    printf("]\n");
}


static void LogMilestoneVramState(const char *milestone)
{
    const u32 *vram = (const u32 *)0x06000000;
    int region, j, nz;

    /* VRAM is 96KB = 24 regions of 4KB each */
    printf("[render_test] %s VRAM 4KB regions (nonzero u32s/1024):", milestone);
    for (region = 0; region < 24; region++)
    {
        nz = 0;
        for (j = 0; j < 1024; j++)
        {
            if (vram[region * 1024 + j] != 0)
                nz++;
        }
        if (nz > 0)
            printf(" [0x%04X]=%d", region * 0x1000, nz);
    }
    printf("\n");

    /* Check actual screen bases from BGxCNT registers */
    {
        const u16 *sb;
        int nonzero_entries;
        int bg;
        const volatile u16 *bgcnt_base = (const volatile u16 *)0x04000008;

        for (bg = 0; bg < 4; bg++) {
            u16 bgcnt = bgcnt_base[bg]; /* BG0CNT..BG3CNT at 0x04000008..0x0400000E */
            u32 mapBase = (bgcnt >> 8) & 0x1F;
            u32 screenOffset = mapBase * 0x800;
            sb = (const u16 *)(0x06000000 + screenOffset);
            nonzero_entries = 0;
            for (j = 0; j < 1024; j++)
                if (sb[j] != 0) nonzero_entries++;
            printf("[render_test] %s screenBase 0x%04X (BG%d): %d/1024 nonzero entries\n",
                   milestone, screenOffset, bg, nonzero_entries);
        }
    }
}

static void CaptureMilestone(const char *milestone)
{
    DumpNamedFramebuffer(milestone);
    LogMilestoneGpuState(milestone);
    LogMilestoneVramState(milestone);
    sCapturedFrames++;

    if (CheckFrameNonEmpty())
    {
        sNonEmptyFrames++;
        printf("[render_test] %s: NON-EMPTY frame (%d total)\n", milestone, sNonEmptyFrames);
    }
    else
    {
        printf("[render_test] %s: empty frame\n", milestone);
    }
}

static void RenderTestVBlankHandler(void)
{
    extern u16 Random(void);

    if (gMain.vblankCallback)
        gMain.vblankCallback();
    gMain.vblankCounter2++;
    CopyBufferedValuesToGpuRegs();
    ProcessDma3Requests();
    Random();
    gMain.intrCheck |= INTR_FLAG_VBLANK;
}

static void RenderTestHBlankHandler(void)
{
    if (gMain.hblankCallback)
        gMain.hblankCallback();
    gMain.intrCheck |= INTR_FLAG_HBLANK;
}

static void IntrDummy(void) { }

static void RenderTestFrameLogic(void *unused)
{
    (void)unused;

    RenderTestReadKeys();

    if (gMain.callback1)
        gMain.callback1();
    if (gMain.callback2)
        gMain.callback2();

    PlayTimeCounter_Update();
    MapMusicMain();
}

int main(int argc, char *argv[])
{
    const char *manifestPath = "golden_frames/manifest.txt";
    int i;
    u32 frame;
    u32 nextMilestone = 0;
    u32 lastFrame = 0;
    extern void CB2_InitCopyrightScreenAfterBootup(void);

    if (argc > 1)
        sOutputDir = argv[1];
    if (argc > 2)
        manifestPath = argv[2];

    mkdir(sOutputDir, 0755);

    if (!HostCaptureLoadManifest(manifestPath, &sManifest))
        return 1;
    if (!HostCaptureLoadInputScript(sManifest.input_path, &sInputScript))
        return 1;

    for (i = 0; i < (int)sManifest.milestone_count; i++)
    {
        if (sManifest.milestones[i].frame > lastFrame)
            lastFrame = sManifest.milestones[i].frame;
    }

    printf("[render_test] Output directory: %s\n", sOutputDir);
    printf("[render_test] Manifest: %s\n", manifestPath);
    printf("[render_test] Input script: %s\n", sManifest.input_path);
    printf("[render_test] Running to frame %u (%u milestones)\n",
           lastFrame, sManifest.milestone_count);

    HostMemoryInit();
    HostNativeSoundInit();
    HostRendererInit();
    HostDisplaySetDumpDir(sOutputDir);
    HostDisplayEnableDump(FALSE);
    HostDisplayInit();

    HostCrt0Init();
    REG_KEYINPUT = KEYS_MASK;

    InitGpuRegManager();

    for (i = 0; i < 14; i++)
        gIntrTable[i] = IntrDummy;
    gIntrTable[3] = RenderTestHBlankHandler;
    gIntrTable[4] = RenderTestVBlankHandler;

    REG_IME = 1;
    REG_IE |= INTR_FLAG_VBLANK;

    InitKeys();
    ClearDma3Requests();
    ResetBgs();
    InitHeap(gHeap, HEAP_SIZE);

    /* Font system initialization (required for text rendering) */
    {
        extern void SetDefaultFontsPointer(void);
        SetDefaultFontsPointer();
    }

    gSaveBlock2Ptr = &gSaveBlock2;
    gSaveBlock1Ptr = &gSaveBlock1;
    gSaveBlock2.encryptionKey = 0;
    gHostIntroStubLoadGameSaveResult = SAVE_STATUS_EMPTY;

    HostNewGameStubReset();
    HostIntroStubReset();
    HostTitleScreenStubReset();
    HostOakSpeechStubReset();

    gMain.callback1 = NULL;
    gMain.callback2 = NULL;
    gMain.vblankCallback = NULL;
    gMain.state = 0;

    SetMainCallback2(CB2_InitCopyrightScreenAfterBootup);

    for (frame = 1; frame <= lastFrame; frame++)
    {
        ApplyScriptedInput(frame);
        HostFrameStepRun(RenderTestFrameLogic, NULL);

        while (nextMilestone < sManifest.milestone_count
            && sManifest.milestones[nextMilestone].frame == frame)
        {
            CaptureMilestone(sManifest.milestones[nextMilestone].name);
            nextMilestone++;
        }
    }

    HostDisplayDestroy();

    printf("\n[render_test] Done. %d non-empty frames out of %d captured milestones.\n",
           sNonEmptyFrames, sCapturedFrames);

    if (nextMilestone != sManifest.milestone_count)
    {
        printf("[render_test] FAIL: captured %u/%u milestones.\n",
               nextMilestone, sManifest.milestone_count);
        return 1;
    }

    printf("[render_test] Completed %u milestones.\n", sManifest.milestone_count);
    return 0;
}
