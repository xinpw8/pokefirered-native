#define _GNU_SOURCE

#include <stdint.h>
#include <dlfcn.h>
#include <execinfo.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "global.h"
#include "bg.h"
#include "dma3.h"
#include "gba/m4a_internal.h"
#include "gpu_regs.h"
#include "help_system.h"
#include "host_crt0.h"
#include "host_flash.h"
#include "host_frame_step.h"
#include "host_intro_stubs.h"
#include "host_memory.h"
#include "host_pointer_codec.h"
#include "host_new_game_stubs.h"
#include "host_oak_speech_stubs.h"
#include "host_renderer.h"
#include "host_savestate.h"
#include "host_sound_init.h"
#include "host_title_screen_stubs.h"
#include "intro.h"
#include "load_save.h"
#include "m4a.h"
#include "main.h"
#include "malloc.h"
#include "new_game.h"
#include "new_menu_helpers.h"
#include "overworld.h"
#include "play_time.h"
#include "quest_log.h"
#include "random.h"
#include "save.h"
#include "save_failed_screen.h"
#include "sound.h"
#include "task.h"
#include "window.h"

#include "pfr_rl_core.h"

void HostPatchBattleScriptPointers(void);
void HostPatchEventScriptPointers(void);
void HostPatchFieldEffectScriptPointers(void);
void HostPatchBattleAIScriptPointers(void);
void HostPatchBattleAnimScriptPointers(void);
void EnableVCountIntrAtLine150(void);

#define PFR_RL_MAX_BOOT_FRAMES 20000u
#define PFR_RL_SETTLE_FRAMES 60u

enum BootMode
{
    BOOT_MODE_NONE = 0,
    BOOT_MODE_CONTINUE,
};

struct FrameContext
{
    struct PfrRlCore *core;
};

struct SymEntry
{
    uintptr_t addr;
    const char *name;
};

static struct SymEntry *sSymTable;
static int sSymCount;
static bool8 sSymbolsInitialized;
static bool8 sCrashHandlerInstalled;

extern void *__libc_malloc(size_t);
extern void *__libc_realloc(void *, size_t);
extern void __libc_free(void *);

static void InitSymbolTable(void);

static void PfrRlCrashHandler(int sig)
{
    void *frames[64];
    int count = backtrace(frames, ARRAY_COUNT(frames));

    fprintf(stderr, "\n=== PFR RL CRASH: signal %d ===\n", sig);
    backtrace_symbols_fd(frames, count, fileno(stderr));
    _exit(128 + sig);
}

static void InstallCrashHandlerIfRequested(void)
{
    const char *enabled = getenv("PFR_RL_DEBUG_CRASH");

    if (sCrashHandlerInstalled)
        return;
    if (enabled == NULL || enabled[0] == '\0' || enabled[0] == '0')
        return;

    signal(SIGSEGV, PfrRlCrashHandler);
    signal(SIGABRT, PfrRlCrashHandler);
    signal(SIGBUS, PfrRlCrashHandler);
    sCrashHandlerInstalled = TRUE;
}

static char *sys_strdup(const char *s)
{
    size_t len = strlen(s) + 1;
    char *d = __libc_malloc(len);
    if (d != NULL)
        memcpy(d, s, len);
    return d;
}

static int SymEntryCmp(const void *a, const void *b)
{
    uintptr_t aa = ((const struct SymEntry *)a)->addr;
    uintptr_t bb = ((const struct SymEntry *)b)->addr;
    return (aa > bb) - (aa < bb);
}

static void ApplyRuntimeSymbolSlide(void)
{
    intptr_t delta = 0;
    int i;

    for (i = 0; i < sSymCount; i++)
    {
        if (strcmp(sSymTable[i].name, "InitSymbolTable") == 0)
        {
            delta = (intptr_t)((uintptr_t)InitSymbolTable - sSymTable[i].addr);
            break;
        }
    }

    if (delta == 0)
    {
        for (i = 0; i < sSymCount; i++)
        {
            if (strcmp(sSymTable[i].name, "PfrRlCoreInit") == 0)
            {
                delta = (intptr_t)((uintptr_t)PfrRlCoreInit - sSymTable[i].addr);
                break;
            }
        }
    }

    if (delta == 0)
        return;

    for (i = 0; i < sSymCount; i++)
        sSymTable[i].addr = (uintptr_t)((intptr_t)sSymTable[i].addr + delta);
}

