/*
 * pfr_game_api.c — Implementation of the public game API for libpfr_game.so
 *
 * Replicates the boot sequence from pfr_play.c but without SDL, display,
 * audio, or tracing. Provides clean API for RL environment.
 */

#include "pfr_game_api.h"
#include "pfr_env.h"

/* Upstream game headers */
#include "global.h"
#include "gba/gba.h"
#include "main.h"
#include "pokemon.h"
#include "event_data.h"
#include "constants/pokemon.h"
#include "constants/flags.h"
#include "global.fieldmap.h"
#include "fieldmap.h"
#include "game_ctx.h"
#include "battle.h"
#include "overworld.h"
#include "quest_log.h"
#include "bg.h"
#include "malloc.h"
#include "text.h"
#include "gpu_regs.h"
#include "dma3.h"
#include "save.h"
#include "sound.h"
#include "help_system.h"
#include "save_failed_screen.h"
#include "new_menu_helpers.h"
#include "play_time.h"
#include "load_save.h"
#include "pokemon_storage_system.h"

/* Host layer headers */
#include "host_memory.h"
#include "host_crt0.h"
#include "host_frame_step.h"
#include "host_renderer.h"
#include "host_savestate.h"

#include <string.h>
#include <stdio.h>
#include <time.h>

/* Forward-declare getenv to avoid stdlib.h vs global.h abs() conflict */
extern char *getenv(const char *);

/* External declarations matching pfr_play.c */
extern u32 gHostIntroStubLoadGameSaveResult;
extern void HostNewGameStubReset(void);
extern void HostOakSpeechStubReset(void);
extern void HostIntroStubReset(void);
extern void HostTitleScreenStubReset(void);
extern void HostNativeSoundInit(void);
extern void HostFlashInit(const char *savePath);
extern void HostPatchBattleScriptPointers(void);
extern void HostPatchEventScriptPointers(void);
extern void HostPatchFieldEffectScriptPointers(void);
extern void HostPatchBattleAIScriptPointers(void);
extern void HostPatchBattleAnimScriptPointers(void);
extern void HostBgRegsInit(void);
extern void HostScriptPtrTabReset(void);
extern void CB2_InitCopyrightScreenAfterBootup(void);
/* IntrDummy is static in main.c — provide our own for the boot sequence */
static void PfrIntrDummy(void) { }
extern u16 gKeyRepeatStartDelay;
extern u16 gKeyRepeatContinueDelay;
/* InitSymbolTable is in pfr_play.c — stub for headless */
static void PfrInitSymbolTable(void) { }
extern void EnableVCountIntrAtLine150(void);
extern void CheckForFlashMemory(void);

/* Interrupt handlers — we provide minimal stubs for headless */
static void HeadlessVBlankHandler(void)
{
    /* VBlank handler for headless/RL operation.
     * Must replicate the critical work from PlayVBlankHandler in pfr_play.c:
     * - vblankCallback: runs the game's per-frame VBlank work (LoadOam,
     *   TransferPlttBuffer, ProcessSpriteCopyRequests, etc.)
     * - CopyBufferedValuesToGpuRegs: flushes SetGpuReg writes from the
     *   double buffer to actual MMIO registers that the renderer reads.
     *   Without this, DISPCNT/BGxCNT/scroll/blend registers are stale.
     * - ProcessDma3Requests: executes queued DMA3 transfers (palette data
     *   to PLTT, tile data to VRAM). Without this, palette RAM is empty
     *   and the renderer produces garbled/white output.
     */
    if (gMain.vblankCallback != NULL)
        gMain.vblankCallback();
    gMain.vblankCounter1++;
    gMain.vblankCounter2++;
    CopyBufferedValuesToGpuRegs();
    ProcessDma3Requests();
    INTR_CHECK |= INTR_FLAG_VBLANK;
}

static void HeadlessHBlankHandler(void)
{
    /* No-op for headless */
}

/* Stub for HostLogSaveStatus — defined in pfr_play.c for the player,
 * but not needed in the headless RL library. */
void HostLogSaveStatus(unsigned char status)
{
    (void)status;
}
/* Stub for HostDisplayGetFrameCount */
unsigned int HostDisplayGetFrameCount(void)
{
    return 0;
}



/* ── Boot ── */

