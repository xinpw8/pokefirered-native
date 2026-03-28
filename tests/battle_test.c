/* Minimal headless battle test — skips Oak Speech, tests Growl directly */
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "global.h"
#include "battle.h"
#include "battle_main.h"
#include "battle_controllers.h"
#include "battle_setup.h"
#include "data.h"
#include "dma3.h"
#include "constants/moves.h"
#include "constants/opponents.h"
#include "constants/songs.h"
#include "game_ctx.h"
#include "gpu_regs.h"
#include "host_agbmain.h"
#include "host_crt0.h"
#include "host_sound_init.h"
#include "host_dma.h"
#include "host_flash.h"
#include "host_intro_stubs.h"
#include "host_oak_speech_stubs.h"
#include "host_memory.h"
#include "host_new_game_stubs.h"
#include "host_runtime_stubs.h"
#include "host_title_screen_stubs.h"
#include "load_save.h"
#include "main.h"
#include "main_menu.h"
#include "malloc.h"
#include "m4a.h"
#include "overworld.h"
#include "fieldmap.h"
#include "palette.h"
#include "pokemon.h"
#include "random.h"
#include "save.h"
#include "sprite.h"
#include "task.h"
#include "bg.h"
#include "scanline_effect.h"

bool32 CheckHeap(void);
void HostPatchBattleScriptPointers(void);
void HostPatchFieldEffectScriptPointers(void);
void HostPatchBattleAIScriptPointers(void);
void HostPatchBattleAnimScriptPointers(void);
void HostScriptPtrTabReset(void);
void InitIntrHandlers(void);
void EnableVCountIntrAtLine150(void);
void HostLogSaveStatus(u8 status) { (void)status; }

extern u16 gBattle_BG0_X;
extern const IntrFunc gIntrTableTemplate[];
extern u32 IntrMain_Buffer[0x200];
extern u8 gPcmDmaCounter;
extern u8 gWirelessCommType;
extern u16 gKeyRepeatContinueDelay;

#include "game_ctx_macros.h"

static void RunAllCallbacksFrame(void)
{
    if (gMain.callback1 != NULL)
        gMain.callback1();
    if (gMain.callback2 != NULL)
        gMain.callback2();
    ProcessDma3Requests();
    if (gMain.vblankCallback != NULL)
        gMain.vblankCallback();
}

static void SetKeys(u16 newKeys, u16 heldKeys)
{
    gMain.newKeys = newKeys;
    gMain.newKeysRaw = newKeys;
    gMain.heldKeys = heldKeys;
    gMain.heldKeysRaw = heldKeys;
}

static void ClearKeys(void) { SetKeys(0, 0); }

static void FullInit(void)
{
    HostScriptPtrTabReset();
    HostMemoryReset();
    HostFlashInit(NULL);
    HostStubReset();
    HostIntroStubReset();
    HostOakSpeechStubReset();
    HostTitleScreenStubReset();
    HostNewGameStubReset();
    ClearDma3Requests();
    InitGpuRegManager();
    HostCrt0Init();
    memset(&gMain, 0, sizeof(gMain));
    ResetBgs();
    InitIntrHandlers();
    InitHeap(gHeap, HEAP_SIZE);
    {
        extern void SetDefaultFontsPointer(void);
        SetDefaultFontsPointer();
    }
    HostNativeSoundInit();
    HostPatchBattleScriptPointers();
    HostPatchFieldEffectScriptPointers();
    HostPatchBattleAIScriptPointers();
    HostPatchBattleAnimScriptPointers();

    /* Minimal game state so battles work */
    SeedRng(42);
    ResetTasks();
    ResetSpriteData();
    FreeAllSpritePalettes();
    gWirelessCommType = 0;
    gPcmDmaCounter = 0;
    /* Undef colliding macros, init save block ptrs, redefine */
    gSaveBlock2Ptr = &gSaveBlock2;
    gSaveBlock1Ptr = &gSaveBlock1;
    gPokemonStoragePtr = &gPokemonStorage;
    gSaveBlock2Ptr->encryptionKey = 0;

    /* Set up player party: Bulbasaur Lv10 with Growl */
    ZeroPlayerPartyMons();
    CreateMon(&gPlayerParty[0], SPECIES_BULBASAUR, 10, 32, TRUE, 0, OT_ID_PLAYER_ID, 0);
    {
        u16 growl = MOVE_GROWL;
        u16 none = MOVE_NONE;
        u8 pp = 40;
        u8 zeroPp = 0;
        u16 hp;
        SetMonData(&gPlayerParty[0], MON_DATA_MOVE1, &growl);
        SetMonData(&gPlayerParty[0], MON_DATA_MOVE2, &none);
        SetMonData(&gPlayerParty[0], MON_DATA_MOVE3, &none);
        SetMonData(&gPlayerParty[0], MON_DATA_MOVE4, &none);
        SetMonData(&gPlayerParty[0], MON_DATA_PP1, &pp);
        SetMonData(&gPlayerParty[0], MON_DATA_PP2, &zeroPp);
        SetMonData(&gPlayerParty[0], MON_DATA_PP3, &zeroPp);
        SetMonData(&gPlayerParty[0], MON_DATA_PP4, &zeroPp);
        CalculateMonStats(&gPlayerParty[0]);
        hp = GetMonData(&gPlayerParty[0], MON_DATA_MAX_HP, NULL);
        SetMonData(&gPlayerParty[0], MON_DATA_HP, &hp);
    }
    gPlayerPartyCount = CalculatePlayerPartyCount();

    /* Set up enemy: Caterpie Lv2 */
    ZeroEnemyPartyMons();
    CreateMon(&gEnemyParty[0], SPECIES_CATERPIE, 2, 0, TRUE, 0, OT_ID_RANDOM_NO_SHINY, 0);
}