static void InitSymbolTable(void)
{
    FILE *fp;
    char line[512];
    int capacity = 4096;
    char exePath[1024];
    char cmd[1200];
    ssize_t len;
    Dl_info info;

    if (sSymbolsInitialized)
        return;

    sSymbolsInitialized = TRUE;
    sSymTable = __libc_malloc((size_t)capacity * sizeof(*sSymTable));
    sSymCount = 0;
    if (sSymTable == NULL)
        return;

    if (dladdr((void *)InitSymbolTable, &info) != 0 && info.dli_fname != NULL && info.dli_fname[0] != '\0')
    {
        snprintf(exePath, sizeof(exePath), "%s", info.dli_fname);
    }
    else
    {
        len = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
        if (len <= 0)
            return;
        exePath[len] = '\0';
    }

    snprintf(cmd, sizeof(cmd), "nm -n '%s' 2>/dev/null", exePath);
    fp = popen(cmd, "r");
    if (fp == NULL)
        return;

    while (fgets(line, sizeof(line), fp) != NULL)
    {
        uintptr_t addr;
        char type;
        char name[256];

        if (sscanf(line, "%lx %c %255s", &addr, &type, name) != 3)
            continue;
        if (type != 't' && type != 'T')
            continue;
        if (sSymCount >= capacity)
        {
            capacity *= 2;
            sSymTable = __libc_realloc(sSymTable, (size_t)capacity * sizeof(*sSymTable));
            if (sSymTable == NULL)
            {
                sSymCount = 0;
                pclose(fp);
                return;
            }
        }
        sSymTable[sSymCount].addr = addr;
        sSymTable[sSymCount].name = sys_strdup(name);
        sSymCount++;
    }

    pclose(fp);
    ApplyRuntimeSymbolSlide();
    qsort(sSymTable, (size_t)sSymCount, sizeof(*sSymTable), SymEntryCmp);
}

static const char *CallbackName(MainCallback callback)
{
    static char addrBuf[64];
    uintptr_t target;
    int lo;
    int hi;

    if (callback == NULL)
        return "NULL";

    target = (uintptr_t)callback;
    lo = 0;
    hi = sSymCount - 1;
    while (lo <= hi)
    {
        int mid = (lo + hi) / 2;
        if (sSymTable[mid].addr == target)
            return sSymTable[mid].name;
        if (sSymTable[mid].addr < target)
            lo = mid + 1;
        else
            hi = mid - 1;
    }

    snprintf(addrBuf, sizeof(addrBuf), "0x%lx", (unsigned long)target);
    return addrBuf;
}

static bool8 StringEquals(const char *a, const char *b)
{
    return strcmp(a, b) == 0;
}

static void SetError(struct PfrRlCore *core, const char *message)
{
    snprintf(core->lastError, sizeof(core->lastError), "%s", message);
}

static void SetErrorf(struct PfrRlCore *core, const char *fmt, const char *arg)
{
    snprintf(core->lastError, sizeof(core->lastError), fmt, arg);
}

static void PlayVBlankHandler(void)
{
    if (gMain.vblankCounter1 != NULL)
        (*gMain.vblankCounter1)++;

    if (gMain.vblankCallback != NULL)
        gMain.vblankCallback();

    gMain.vblankCounter2++;

    CopyBufferedValuesToGpuRegs();
    ProcessDma3Requests();
    m4aSoundVSync();
    SoundMain();
    Random();

    INTR_CHECK |= INTR_FLAG_VBLANK;
    gMain.intrCheck |= INTR_FLAG_VBLANK;
}

static void PlayHBlankHandler(void)
{
    if (gMain.hblankCallback != NULL)
        gMain.hblankCallback();
    gMain.intrCheck |= INTR_FLAG_HBLANK;
}

static void IntrDummy(void)
{
}

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

static u16 ComputeAutoPlayContinueInput(struct PfrRlCore *core)
{
    const char *cb2Name = CallbackName(gMain.callback2);

    if (core->bootMode != BOOT_MODE_CONTINUE)
        return 0;

    if (gMain.callback1 == CB1_Overworld && StringEquals(cb2Name, "CB2_Overworld"))
        return 0;

    if (StringEquals(cb2Name, "CB2_TitleScreenRun"))
        return (core->frame % 32u == 0) ? START_BUTTON : 0;

    if (StringEquals(cb2Name, "CB2_InitMainMenu") || StringEquals(cb2Name, "CB2_MainMenu"))
        return (core->frame % 12u == 0) ? A_BUTTON : 0;

    return 0;
}

static void RunnerFrameLogic(void *userdata)
{
    struct FrameContext *ctx = userdata;
    u16 buttons = ctx->core->heldButtons | ComputeAutoPlayContinueInput(ctx->core);

    REG_KEYINPUT = KEYS_MASK & ~buttons;
    PlayReadKeys();

    if (gMain.callback1 != NULL)
        gMain.callback1();
    if (gMain.callback2 != NULL)
        gMain.callback2();

    PlayTimeCounter_Update();
    MapMusicMain();
}