void pfr_game_boot(void)
{
    int i;

    /* 1. Map GBA memory regions (dynamic addresses) */
    HostMemoryInit();

    /* 2. CRT0 init */
    /* Allocate per-instance game state */
    if (!g_ctx) {
        g_ctx = game_ctx_alloc();
    }
    HostCrt0Init();
    REG_KEYINPUT = KEYS_MASK;

    /* Initialize save block pointers. These were previously set by GameCtx
     * initialization. With GameCtx gutted to a stub, we must set them
     * explicitly before any code dereferences them. */
    gSaveBlock2Ptr = &gSaveBlock2;
    gSaveBlock1Ptr = &gSaveBlock1;
    gPokemonStoragePtr = &gPokemonStorage;

    /* 3. GPU register manager */
    InitGpuRegManager();

    /* 4. Interrupt dispatch table */
    for (i = 0; i < 14; i++)
        gIntrTable[i] = PfrIntrDummy;
    gIntrTable[3] = HeadlessHBlankHandler;
    gIntrTable[4] = HeadlessVBlankHandler;

    REG_IME = 1;
    REG_IE |= INTR_FLAG_VBLANK;

    /* 5. Key input */
    InitKeys();

    /* 6. Sound init (sets up SOUND_INFO_PTR etc.) */
    HostNativeSoundInit();

    /* 7. VCount interrupt at line 150 */
    EnableVCountIntrAtLine150();

    /* 8. Flash/save init */
    /* Load .sav if PFR_SAVE_PATH is set, so LoadGameSave() finds valid data */
    {
        const char *save_path = getenv("PFR_SAVE_PATH");
        if (save_path && save_path[0] != '\0') {
            fprintf(stderr, "[PFR-BOOT] loading .sav into flash: %s\n", save_path);
            HostFlashInit(save_path);
        } else {
            HostFlashInit("");
        }
    }
    CheckForFlashMemory();
    HostSavestateInit();

    /* The live game state is stored in the heap-backed GameCtx. Register it
     * as a savestate segment so hot resets restore the actual runtime state
     * instead of only the static/GBA memory regions. */
    {
        extern size_t game_ctx_sizeof(void);
        HostSavestateAddSegment(g_ctx, game_ctx_sizeof());
    }

    /* 9. InitMainCallbacks — replicated from main.c */
    gMain.vblankCounter1 = 0;
    gMain.vblankCounter2 = 0;
    gMain.callback1 = NULL;
    gMain.callback2 = NULL;
    gMain.vblankCallback = NULL;
    gMain.state = 0;
    gSaveBlock2Ptr->encryptionKey = 0;
    gQuestLogPlaybackState = QL_PLAYBACK_STATE_STOPPED;

    /* 10. Subsystem init */
    InitMapMusic();
    ClearDma3Requests();
    ResetBgs();
    InitHeap(gHeap, HEAP_SIZE);
    { extern void InitSpecialVars(void); InitSpecialVars(); }
    SetDefaultFontsPointer();

    gSoftResetDisabled = FALSE;
    gHelpSystemEnabled = FALSE;
    SetNotInSaveFailedScreen();

    HostNewGameStubReset();
    HostOakSpeechStubReset();

    /* 11. Patch script pointers */
    HostScriptPtrTabReset();
    HostPatchBattleScriptPointers();
    HostPatchEventScriptPointers();
    HostPatchFieldEffectScriptPointers();
    HostPatchBattleAIScriptPointers();
    HostPatchBattleAnimScriptPointers();

    HostIntroStubReset();
    HostTitleScreenStubReset();
    PfrInitSymbolTable();

    /* Note: gSaveFileStatus is set by LoadGameSave() in
     * CB2_InitCopyrightScreenAfterBootup. main_menu checks gSaveFileStatus
     * (not gHostIntroStubLoadGameSaveResult) for CONTINUE vs NEW GAME. */

    /* 12. Set initial callback and run to overworld */
    SetMainCallback2(CB2_InitCopyrightScreenAfterBootup);

    /* Run frames to advance through stubs (intro/title/oak/newgame)
     * until we reach the overworld. The stubs fast-forward through
     * splash/menus but still need button input to proceed. */
    {
        int max_boot_frames = 20000;
        int frame;
        for (frame = 0; frame < max_boot_frames; frame++) {
            /* Alternate START and A to advance through title + menus.
             * Title screen needs START, main menu needs A at cursor 0. */
            if (frame % 30 == 0)
                pfr_game_step_frames(START_BUTTON, 1);
            else if (frame % 30 == 12)
                pfr_game_step_frames(A_BUTTON, 1);
            else
                pfr_game_step_frames(0, 1);
            if (gMain.callback1 == CB1_Overworld)
                break;
        }
        if (frame >= max_boot_frames) {
            fprintf(stderr, "pfr_game_boot: FATAL — did not reach overworld "
                    "in %d frames\n", max_boot_frames);
            return;
        }

        /* Let the map fully load */
        pfr_game_step_frames(0, 60);

        /* --- MONITOR: validate game state after boot --- */
        {
            PfrRewardInfo boot_info;
            pfr_game_get_reward_info(&boot_info);
            fprintf(stderr, "[PFR-BOOT] pos=(%d,%d) map=%d.%d party=%u "
                    "levels=%u money=%u badges=%u in_battle=%u (frame=%d)\n",
                    boot_info.player_x, boot_info.player_y,
                    boot_info.map_group, boot_info.map_num,
                    boot_info.party_count, boot_info.party_level_sum,
                    boot_info.money, boot_info.badges, boot_info.in_battle,
                    frame);
            if (boot_info.party_count == 0) {
                fprintf(stderr, "[PFR-BOOT] party EMPTY — auto-injecting Charmander lv30\n");
                {
                    extern struct Pokemon gPlayerParty[];
                    extern u8 gPlayerPartyCount;
                    extern void CreateMon(struct Pokemon *mon, u16 species, u8 level,
                                          u8 fixedIV, u8 hasFixedPersonality,
                                          u32 fixedPersonality, u8 otIdType, u32 fixedOtId);
                    extern u8 CalculatePlayerPartyCount(void);

                    /* Direct party injection - no script engine needed */
                    CreateMon(&gPlayerParty[0], 4, 30, 32, 0, 0, 0, 0);
                    /* species=4 (CHARMANDER), level=30, fixedIV=32 (random) */
                    CalculatePlayerPartyCount();
                    /* Sync save block party count too */
                    gSaveBlock1Ptr->playerPartyCount = gPlayerPartyCount;

                    pfr_game_get_reward_info(&boot_info);
                    fprintf(stderr, "[PFR-BOOT] after inject: party=%u levels=%u count=%u\n",
                            boot_info.party_count, boot_info.party_level_sum,
                            gPlayerPartyCount);
                }

                /* Set game flags to bypass Oak's Route 1 cutscene.
                 * Without these, walking north triggers "don't go in grass!"
                 * event that blocks Route 1 access entirely. */
                {
                    extern u8 FlagSet(u16 id);
                    extern bool8 VarSet(u16 id, u16 value);

                    /* VAR_MAP_SCENE_PALLET_TOWN_OAK = 1: disables coord triggers */
                    VarSet(0x4050, 1);

                    /* FLAG_SYS_POKEMON_GET: game knows player has a Pokemon */
                    FlagSet(0x828);

                    /* FLAG_SYS_POKEDEX_GET: enables Pokedex */
                    FlagSet(0x829);

                    /* FLAG_HIDE_OAK_IN_PALLET_TOWN: hides Oak sprite */
                    FlagSet(0x02C);

                    /* FLAG_SYS_B_DASH: enables running shoes */
                    FlagSet(0x82F);

                    /* Viridian City: bypass old man road block */
                    VarSet(0x4051, 2);   /* VAR_MAP_SCENE_VIRIDIAN_CITY_OLD_MAN */

                    /* Permanent repel: suppress wild encounters so agents
                     * spend time exploring, not battling.  0x4020 is
                     * VAR_REPEL_STEP_COUNT; 0xFFFF = ~65535 steps. */
                    VarSet(0x4020, 0xFFFF);

                    /* Pewter City: bypass gym guide + running shoes aide cutscenes */
                    VarSet(0x406C, 2);   /* VAR_MAP_SCENE_PEWTER_CITY */

                    /* Set last-heal location to Pewter City so white-out
                     * respawns near Route 3, not in Pallet Town.
                     * HEAL_LOCATION_PEWTER_CITY = 3. */
                    {
                        extern void SetLastHealLocationWarp(u8 healLocationId);
                        SetLastHealLocationWarp(3);
                    }

                    fprintf(stderr, "[PFR-BOOT] Game flags set: Oak bypassed, running enabled, repel active\n");
                }
            }
            if (boot_info.money > 999999) {
                fprintf(stderr, "[PFR-BOOT] WARNING: corrupt money=%u after boot!\n",
                        boot_info.money);
            }
        }

        /* Warp agent out of the house to Pallet Town.
         * The bedroom is a random-walk trap — warp tiles need specific
         * positioning that random agents rarely achieve. Use the game's
         * warp API to teleport to Pallet Town overworld directly. */
        {
            extern void SetWarpDestination(s8 mapGroup, s8 mapNum,
                                           s8 warpId, s8 x, s8 y);
            extern void WarpIntoMap(void);
            extern void SetMainCallback2(MainCallback callback);
            extern void CB2_LoadMap(void);

            PfrRewardInfo walk_info;
            pfr_game_get_reward_info(&walk_info);

            if (walk_info.map_group == 4) {
                int warp_frame;

                /* Multi-spawn: distribute instances across 4 map positions
                 * to maximize exploration coverage. Each SO-copy has unique
                 * ASLR addresses, so (uintptr_t)&walk_info varies per instance. */
                {
                    static const struct { s8 mg; s8 mn; s8 x; s8 y; } spawns[] = {
                        { 3, 21, 10, 10 },  { 3, 21, 43,  5 },  { 3, 21, 68,  0 },
                        { 3, 22, 43,  2 },  { 3, 22, 90,  2 },
                        { 3,  2, 20, 10 },  { 3,  2, 22, 33 },
                        { 3, 20,  5,  3 },  { 3, 20, 18,  5 },  { 3, 20,  5, 62 },
                        { 3, 19, 10,  3 },  { 3, 19, 10, 22 },
                        { 3,  3, 20, 33 },
                        { 1,  0, 20,  5 },  { 1,  0, 35,  5 },  { 1,  0, 10, 30 },
                        { 1,  0, 15, 55 },  { 1,  0, 25, 60 },
                        { 1,  1,  8,  5 },  { 1,  1, 30,  5 },  { 1,  1, 30, 15 },
                        { 1,  1, 15, 25 },
                        { 3, 43,  8,  5 },  { 3, 43, 10, 20 },
                        { 3, 44, 10,  5 },  { 3, 44, 40,  5 },
                        { 1,  3, 10,  5 },  { 1,  3, 10, 30 },
                        { 3,  1, 20, 20 },  { 3,  1, 20,  5 },
                        { 1,  2, 15,  8 },  { 1,  2, 35, 20 },
                        { 3,  0, 10, 10 },
                    };
                    struct timespec _ts;
                    clock_gettime(CLOCK_MONOTONIC, &_ts);
                    int spawn_idx = (int)(((unsigned long)_ts.tv_nsec * 2654435761UL) % 32);
                    fprintf(stderr, "[PFR-BOOT] Warping spawn %d: (%d,%d) map(%d,%d)...\n",
                            spawn_idx,
                            spawns[spawn_idx].x, spawns[spawn_idx].y,
                            spawns[spawn_idx].mg, spawns[spawn_idx].mn);
                    SetWarpDestination(spawns[spawn_idx].mg, spawns[spawn_idx].mn,
                                       -1, spawns[spawn_idx].x, spawns[spawn_idx].y);
                }

                /* Replicate the game's own warp flow from
                 * field_fadetransition.c:  WarpIntoMap + CB2_LoadMap.
                 *
                 * WarpIntoMap: ApplyCurrentWarp (copies warp dest to
                 *   gSaveBlock1Ptr->location) + LoadCurrentMapData +
                 *   SetPlayerCoordsFromWarp.
                 *
                 * CB2_LoadMap triggers the full callback chain:
                 *   CB2_LoadMap → CB2_DoChangeMap → CB2_LoadMap2
                 *   which runs DoMapLoadLoop (14-step init: BGs, tileset,
                 *   ResumeMap, InitObjectEventsLocal, camera, etc.)
                 *   and finally sets CB1_Overworld + CB2_Overworld. */
                WarpIntoMap();
                SetMainCallback2(CB2_LoadMap);

                /* Run frames until the overworld callback chain completes */
                for (warp_frame = 0; warp_frame < 5000; warp_frame++) {
                    pfr_game_step_frames(0, 1);
                    if (gMain.callback1 == CB1_Overworld)
                        break;
                }
                /* A few extra frames for the map to fully settle */
                pfr_game_step_frames(0, 30);

                pfr_game_get_reward_info(&walk_info);
                fprintf(stderr,
                    "[PFR-BOOT] After warp (%d frames): pos=(%d,%d) map=%d.%d\n",
                    warp_frame, walk_info.player_x, walk_info.player_y,
                    walk_info.map_group, walk_info.map_num);
            }
        }


        HostSavestateCaptureHot();
    }
}

