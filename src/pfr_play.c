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
#include <stdlib.h>
#include <sys/stat.h>
#include <string.h>

#include "global.h"
#include "gba/gba.h"
#include "main.h"
#include "gpu_regs.h"
#include "intro.h"
#include "palette.h"
#include "task.h"
#include "sprite.h"
#include "bg.h"
#include "dma3.h"
#include "play_time.h"
#include "malloc.h"
#include "load_save.h"
#include "main_menu.h"
#include "oak_speech.h"
#include "sound.h"
#include "title_screen.h"
#include "host_memory.h"
#include "host_renderer.h"
#include "host_display.h"
#include "host_crt0.h"
#include "host_intro_stubs.h"
#include "host_new_game_stubs.h"
#include "host_oak_speech_stubs.h"
#include "host_title_screen_stubs.h"

/* ── Interrupt handlers ────────────────────────────────────── */

extern const u8 gOakSpeech_Text_IStudyPokemon[];
extern const u8 gOakSpeech_Text_TellMeALittleAboutYourself[];
extern const u8 gOakSpeech_Text_AskPlayerGender[];
extern const u8 gOakSpeech_Text_YourNameWhatIsIt[];
extern const u8 gOakSpeech_Text_YourRivalsNameWhatWasIt[];

static FILE *sTraceFile;
static MainCallback sLastCallback1;
static MainCallback sLastCallback2;
static const u8 *sLastOakPlaceholderSource;
static u32 sLastMainMenuInitCalls;
static u32 sLastStartNewGameSceneCalls;
static u32 sLastNamingCalls;
static u32 sLastCB2NewGameCalls;
static u32 sLastControlsGuidePage1Loads;
static u32 sLastControlsGuidePage2Loads;
static u32 sLastControlsGuidePage3Loads;
static u32 sLastPikachuIntroPage1Loads;
static u32 sLastPikachuIntroPage2Loads;
static u32 sLastPikachuIntroPage3Loads;
static u32 sLastWelcomePrints;
static u32 sLastThisWorldPrints;
static u32 sUniformFrameRunLength;
static u32 sLastUniformColor;
static bool8 sSnapshotRequested;
static char sSnapshotLabel[96];

#define MAX_SCRIPTED_INPUT_RANGES 256

struct ScriptedInputRange
{
    u32 startFrame;
    u32 endFrame;
    u16 pressedMask;
};

static struct ScriptedInputRange sScriptedInputRanges[MAX_SCRIPTED_INPUT_RANGES];
static u32 sScriptedInputRangeCount;
static bool8 sScriptedInputEnabled;

static u16 ParseInputToken(const char *token)
{
    if (strcmp(token, "A") == 0)
        return A_BUTTON;
    if (strcmp(token, "B") == 0)
        return B_BUTTON;
    if (strcmp(token, "SELECT") == 0)
        return SELECT_BUTTON;
    if (strcmp(token, "START") == 0)
        return START_BUTTON;
    if (strcmp(token, "RIGHT") == 0)
        return DPAD_RIGHT;
    if (strcmp(token, "LEFT") == 0)
        return DPAD_LEFT;
    if (strcmp(token, "UP") == 0)
        return DPAD_UP;
    if (strcmp(token, "DOWN") == 0)
        return DPAD_DOWN;
    if (strcmp(token, "R") == 0)
        return R_BUTTON;
    if (strcmp(token, "L") == 0)
        return L_BUTTON;
    if (strcmp(token, "NONE") == 0)
        return 0;
    return 0xFFFF;
}

static bool8 ParseInputMask(const char *maskText, u16 *outMask)
{
    char buffer[128];
    char *token;
    char *savePtr = NULL;
    u16 mask = 0;

    if (strncmp(maskText, "0x", 2) == 0 || strncmp(maskText, "0X", 2) == 0)
    {
        char *endPtr = NULL;
        unsigned long value = strtoul(maskText, &endPtr, 16);

        if (endPtr == NULL || *endPtr != '\0')
            return FALSE;
        *outMask = (u16)value;
        return TRUE;
    }

    snprintf(buffer, sizeof(buffer), "%.127s", maskText);
    token = strtok_r(buffer, "+|,", &savePtr);
    while (token != NULL)
    {
        u16 value = ParseInputToken(token);

        if (value == 0xFFFF)
            return FALSE;
        mask |= value;
        token = strtok_r(NULL, "+|,", &savePtr);
    }

    *outMask = mask;
    return TRUE;
}