static void StepFrame(struct PfrRlCore *core, bool8 renderFrame)
{
    struct FrameContext ctx = {
        .core = core,
    };

    HostFrameStepRun(RunnerFrameLogic, &ctx, renderFrame);
    core->frame++;
}

static bool8 IsOverworldReady(void)
{
    return gMain.callback1 == CB1_Overworld && StringEquals(CallbackName(gMain.callback2), "CB2_Overworld");
}

static bool8 ResetRuntime(struct PfrRlCore *core)
{
    int i;

    HostMemoryInit();
    HostRendererInit();
    HostCrt0Init();
    REG_KEYINPUT = KEYS_MASK;

    InitGpuRegManager();
    for (i = 0; i < 14; i++)
        gIntrTable[i] = IntrDummy;
    gIntrTable[3] = PlayHBlankHandler;
    gIntrTable[4] = PlayVBlankHandler;

    REG_IME = 1;
    REG_IE |= INTR_FLAG_VBLANK;

    InitKeys();
    HostNativeSoundInit();
    EnableVCountIntrAtLine150();
    HostFlashInit(core->savePath);
    CheckForFlashMemory();
    if (!HostSavestateInit())
    {
        SetError(core, HostSavestateGetLastError());
        return FALSE;
    }

    gMain.vblankCounter1 = 0;
    gMain.vblankCounter2 = 0;
    gMain.callback1 = NULL;
    gMain.callback2 = NULL;
    gMain.vblankCallback = NULL;
    gMain.state = 0;
    gSaveBlock2Ptr = &gSaveBlock2;
    gSaveBlock1Ptr = &gSaveBlock1;
    gPokemonStoragePtr = &gPokemonStorage;
    gSaveBlock2.encryptionKey = 0;
    gQuestLogPlaybackState = QL_PLAYBACK_STATE_STOPPED;

    InitMapMusic();
    ClearDma3Requests();
    ResetBgs();
    InitHeap(gHeap, HEAP_SIZE);
    SetDefaultFontsPointer();

    gSoftResetDisabled = FALSE;
    gHelpSystemEnabled = FALSE;
    SetNotInSaveFailedScreen();

    gHostIntroStubLoadGameSaveResult = SAVE_STATUS_EMPTY;
    HostNewGameStubReset();
    HostOakSpeechStubReset();
    HostTitleScreenStubReset();
    HostIntroStubReset();

    HostPointerCodecReset();
    HostPatchBattleScriptPointers();
    HostPatchEventScriptPointers();
    HostPatchFieldEffectScriptPointers();
    HostPatchBattleAIScriptPointers();
    HostPatchBattleAnimScriptPointers();

    core->frame = 0;
    core->heldButtons = 0;
    core->bootMode = BOOT_MODE_CONTINUE;
    core->bootComplete = FALSE;
    core->hotCaptured = FALSE;
    core->lastError[0] = '\0';
    return TRUE;
}

static bool8 BeginContinueFromSave(struct PfrRlCore *core)
{
    char buffer[64];
    u8 status;

    gMain.inBattle = FALSE;
    ResetMenuAndMonGlobals();
    Save_ResetSaveCounters();
    status = LoadGameSave(SAVE_NORMAL);
    gHostIntroStubLoadGameSaveResult = status;
    if (status == SAVE_STATUS_EMPTY || status == SAVE_STATUS_INVALID)
    {
        SetError(core, "runner save bootstrap requires a persisted battery save");
        return FALSE;
    }
    if (status != SAVE_STATUS_OK && status != SAVE_STATUS_ERROR)
    {
        snprintf(buffer, sizeof(buffer), "runner save bootstrap failed with status=%u", status);
        SetError(core, buffer);
        return FALSE;
    }

    SetPokemonCryStereo(gSaveBlock2Ptr->optionsSound);
    InitHeap(gHeap, HEAP_SIZE);
    SetMainCallback2(CB2_ContinueSavedGame);
    return TRUE;
}

static bool8 CaptureEpisodeHotState(struct PfrRlCore *core)
{
    if (!HostSavestateCaptureHot())
    {
        SetError(core, HostSavestateGetLastError());
        return FALSE;
    }

    core->hotCaptured = TRUE;
    core->frame = 0;
    core->heldButtons = 0;
    return TRUE;
}