/* ── Savestate ── */


/* Re-initialize critical pointers and function tables after savestate restore.
 * Savestates capture the entire .data/.bss segments, which means all function
 * pointers get overwritten with values from the original executable. We must
 * re-patch anything that contains function pointers or addresses that differ
 * between the exe and the shared library. */
static void PostSavestateRestore(void)
{
    int i;

//     fprintf(stderr, "[PSR] 1: data ptrs\n");
    /* Data pointers */

//     fprintf(stderr, "[PSR] 2: intr table\n");
    /* Interrupt dispatch table */
    for (i = 0; i < 14; i++)
        gIntrTable[i] = PfrIntrDummy;
    gIntrTable[3] = HeadlessHBlankHandler;
    gIntrTable[4] = HeadlessVBlankHandler;

//     fprintf(stderr, "[PSR] 3: script ptr tab reset\n");
    HostScriptPtrTabReset();

//     fprintf(stderr, "[PSR] 4: battle scripts\n");
    HostPatchBattleScriptPointers();
//     fprintf(stderr, "[PSR] 5: event scripts\n");
    HostPatchEventScriptPointers();
//     fprintf(stderr, "[PSR] 6: field effect scripts\n");
    HostPatchFieldEffectScriptPointers();
//     fprintf(stderr, "[PSR] 7: battle AI scripts\n");
    HostPatchBattleAIScriptPointers();
//     fprintf(stderr, "[PSR] 8: battle anim scripts\n");
    HostPatchBattleAnimScriptPointers();

//     fprintf(stderr, "[PSR] 9: bg regs\n");
    HostBgRegsInit();

//     fprintf(stderr, "[PSR] 10: stwi\n");
    {
        extern struct STWIStatus *gSTWIStatus;
        gSTWIStatus = NULL;
    }
//     fprintf(stderr, "[PSR] done\n");
}