static bool8 LoadScriptedInputFile(const char *path)
{
    FILE *file = fopen(path, "r");
    char line[256];
    u32 lineNumber = 0;

    if (file == NULL)
    {
        fprintf(stderr, "pfr_play: could not open input script %s\n", path);
        return FALSE;
    }

    sScriptedInputRangeCount = 0;
    while (fgets(line, sizeof(line), file) != NULL)
    {
        char *hash;
        char *startText;
        char *endText;
        char *maskText;
        char *savePtr = NULL;
        unsigned long startFrame;
        unsigned long endFrame;
        u16 mask;

        lineNumber++;
        hash = strchr(line, '#');
        if (hash != NULL)
            *hash = '\0';

        startText = strtok_r(line, " \t\r\n", &savePtr);
        if (startText == NULL)
            continue;
        endText = strtok_r(NULL, " \t\r\n", &savePtr);
        maskText = strtok_r(NULL, " \t\r\n", &savePtr);

        if (endText == NULL || maskText == NULL)
        {
            fprintf(stderr, "pfr_play: malformed input script line %u\n", lineNumber);
            fclose(file);
            return FALSE;
        }

        startFrame = strtoul(startText, NULL, 10);
        endFrame = strtoul(endText, NULL, 10);
        if (endFrame < startFrame)
        {
            fprintf(stderr, "pfr_play: invalid frame range on line %u\n", lineNumber);
            fclose(file);
            return FALSE;
        }

        if (!ParseInputMask(maskText, &mask))
        {
            fprintf(stderr, "pfr_play: invalid input mask on line %u: %s\n", lineNumber, maskText);
            fclose(file);
            return FALSE;
        }

        if (sScriptedInputRangeCount >= MAX_SCRIPTED_INPUT_RANGES)
        {
            fprintf(stderr, "pfr_play: too many scripted input ranges\n");
            fclose(file);
            return FALSE;
        }

        sScriptedInputRanges[sScriptedInputRangeCount].startFrame = (u32)startFrame;
        sScriptedInputRanges[sScriptedInputRangeCount].endFrame = (u32)endFrame;
        sScriptedInputRanges[sScriptedInputRangeCount].pressedMask = mask;
        sScriptedInputRangeCount++;
    }

    fclose(file);
    sScriptedInputEnabled = TRUE;
    return TRUE;
}

static void ApplyScriptedInput(u32 frame)
{
    u16 pressedMask = 0;
    u32 i;

    if (!sScriptedInputEnabled)
        return;

    for (i = 0; i < sScriptedInputRangeCount; i++)
    {
        if (frame >= sScriptedInputRanges[i].startFrame && frame <= sScriptedInputRanges[i].endFrame)
            pressedMask |= sScriptedInputRanges[i].pressedMask;
    }

    REG_KEYINPUT = KEYS_MASK & ~pressedMask;
}

static void RequestSnapshot(const char *label)
{
    snprintf(sSnapshotLabel, sizeof(sSnapshotLabel), "%.95s", label);
    sSnapshotRequested = TRUE;
}

static void SanitizeSnapshotLabel(char *label)
{
    size_t i;

    for (i = 0; label[i] != '\0'; i++)
    {
        char c = label[i];

        if (!((c >= 'a' && c <= 'z')
           || (c >= 'A' && c <= 'Z')
           || (c >= '0' && c <= '9')
           || c == '_' || c == '-'))
        {
            label[i] = '_';
        }
    }
}