static void crash_handler(int sig)
{
    char buf[128];
    int n = snprintf(buf, sizeof(buf), "\n*** CRASH: signal %d ***\n", sig);
    (void)!write(2, buf, n);
    _exit(2);
}

int main(void)
{
    int i;
    int reached_battle = 0;
    int battle_ended = 0;

    signal(SIGSEGV, crash_handler);
    signal(SIGBUS, crash_handler);

    g_ctx = game_ctx_alloc();
    if (!g_ctx) { fprintf(stderr, "FATAL: game_ctx_alloc failed\n"); return 1; }
    HostMemoryInit();

    fprintf(stderr, "== Battle Test: Bulbasaur (Growl) vs Caterpie ==\n");
    FullInit();
    fprintf(stderr, "  Init complete. Player party: %d mon(s)\n", gPlayerPartyCount);

    /* Set up minimal map state so BattleSetup_GetTerrainId doesnt crash.
     * It reads: gMapHeader.mapLayout->primaryTileset->metatileAttributes
     * We provide the full chain of structs with zeroed data. */
    {
        extern struct BackupMapLayout VMap;
        static u16 sDummyGrid[64 * 64];
        static u32 sDummyMetatileAttr[1024];
        static struct Tileset sDummyTileset;
        static struct MapLayout sDummyLayout;

        memset(sDummyGrid, 0, sizeof(sDummyGrid));
        memset(sDummyMetatileAttr, 0, sizeof(sDummyMetatileAttr));
        memset(&sDummyTileset, 0, sizeof(sDummyTileset));
        memset(&sDummyLayout, 0, sizeof(sDummyLayout));

        /* VMap for MapGridGetMetatileIdAt */
        VMap.map = sDummyGrid;
        VMap.Xsize = 64;
        VMap.Ysize = 64;

        /* Tileset with valid metatileAttributes pointer */
        sDummyTileset.metatileAttributes = sDummyMetatileAttr;

        /* MapLayout pointing to our tileset */
        sDummyLayout.width = 32;
        sDummyLayout.height = 32;
        sDummyLayout.map = sDummyGrid;
        sDummyLayout.primaryTileset = &sDummyTileset;
        sDummyLayout.secondaryTileset = &sDummyTileset;

        /* Wire gMapHeader to use our layout */
        gMapHeader.mapLayout = &sDummyLayout;
        gMapHeader.mapType = 3;
    }

    /* Start the battle */
    ClearKeys();
    gBattleTypeFlags = 0;
    gMain.savedCallback = CB2_ReturnToFieldContinueScriptPlayMapMusic;
    SetMainCallback2(CB2_InitBattle);

    /* Phase 1: Run until we reach BattleMainCB2 */
    fprintf(stderr, "  Starting battle init...\n");
    for (i = 0; i < 4096; i++)
    {
        if (gMain.callback2 == BattleMainCB2)
        {
            reached_battle = 1;
            fprintf(stderr, "  Reached BattleMainCB2 at frame %d\n", i);
            break;
        }
        if ((i % 6) == 0)
            SetKeys(A_BUTTON, A_BUTTON);
        else
            ClearKeys();
        RunAllCallbacksFrame();
    }

    if (!reached_battle)
    {
        fprintf(stderr, "FAIL: Did not reach BattleMainCB2 within 4096 frames\n");
        return 1;
    }

    /* Phase 2: Run the battle with A-button pulsing (auto-selects Fight → Growl → A through messages)
     * The agent has only Growl, so Fight→slot1 is the only option.
     * A-button advances through all dialogs.
     */
    fprintf(stderr, "  Running battle frames (A-button pulsing)...\n");
    for (i = 0; i < 16384; i++)
    {
        /* Check if battle ended */
        if (gBattleOutcome != 0 && !gMain.inBattle && gMain.callback2 != BattleMainCB2)
        {
            battle_ended = 1;
            fprintf(stderr, "  Battle ended at frame %d, outcome=%d\n", i, gBattleOutcome);
            break;
        }

        /* Pulse A every 6 frames */
        if ((i % 6) == 0)
            SetKeys(A_BUTTON, A_BUTTON);
        else
            ClearKeys();
        RunAllCallbacksFrame();

        /* Progress report every 2000 frames */
        if (i % 2000 == 0 && i > 0)
            fprintf(stderr, "    frame %d: cb2=%p inBattle=%d outcome=%d\n",
                    i, (void*)gMain.callback2, gMain.inBattle, gBattleOutcome);
    }

    if (!battle_ended)
    {
        fprintf(stderr, "FAIL: Battle did not end within 16384 frames (cb2=%p inBattle=%d outcome=%d)\n",
                (void*)gMain.callback2, gMain.inBattle, gBattleOutcome);
        return 1;
    }

    fprintf(stderr, "PASS: Battle completed successfully\n");
    return 0;
}