int pfr_game_load_state(const char *path)
{
    if (path == NULL || path[0] == '\0')
        return -1;

    if (!HostSavestateLoadFromFile(path))
    {
        fprintf(stderr, "pfr_game: failed to load state: %s\n",
                HostSavestateGetLastError());
        return -1;
    }

    PostSavestateRestore();
    return 0;
}

int pfr_game_save_state(const char *path)
{
    extern bool8 HostSavestateSaveToFile(const char *path);
    if (path == NULL || path[0] == '\0')
        return -1;
    if (!HostSavestateSaveToFile(path))
    {
        fprintf(stderr, "pfr_game: failed to save state\n");
        return -1;
    }
    return 0;
}

void pfr_game_save_hot(void)
{
    HostSavestateCaptureHot();
}

void pfr_game_restore_hot(void)
{
    HostSavestateRestoreHot();
    PostSavestateRestore();
}

/* ── Frame stepping ── */

static void HeadlessFrameLogic(void *userdata)
{
    /* Inline ReadKeys from main.c (it's static there, can't call externally) */
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
            if (gMain.newKeys & L_BUTTON)
                gMain.newKeys |= A_BUTTON;
            if (gMain.heldKeys & L_BUTTON)
                gMain.heldKeys |= A_BUTTON;
        }

        if (gMain.newKeys & gMain.watchedKeysMask)
            gMain.watchedKeysPressed = TRUE;
    }

    if (gMain.callback1)
        gMain.callback1();
    if (gMain.callback2)
        gMain.callback2();
}