static void DumpCurrentFramebuffer(u32 frame, const char *label)
{
    char safeLabel[96];
    char path[192];
    const u32 *fb = HostRendererGetFramebuffer();
    FILE *f;
    int i;

    snprintf(safeLabel, sizeof(safeLabel), "%s", label);
    SanitizeSnapshotLabel(safeLabel);
    snprintf(path, sizeof(path), "pfr_play_frames/%06u_%s.ppm", frame, safeLabel);

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

static void TraceLog(u32 frame, const char *message)
{
    if (sTraceFile != NULL)
    {
        fprintf(sTraceFile, "[%06u] %s\n", frame, message);
        fflush(sTraceFile);
    }
    fprintf(stderr, "[%06u] %s\n", frame, message);
}

static const char *CallbackName(MainCallback callback)
{
    if (callback == NULL)
        return "NULL";
    if (callback == CB2_InitCopyrightScreenAfterBootup)
        return "CB2_InitCopyrightScreenAfterBootup";
    if (callback == CB2_InitTitleScreen)
        return "CB2_InitTitleScreen";
    if (callback == CB2_InitMainMenu)
        return "CB2_InitMainMenu";
    if (callback == HostCB1_Overworld)
        return "HostCB1_Overworld";
    if (callback == HostCB2_Overworld)
        return "HostCB2_Overworld";
    return "(unknown)";
}

static const char *OakPlaceholderName(const u8 *text)
{
    if (text == NULL)
        return "NULL";
    if (text == gOakSpeech_Text_IStudyPokemon)
        return "gOakSpeech_Text_IStudyPokemon";
    if (text == gOakSpeech_Text_TellMeALittleAboutYourself)
        return "gOakSpeech_Text_TellMeALittleAboutYourself";
    if (text == gOakSpeech_Text_AskPlayerGender)
        return "gOakSpeech_Text_AskPlayerGender";
    if (text == gOakSpeech_Text_YourNameWhatIsIt)
        return "gOakSpeech_Text_YourNameWhatIsIt";
    if (text == gOakSpeech_Text_YourRivalsNameWhatWasIt)
        return "gOakSpeech_Text_YourRivalsNameWhatWasIt";
    return "(other Oak placeholder)";
}

static void TraceCallbackChange(u32 frame, const char *label, MainCallback callback)
{
    char buffer[160];

    snprintf(buffer, sizeof(buffer), "%s -> %s (%p)", label, CallbackName(callback), (void *)callback);
    TraceLog(frame, buffer);
    RequestSnapshot(buffer);
}

static void TraceInput(u32 frame)
{
    char buffer[128];

    if (gMain.newKeys == 0)
        return;

    snprintf(buffer, sizeof(buffer),
             "input newKeys=0x%04X heldKeys=0x%04X callback2=%s",
             gMain.newKeys,
             gMain.heldKeys,
             CallbackName(gMain.callback2));
    TraceLog(frame, buffer);
}

static void TraceMilestones(u32 frame)
{
    char buffer[160];

    if (gMain.callback1 != sLastCallback1)
    {
        sLastCallback1 = gMain.callback1;
        TraceCallbackChange(frame, "callback1", gMain.callback1);
    }

    if (gMain.callback2 != sLastCallback2)
    {
        sLastCallback2 = gMain.callback2;
        TraceCallbackChange(frame, "callback2", gMain.callback2);
    }

    if (gHostTitleStubCB2InitMainMenuCalls != sLastMainMenuInitCalls)
    {
        sLastMainMenuInitCalls = gHostTitleStubCB2InitMainMenuCalls;
        snprintf(buffer, sizeof(buffer), "main menu init calls=%u", sLastMainMenuInitCalls);
        TraceLog(frame, buffer);
        RequestSnapshot("main_menu_init");
    }

    if (gHostTitleStubStartNewGameSceneCalls != sLastStartNewGameSceneCalls)
    {
        sLastStartNewGameSceneCalls = gHostTitleStubStartNewGameSceneCalls;
        snprintf(buffer, sizeof(buffer), "StartNewGameScene calls=%u", sLastStartNewGameSceneCalls);
        TraceLog(frame, buffer);
        RequestSnapshot("start_new_game_scene");
    }

    if (gHostOakSpeechControlsGuidePage1Loads != sLastControlsGuidePage1Loads)
    {
        sLastControlsGuidePage1Loads = gHostOakSpeechControlsGuidePage1Loads;
        TraceLog(frame, "Oak controls guide page 1 reached");
        RequestSnapshot("oak_controls_page_1");
    }
    if (gHostOakSpeechControlsGuidePage2Loads != sLastControlsGuidePage2Loads)
    {
        sLastControlsGuidePage2Loads = gHostOakSpeechControlsGuidePage2Loads;
        TraceLog(frame, "Oak controls guide page 2 reached");
        RequestSnapshot("oak_controls_page_2");
    }
    if (gHostOakSpeechControlsGuidePage3Loads != sLastControlsGuidePage3Loads)
    {
        sLastControlsGuidePage3Loads = gHostOakSpeechControlsGuidePage3Loads;
        TraceLog(frame, "Oak controls guide page 3 reached");
        RequestSnapshot("oak_controls_page_3");
    }
    if (gHostOakSpeechPikachuIntroPage1Loads != sLastPikachuIntroPage1Loads)
    {
        sLastPikachuIntroPage1Loads = gHostOakSpeechPikachuIntroPage1Loads;
        TraceLog(frame, "Oak Pikachu intro page 1 reached");
        RequestSnapshot("oak_pikachu_intro_1");
    }
    if (gHostOakSpeechPikachuIntroPage2Loads != sLastPikachuIntroPage2Loads)
    {
        sLastPikachuIntroPage2Loads = gHostOakSpeechPikachuIntroPage2Loads;
        TraceLog(frame, "Oak Pikachu intro page 2 reached");
        RequestSnapshot("oak_pikachu_intro_2");
    }
    if (gHostOakSpeechPikachuIntroPage3Loads != sLastPikachuIntroPage3Loads)
    {
        sLastPikachuIntroPage3Loads = gHostOakSpeechPikachuIntroPage3Loads;
        TraceLog(frame, "Oak Pikachu intro page 3 reached");
        RequestSnapshot("oak_pikachu_intro_3");
    }
    if (gHostOakSpeechWelcomeToTheWorldPrints != sLastWelcomePrints)
    {
        sLastWelcomePrints = gHostOakSpeechWelcomeToTheWorldPrints;
        TraceLog(frame, "Oak welcome message printed");
        RequestSnapshot("oak_welcome_message");
    }
    if (gHostOakSpeechThisWorldPrints != sLastThisWorldPrints)
    {
        sLastThisWorldPrints = gHostOakSpeechThisWorldPrints;
        TraceLog(frame, "Oak 'This world' message printed");
        RequestSnapshot("oak_this_world_message");
    }

    if (gHostOakSpeechLastExpandedPlaceholderSource != sLastOakPlaceholderSource
        && gHostOakSpeechLastExpandedPlaceholderSource != NULL)
    {
        sLastOakPlaceholderSource = gHostOakSpeechLastExpandedPlaceholderSource;
        snprintf(buffer, sizeof(buffer), "Oak placeholder -> %s",
                 OakPlaceholderName(gHostOakSpeechLastExpandedPlaceholderSource));
        TraceLog(frame, buffer);
        RequestSnapshot(buffer);
    }

    if (gHostOakSpeechDoNamingScreenCalls != sLastNamingCalls)
    {
        sLastNamingCalls = gHostOakSpeechDoNamingScreenCalls;
        snprintf(buffer, sizeof(buffer), "Oak naming screen calls=%u", sLastNamingCalls);
        TraceLog(frame, buffer);
        RequestSnapshot("oak_naming_screen");
    }

    if (gHostOakSpeechCB2NewGameCalls != sLastCB2NewGameCalls)
    {
        sLastCB2NewGameCalls = gHostOakSpeechCB2NewGameCalls;
        snprintf(buffer, sizeof(buffer), "CB2_NewGame calls=%u", sLastCB2NewGameCalls);
        TraceLog(frame, buffer);
        RequestSnapshot("cb2_new_game");
    }
}

static void TraceFramebufferUniformity(u32 frame)
{
    const u32 *framebuffer = HostRendererGetFramebuffer();
    u32 first = framebuffer[0];
    u32 sameCount = 1;
    int i;

    for (i = 64; i < GBA_SCREEN_WIDTH * GBA_SCREEN_HEIGHT; i += 64)
    {
        if (framebuffer[i] == first)
            sameCount++;
    }

    if (sameCount >= 590)
    {
        if (sUniformFrameRunLength == 0 || sLastUniformColor != first)
        {
            sUniformFrameRunLength = 1;
            sLastUniformColor = first;
        }
        else
        {
            sUniformFrameRunLength++;
        }

        if (sUniformFrameRunLength == 120)
        {
            char buffer[160];

            snprintf(buffer, sizeof(buffer),
                     "framebuffer nearly uniform for 120 frames, color=0x%08X callback2=%s",
                     sLastUniformColor,
                     CallbackName(gMain.callback2));
            TraceLog(frame, buffer);
            RequestSnapshot("uniform_frame_warning");
        }
    }
    else
    {
        sUniformFrameRunLength = 0;
    }
}

static void PlayVBlankHandler(void)
{
    if (gMain.vblankCallback)
        gMain.vblankCallback();
    gMain.vblankCounter2++;
    CopyBufferedValuesToGpuRegs();
    ProcessDma3Requests();
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

/* Not exported in main.h but defined in main.c, set by InitKeys() */
extern u16 gKeyRepeatContinueDelay;

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
    static u32 frame;

    /* 1. Read key state from REG_KEYINPUT (set by SDL PollInput last present) */
    ApplyScriptedInput(frame);
    PlayReadKeys();
    TraceInput(frame);
    TraceMilestones(frame);

    /* 2. Run the same high-level loop order as AgbMain before WaitForVBlank. */
    if (gMain.callback1)
        gMain.callback1();
    if (gMain.callback2)
        gMain.callback2();

    PlayTimeCounter_Update();
    MapMusicMain();

    /* 3. Simulate the wait for the next VBlank by advancing one frame. */
    gMain.intrCheck &= ~INTR_FLAG_VBLANK;
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

    frame++;
}

/* ── Main ──────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    int i;
    const char *scriptPath = NULL;

    if (argc > 1)
        scriptPath = argv[1];

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
    HostNewGameStubReset();
    HostOakSpeechStubReset();

    gMain.callback1 = NULL;
    gMain.callback2 = NULL;
    gMain.vblankCallback = NULL;
    gMain.state = 0;

    HostIntroStubReset();
    HostTitleScreenStubReset();
    mkdir("pfr_play_frames", 0775);
    sTraceFile = fopen("pfr_play_trace.log", "w");
    if (sTraceFile != NULL)
        setvbuf(sTraceFile, NULL, _IOLBF, 0);
    sLastCallback1 = (MainCallback)(uintptr_t)-1;
    sLastCallback2 = (MainCallback)(uintptr_t)-1;
    sLastOakPlaceholderSource = (const u8 *)(uintptr_t)-1;
    sLastUniformColor = 0;
    sUniformFrameRunLength = 0;
    sScriptedInputEnabled = FALSE;
    sScriptedInputRangeCount = 0;
    TraceLog(0, "pfr_play trace started");

    if (scriptPath != NULL)
    {
        char buffer[160];

        if (!LoadScriptedInputFile(scriptPath))
            return 1;
        snprintf(buffer, sizeof(buffer),
                 "loaded scripted input file %s with %u ranges",
                 scriptPath,
                 sScriptedInputRangeCount);
        TraceLog(0, buffer);
    }

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

        if (sSnapshotRequested)
        {
            DumpCurrentFramebuffer(HostDisplayGetFrameCount(), sSnapshotLabel);
            sSnapshotRequested = FALSE;
        }

        TraceFramebufferUniformity(HostDisplayGetFrameCount());
    }

    HostDisplayDestroy();
    if (sTraceFile != NULL)
        fclose(sTraceFile);
    return 0;
}