static bool8 BootToOverworld(struct PfrRlCore *core)
{
    u32 i;

    if (!BeginContinueFromSave(core))
        return FALSE;

    for (i = 0; i < PFR_RL_MAX_BOOT_FRAMES; i++)
    {
        if (i % 1000u == 0)
        {
            fprintf(stderr,
                    "runner boot frame=%u cb1=%p cb2=%s loadSave=%d\n",
                    core->frame,
                    (void *)gMain.callback1,
                    CallbackName(gMain.callback2),
                    gHostIntroStubLoadGameSaveResult);
        }
        StepFrame(core, FALSE);
        if (IsOverworldReady())
        {
            u32 settle;
            u32 bootFrame = core->frame;

            for (settle = 0; settle < PFR_RL_SETTLE_FRAMES; settle++)
                StepFrame(core, FALSE);
            HostRendererRenderFrame();
            fprintf(stderr,
                    "runner boot complete bootFrame=%u map=%u/%u pos=(%d,%d) settle=%u\n",
                    bootFrame,
                    gSaveBlock1Ptr->location.mapGroup,
                    gSaveBlock1Ptr->location.mapNum,
                    gSaveBlock1Ptr->pos.x,
                    gSaveBlock1Ptr->pos.y,
                    PFR_RL_SETTLE_FRAMES);
            core->bootComplete = TRUE;
            core->bootMode = BOOT_MODE_NONE;
            return CaptureEpisodeHotState(core);
        }
    }

    fprintf(stderr,
            "runner boot timeout frame=%u cb1=%p cb2=%s loadSave=%d\n",
            core->frame,
            (void *)gMain.callback1,
            CallbackName(gMain.callback2),
            gHostIntroStubLoadGameSaveResult);
    SetError(core, "runner boot did not reach overworld; ensure the save path points to a valid persisted save");
    return FALSE;
}

static bool8 LoadInitialState(struct PfrRlCore *core)
{
    if (core->statePath[0] == '\0')
        return BootToOverworld(core);

    if (!HostSavestateLoadFromFile(core->statePath))
    {
        SetError(core, HostSavestateGetLastError());
        return FALSE;
    }

    HostRendererRenderFrame();
    core->bootMode = BOOT_MODE_NONE;
    core->bootComplete = TRUE;
    if (!CaptureEpisodeHotState(core))
        return FALSE;
    fprintf(stderr,
            "runner state loaded path=%s map=%u/%u pos=(%d,%d)\n",
            core->statePath,
            gSaveBlock1Ptr->location.mapGroup,
            gSaveBlock1Ptr->location.mapNum,
            gSaveBlock1Ptr->pos.x,
            gSaveBlock1Ptr->pos.y);
    return TRUE;
}

bool8 PfrRlCoreInit(struct PfrRlCore *core, const char *savePath, const char *statePath)
{
    if (core == NULL || savePath == NULL || savePath[0] == '\0')
        return FALSE;

    InstallCrashHandlerIfRequested();

    memset(core, 0, sizeof(*core));
    snprintf(core->savePath, sizeof(core->savePath), "%s", savePath);
    if (statePath != NULL)
        snprintf(core->statePath, sizeof(core->statePath), "%s", statePath);

    InitSymbolTable();
    if (!ResetRuntime(core))
        return FALSE;
    return LoadInitialState(core);
}

bool8 PfrRlCoreReset(struct PfrRlCore *core)
{
    if (core == NULL)
        return FALSE;
    if (!core->hotCaptured)
    {
        SetError(core, "runner reset requested before a hot state was captured");
        return FALSE;
    }
    if (!HostSavestateRestoreHot())
    {
        SetError(core, HostSavestateGetLastError());
        return FALSE;
    }

    HostRendererRenderFrame();
    core->frame = 0;
    core->heldButtons = 0;
    return TRUE;
}

bool8 PfrRlCoreStep(struct PfrRlCore *core, u16 buttons, u32 frames)
{
    u32 i;

    if (core == NULL)
        return FALSE;

    if (frames == 0)
        frames = 1;
    core->heldButtons = buttons;
    for (i = 0; i < frames; i++)
        StepFrame(core, FALSE);
    core->heldButtons = 0;
    HostRendererRenderFrame();
    return TRUE;
}

void PfrRlCoreFillPacket(const struct PfrRlCore *core, struct PfrRlPacket *packet)
{
    PfrRlCapturePacket(packet,
                       core->frame,
                       core->heldButtons,
                       (gMain.callback1 == CB1_Overworld && StringEquals(CallbackName(gMain.callback2), "CB2_Overworld")),
                       gMain.inBattle);
}

const char *PfrRlCoreLastError(const struct PfrRlCore *core)
{
    return core->lastError;
}