void pfr_game_step_frames(uint16_t keys, int n)
{
    int i;

    /* Clear held-key state so the key reader sees this press as "new".
     * The GBA key reader computes: newKeysRaw = keyInput & ~heldKeysRaw.
     * Without clearing, consecutive calls with the same key are treated
     * as "held" and never "new" — menus/battles only respond to newKeys. */
    gMain.heldKeysRaw = 0;
    gMain.heldKeys = 0;

    /* REG_KEYINPUT is active-low: all bits set = released, clear = pressed */
    REG_KEYINPUT = KEYS_MASK & ~keys;

    for (i = 0; i < n; i++)
    {
        HostFrameStepRunFast(HeadlessFrameLogic, NULL);
    }

    /* Release all buttons */
    REG_KEYINPUT = KEYS_MASK;
}

/* ── Ultra-fast training path ── */

/*
 * HeadlessVBlankHandlerFast — minimal VBlank for training.
 * Only increments vblank counters and sets INTR_CHECK.
 *
 * Skips:
 *   - vblankCallback (LoadOam, ProcessSpriteCopyRequests,
 *     ScanlineEffect_InitHBlankDmaTransfer, FieldUpdateBgTilemapScroll,
 *     TransferPlttBuffer, TransferTilesetAnimsBuffer)
 *   - CopyBufferedValuesToGpuRegs (GPU reg double-buffer flush)
 *   - ProcessDma3Requests (DMA3 palette/tile transfers)
 */
static void HeadlessVBlankHandlerFast(void)
{
    /* Call the game vblankCallback (movement, scrolling, sprites) but
     * skip CopyBufferedValuesToGpuRegs and ProcessDma3Requests (rendering). */
    if (gMain.vblankCallback != NULL)
        gMain.vblankCallback();
    gMain.vblankCounter1++;
    gMain.vblankCounter2++;
    INTR_CHECK |= INTR_FLAG_VBLANK;
}

/*
 * HeadlessFrameLogicFast — game callbacks with vblankCallback suppressed.
 *
 * The game's callback1/callback2 ARE the game logic — state machines,
 * script engine, battle logic, movement, event triggers. These must run.
 *
 * However, within callback2, the game may install a vblankCallback that
 * does rendering work (e.g., VBlankCB_Field). We suppress it by saving
 * and nulling it before callbacks, then restoring after.
 */
static void HeadlessFrameLogicFast(void)
{
    /* Inline ReadKeys (same as HeadlessFrameLogic) */
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
            if (gMain.newKeys & L_BUTTON)
                gMain.newKeys |= A_BUTTON;
            if (gMain.heldKeys & L_BUTTON)
                gMain.heldKeys |= A_BUTTON;
        }

        if (gMain.newKeys & gMain.watchedKeysMask)
            gMain.watchedKeysPressed = TRUE;
    }

    if (gMain.callback1)
        gMain.callback1();
    if (gMain.callback2)
        gMain.callback2();
}

void pfr_game_step_frames_fast(uint16_t keys, int n)
{
    int i;
    IntrFunc savedVBlankHandler;

    /* REG_KEYINPUT is active-low: all bits set = released, clear = pressed */
    REG_KEYINPUT = KEYS_MASK & ~keys;

    /* Swap the VBlank interrupt handler to our ultra-fast version.
     * This avoids the vblankCallback (rendering work) and DMA/GPU flushes. */
    savedVBlankHandler = gIntrTable[4];
    gIntrTable[4] = HeadlessVBlankHandlerFast;

    for (i = 0; i < n; i++)
    {
        /* Run game logic directly — no HostFrameStepRunFast overhead.
         * Skip timer sync, interrupt dispatch, DMA triggers entirely.
         * Just: game logic + minimal vblank counter bump. */
        HeadlessFrameLogicFast();
        HeadlessVBlankHandlerFast();
    }

    /* Restore the normal VBlank handler */
    gIntrTable[4] = savedVBlankHandler;

    /* Release all buttons */
    REG_KEYINPUT = KEYS_MASK;
}

/* ── Observation extraction ── */

static int8_t clamp_s8(int16_t v)
{
    if (v > 127) return 127;
    if (v < -128) return -128;
    return (int8_t)v;
}

void pfr_game_extract_obs(unsigned char *buf)
{
    PfrScalarObs *scalar = (PfrScalarObs *)buf;
    PfrNpcObs *npcs = (PfrNpcObs *)(buf + PFR_SCALAR_OBS_SIZE);
    unsigned char *tiles = buf + PFR_SCALAR_OBS_SIZE + PFR_NPC_OBS_SIZE;
    int i, npc_idx;
    int16_t px, py, map_x, map_y;
    int dy, dx, idx;

    memset(buf, 0, PFR_OBS_SIZE);

    /* --- MONITOR: guard against null pointers --- */
    if (!gSaveBlock1Ptr) {
        fprintf(stderr, "[PFR-MONITOR] FATAL: null pointer in extract_obs "
                "(gSaveBlock1Ptr=%p, g_ctx=%p)\n",
                (void*)gSaveBlock1Ptr, (void*)g_ctx);
        return;  /* buf remains zeroed */
    }

    /* ── Scalars ── */
    scalar->player_x = gSaveBlock1Ptr->pos.x;
    scalar->player_y = gSaveBlock1Ptr->pos.y;

    scalar->map_group = gSaveBlock1Ptr->location.mapGroup;
    scalar->map_num = gSaveBlock1Ptr->location.mapNum;
    scalar->map_layout_id = (uint8_t)(gSaveBlock1Ptr->location.mapGroup);

    {
        struct ObjectEvent *playerObj = &gObjectEvents[gPlayerAvatar.objectEventId];
        scalar->player_direction = playerObj->facingDirection & 0x0F;
    }
    scalar->player_avatar_flags = gPlayerAvatar.flags;
    scalar->player_running_state = gPlayerAvatar.runningState;
    scalar->player_transition_state = gPlayerAvatar.tileTransitionState;

    scalar->in_battle = gMain.inBattle;
    scalar->battle_outcome = gBattleOutcome;

    /* Party */
    {
        uint8_t partyCount = gSaveBlock1Ptr->playerPartyCount;
        for (i = 0; i < PFR_MAX_PARTY; i++)
        {
            if (i < partyCount)
            {
                struct Pokemon *mon = &gPlayerParty[i];
                scalar->party[i].species = (uint16_t)GetMonData(mon, MON_DATA_SPECIES);
                scalar->party[i].level = (uint8_t)GetMonData(mon, MON_DATA_LEVEL);
                {
                    uint16_t hp = (uint16_t)GetMonData(mon, MON_DATA_HP);
                    uint16_t maxHP = (uint16_t)GetMonData(mon, MON_DATA_MAX_HP);
                    scalar->party[i].hp_pct = (maxHP > 0) ? (uint8_t)(hp * 255u / maxHP) : 0;
                }
                scalar->party[i].status = (uint8_t)(GetMonData(mon, MON_DATA_STATUS) & 0xFF);
                scalar->party[i].type1 = 0;
            }
        }
    }

    /* Badges */
    {
        uint8_t badges = 0;
        for (i = 0; i < PFR_NUM_BADGES; i++)
        {
            if (FlagGet(FLAG_BADGE01_GET + i))
                badges |= (1u << i);
        }
        scalar->badges = badges;
    }

    /* Money — must decrypt via GetMoney() (XOR with encryptionKey) */
    {
        extern u32 GetMoney(u32 *moneyPtr);
        uint32_t money = GetMoney(&gSaveBlock1Ptr->money);
        scalar->money = (money > 65535) ? 65535 : (uint16_t)money;
    }

    /* Weather */
    scalar->weather = gSaveBlock1Ptr->weather;
    scalar->step_counter = 0;

    /* ── NPCs ── */
    px = gSaveBlock1Ptr->pos.x;
    py = gSaveBlock1Ptr->pos.y;
    npc_idx = 0;

    for (i = 0; i < 16 && npc_idx < PFR_MAX_NPCS; i++)
    {
        struct ObjectEvent *obj = &gObjectEvents[i];
        if (!obj->active || obj->isPlayer)
            continue;

        npcs[npc_idx].dx = clamp_s8(obj->currentCoords.x - px);
        npcs[npc_idx].dy = clamp_s8(obj->currentCoords.y - py);
        npcs[npc_idx].graphics_id = obj->graphicsId;
        npcs[npc_idx].direction = obj->facingDirection & 0x0F;
        npcs[npc_idx].active = 1;
        npcs[npc_idx].movement_type = obj->movementType;
        npc_idx++;
    }

    /* ── Tile grid (9x9 metatile behaviors) ── */
    map_x = px + MAP_OFFSET;
    map_y = py + MAP_OFFSET;

    for (dy = -PFR_TILE_RADIUS; dy <= PFR_TILE_RADIUS; dy++)
    {
        for (dx = -PFR_TILE_RADIUS; dx <= PFR_TILE_RADIUS; dx++)
        {
            idx = (dy + PFR_TILE_RADIUS) * PFR_TILE_DIM + (dx + PFR_TILE_RADIUS);
            {
                uint32_t behavior = MapGridGetMetatileBehaviorAt(map_x + dx, map_y + dy);
                tiles[idx] = (unsigned char)(behavior & 0xFF);
            }
        }
    }
}

/* ── Exact rendering ── */

static int g_renderer_initialized = 0;

static void EnsureRendererInit(void)
{
    if (!g_renderer_initialized) {
        HostRendererInit();
        g_renderer_initialized = 1;
    }
}

void pfr_game_step_frames_exact(uint16_t keys, int n)
{
    int i;

    EnsureRendererInit();

    /* REG_KEYINPUT is active-low: all bits set = released, clear = pressed */
    REG_KEYINPUT = KEYS_MASK & ~keys;

    for (i = 0; i < n; i++)
    {
        HostFrameStepRun(HeadlessFrameLogic, NULL, /*renderFrame=*/TRUE);
    }

    /* Release all buttons */
    REG_KEYINPUT = KEYS_MASK;
}

const uint32_t *pfr_game_get_framebuffer(void)
{
    if (!g_renderer_initialized)
        return NULL;
    return HostRendererGetFramebuffer();
}

void pfr_game_copy_framebuffer(uint32_t *dst, int stride_pixels)
{
    const uint32_t *src;

    if (!g_renderer_initialized)
        return;

    src = HostRendererGetFramebuffer();
    if (!src || !dst)
        return;

    if (stride_pixels == GBA_SCREEN_WIDTH) {
        memcpy(dst, src, GBA_SCREEN_WIDTH * GBA_SCREEN_HEIGHT * sizeof(uint32_t));
    } else {
        int y;
        for (y = 0; y < GBA_SCREEN_HEIGHT; y++) {
            memcpy(dst + y * stride_pixels,
                   src + y * GBA_SCREEN_WIDTH,
                   GBA_SCREEN_WIDTH * sizeof(uint32_t));
        }
    }
}

void pfr_game_render_current_frame(void)
{
    EnsureRendererInit();
    HostRendererRenderFrame();
}

/* ── Reward info ── */

void pfr_game_get_reward_info(PfrRewardInfo *info)
{
    int i;

    memset(info, 0, sizeof(*info));

    /* --- MONITOR: guard against null pointer dereference --- */
    if (!gSaveBlock1Ptr) {
        fprintf(stderr, "[PFR-MONITOR] FATAL: gSaveBlock1Ptr is NULL in get_reward_info!\n");
        return;
    }

    info->player_x = gSaveBlock1Ptr->pos.x;
    info->player_y = gSaveBlock1Ptr->pos.y;
    info->map_group = gSaveBlock1Ptr->location.mapGroup;
    info->map_num = gSaveBlock1Ptr->location.mapNum;

    /* Badges */
    {
        uint8_t badges = 0;
        for (i = 0; i < 8; i++)
        {
            if (FlagGet(FLAG_BADGE01_GET + i))
                badges |= (1u << i);
        }
        info->badges = badges;
    }

    /* Party */
    info->party_count = gSaveBlock1Ptr->playerPartyCount;
    if (info->party_count > 6) {
        fprintf(stderr, "[PFR-MONITOR] CORRUPT party_count=%u, clamping to 0\n",
                info->party_count);
        info->party_count = 0;
    }
    {
        uint16_t level_sum = 0;
        for (i = 0; i < info->party_count && i < 6; i++)
        {
            uint16_t lvl = (uint16_t)GetMonData(&gPlayerParty[i], MON_DATA_LEVEL);
            if (lvl > 100) {
                fprintf(stderr, "[PFR-MONITOR] CORRUPT level=%u for party[%d]\n", lvl, i);
                lvl = 0;
            }
            level_sum += lvl;
        }
        info->party_level_sum = level_sum;
    }

    /* Money is XOR-encrypted with gSaveBlock2Ptr->encryptionKey.
     * Must use GetMoney() to decrypt, NOT read .money directly. */
    {
        extern u32 GetMoney(u32 *moneyPtr);
        info->money = GetMoney(&gSaveBlock1Ptr->money);
    }
    if (info->money > 999999) {
        fprintf(stderr, "[PFR-MONITOR] SUSPICIOUS money=%u (decrypted)\n",
                info->money);
    }
    info->in_battle = gMain.inBattle;
}

void pfr_game_randomize_spawn(void) {
    extern void SetWarpDestination(s8 mapGroup, s8 mapNum, s8 warpId, s8 x, s8 y);
    extern void WarpIntoMap(void);
    extern void SetMainCallback2(MainCallback callback);
    extern void CB2_LoadMap(void);

    static int initialized = 0;
    #define N_SPAWNS 32
    static char paths[N_SPAWNS][256];

    static const struct { s8 mg; s8 mn; s8 x; s8 y; } spawns[] = {
        /* Route 3 (group 3, map 21, 84x20) */
        { 3, 21, 10, 10 },  /* Route 3 west */
        { 3, 21, 43,  5 },  /* Route 3 middle */
        { 3, 21, 68,  0 },  /* Route 3 east */
        /* Route 4 (group 3, map 22, 108x20) */
        { 3, 22, 43,  2 },  /* Route 4 middle */
        { 3, 22, 90,  2 },  /* Route 4 east */
        /* Pewter City (group 3, map 2, 48x40) */
        { 3,  2, 20, 10 },  /* Pewter center */
        { 3,  2, 22, 33 },  /* Pewter south */
        /* Route 2 (group 3, map 20, 24x80) */
        { 3, 20,  5,  3 },  /* Route 2 NW */
        { 3, 20, 18,  5 },  /* Route 2 NE */
        { 3, 20,  5, 62 },  /* Route 2 south */
        /* Route 1 (group 3, map 19, 24x40) */
        { 3, 19, 10,  3 },  /* Route 1 north */
        { 3, 19, 10, 22 },  /* Route 1 mid */
        /* Cerulean City (group 3, map 3, 48x40) */
        { 3,  3, 20, 33 },  /* Cerulean south */
        /* Viridian Forest (group 1, map 0, 54x69) */
        { 1,  0, 20,  5 },  /* VF north */
        { 1,  0, 35,  5 },  /* VF NE */
        { 1,  0, 10, 30 },  /* VF mid */
        { 1,  0, 15, 55 },  /* VF south */
        { 1,  0, 25, 60 },  /* VF SE */
        /* Mt Moon 1F (group 1, map 1, 48x40) */
        { 1,  1,  8,  5 },  /* MM1F NW */
        { 1,  1, 30,  5 },  /* MM1F NE */
        { 1,  1, 30, 15 },  /* MM1F mid-E */
        { 1,  1, 15, 25 },  /* MM1F mid-W */
        /* Route 24 (group 3, map 43, 24x40) */
        { 3, 43,  8,  5 },  /* Route 24 north */
        { 3, 43, 10, 20 },  /* Route 24 mid */
        /* Route 25 (group 3, map 44, 72x20) */
        { 3, 44, 10,  5 },  /* Route 25 west */
        { 3, 44, 40,  5 },  /* Route 25 east */
        /* Mt Moon B2F (group 1, map 3, 48x40) */
        { 1,  3, 10,  5 },  /* MMB2F north */
        { 1,  3, 10, 30 },  /* MMB2F south */
        /* Viridian City (group 3, map 1, 48x40) */
        { 3,  1, 20, 20 },  /* Viridian mid */
        /* Mt Moon B1F (group 1, map 2, 49x40) */
        { 1,  2, 15,  8 },  /* MMB1F NW */
        { 1,  2, 35, 20 },  /* MMB1F mid-E */
    };
    static const int n_spawns = N_SPAWNS;

    if (!initialized) {
        /* First call: create one savestate file per spawn point.
         * Each starts from a clean restore_hot, warps, stabilizes, saves.
         * File paths use address of 'initialized' as per-instance unique ID
         * (each dlopen'd SO copy has its own .data segment). */
        int i;
        for (i = 0; i < n_spawns; i++) {
            pfr_game_restore_hot();

            SetWarpDestination(spawns[i].mg, spawns[i].mn,
                               -1, spawns[i].x, spawns[i].y);
            WarpIntoMap();
            SetMainCallback2(CB2_LoadMap);

            /* Step one frame to kick CB2_LoadMap, then wait for overworld */
            pfr_game_step_frames(0, 1);
            {
                int f;
                for (f = 0; f < 5000; f++) {
                    pfr_game_step_frames(0, 1);
                    if (gMain.callback1 == CB1_Overworld)
                        break;
                }
            }
            pfr_game_step_frames(0, 30);

            snprintf(paths[i], sizeof(paths[i]),
                     "/tmp/pfr_spawn_%lx_%d.sav",
                     (unsigned long)(uintptr_t)&initialized, i);
            pfr_game_save_state(paths[i]);
            fprintf(stderr, "[PFR-SPAWN] Saved spawn %d to %s\n", i, paths[i]);
        }
        initialized = 1;
    }

    /* Pick random spawn and load complete savestate (no live warping) */
    {
        struct timespec _ts;
        clock_gettime(CLOCK_MONOTONIC, &_ts);
        int idx = (int)(((unsigned long)_ts.tv_nsec * 2654435761UL) % (unsigned long)n_spawns);
        pfr_game_load_state(paths[idx]);
    }
}
