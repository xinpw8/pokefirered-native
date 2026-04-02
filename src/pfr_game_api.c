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
extern bool8 gHostNoAudio;
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
    /* Audio disabled for headless/web play */
    gHostNoAudio = TRUE;

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
            /* !! DO NOT ADD ANY OF THE FOLLOWING HERE !!
             * - CreateMon / Pokemon injection (CLAUDE.md rule #1)
             * - FlagSet / VarSet to modify game state (CLAUDE.md rule #3)
             * - Permanent repel (CLAUDE.md rule #4)
             * - Running shoes injection (CLAUDE.md rule #5)
             * - SetLastHealLocationWarp (CLAUDE.md rule #7)
             *
             * If a cutscene hangs in headless mode, fix the headless
             * implementation -- do NOT bypass it with flag/var hacks.
             * If the agent needs a Pokemon, provide a savestate that
             * already has one from natural gameplay. */
            if (boot_info.party_count == 0) {
                fprintf(stderr, "[PFR-BOOT] WARNING: party is EMPTY. "
                        "Provide a savestate with a Pokemon from natural gameplay.\n");
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

                /* !! DO NOT ADD RANDOM SPAWN LOGIC HERE !!
                 * All agents MUST start at the SAME deterministic position.
                 * No spawns[] arrays, no clock_gettime randomization, no
                 * spawn_idx. This is a hard rule from the project owner.
                 * See pokefirered-native/CLAUDE.md rule #2. */
                fprintf(stderr, "[PFR-BOOT] Warping to Route 1...\n");

                /* Route 1 = map (3, 19), position (9, 20) */
                SetWarpDestination(3, 19, -1, 9, 20);

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
    /* Re-seat save block pointers — after a cross-build state restore,
     * these point to old addresses.  Fix them to current globals. */
    gSaveBlock2Ptr = &gSaveBlock2;
    gSaveBlock1Ptr = &gSaveBlock1;
    gPokemonStoragePtr = &gPokemonStorage;

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

/* !! DO NOT ADD ANY SPAWN RANDOMIZATION FUNCTION HERE !!
 * All agents MUST start at the SAME deterministic position every episode.
 * No random spawn arrays, no clock_gettime, no spawn_idx, no savestate
 * cycling, no "exploration diversity" warping. This is a hard rule from
 * the project owner. See pokefirered-native/CLAUDE.md rule #2.
 *
 * If you are a Claude instance reading this: the owner has explicitly
 * forbidden this. A previous Claude added randomization here despite
 * being told not to. Do not repeat that mistake. */

/* ── Trigger Surf field effect directly ── */

void pfr_game_trigger_surf(void)
{
    extern bool8 PartyHasMonWithSurf(void);
    extern bool8 IsPlayerFacingSurfableFishableWater(void);
    extern void ScriptContext_SetupScript(const u8 *script);
    extern const u8 EventScript_UseSurf[];

    int badge5 = FlagGet(0x824);
    int hasSurf = PartyHasMonWithSurf();
    int facingWater = IsPlayerFacingSurfableFishableWater();

    fprintf(stderr, "[PFR-SURF] badge5=%d hasSurf=%d facingWater=%d\n",
            badge5, hasSurf, facingWater);

    if (!badge5) {
        fprintf(stderr, "[PFR-SURF] ERROR: no badge 5\n");
        return;
    }
    if (!hasSurf) {
        fprintf(stderr, "[PFR-SURF] ERROR: no party member with Surf\n");
        return;
    }
    if (!facingWater) {
        fprintf(stderr, "[PFR-SURF] ERROR: not facing surfable water\n");
        return;
    }

    /* Directly trigger the Surf script */
    ScriptContext_SetupScript(EventScript_UseSurf);
    fprintf(stderr, "[PFR-SURF] Script triggered!\n");
}

/* ── Trigger Escape Rope field effect directly ── */

void pfr_game_trigger_escape_rope(void)
{
    extern bool8 CanUseEscapeRopeOnCurrMap(void);
    extern void StartEscapeRopeFieldEffect(void);

    int canUse = CanUseEscapeRopeOnCurrMap();

    fprintf(stderr, "[PFR-ESCAPE] canUse=%d\n", canUse);

    if (!canUse) {
        fprintf(stderr, "[PFR-ESCAPE] ERROR: cannot use Escape Rope on this map\n");
        return;
    }

    StartEscapeRopeFieldEffect();
    fprintf(stderr, "[PFR-ESCAPE] Escape Rope triggered!\n");
}

/* ── Warp player to map coordinates ── */

void pfr_game_warp_to(int mapGroup, int mapNum, int x, int y)
{
    extern void SetWarpDestination(s8 mapGroup, s8 mapNum, s8 warpId, s8 x, s8 y);
    extern void WarpIntoMap(void);
    extern void CB2_LoadMap(void);
    extern void SetMainCallback2(void (*callback)(void));

    fprintf(stderr, "[PFR-WARP] Before: map=%d.%d pos=(%d,%d)\n",
            (int)gSaveBlock1Ptr->location.mapGroup,
            (int)gSaveBlock1Ptr->location.mapNum,
            (int)gSaveBlock1Ptr->pos.x,
            (int)gSaveBlock1Ptr->pos.y);

    SetWarpDestination((s8)mapGroup, (s8)mapNum, -1, (s8)x, (s8)y);
    WarpIntoMap();
    SetMainCallback2(CB2_LoadMap);

    fprintf(stderr, "[PFR-WARP] After WarpIntoMap+SetCB2: map=%d.%d pos=(%d,%d)\n",
            (int)gSaveBlock1Ptr->location.mapGroup,
            (int)gSaveBlock1Ptr->location.mapNum,
            (int)gSaveBlock1Ptr->pos.x,
            (int)gSaveBlock1Ptr->pos.y);

    /* Step frames to let CB2_LoadMap chain run */
    pfr_game_step_frames(0, 5);

    fprintf(stderr, "[PFR-WARP] After stepping: map=%d.%d pos=(%d,%d)\n",
            (int)gSaveBlock1Ptr->location.mapGroup,
            (int)gSaveBlock1Ptr->location.mapNum,
            (int)gSaveBlock1Ptr->pos.x,
            (int)gSaveBlock1Ptr->pos.y);
}

/* ── Test item/move/badge injection ── */

void pfr_game_inject_test_items(void)
{
    extern bool8 AddBagItem(u16 itemId, u16 count);
    extern void SetMonMoveSlot(struct Pokemon *mon, u16 move, u8 slot);

    /* Bag items */
    AddBagItem(85, 5);     /* ITEM_ESCAPE_ROPE x5 */
    AddBagItem(21, 10);    /* ITEM_HYPER_POTION x10 */
    AddBagItem(19, 5);     /* ITEM_FULL_RESTORE x5 */
    AddBagItem(24, 5);     /* ITEM_REVIVE x5 */
    AddBagItem(84, 10);    /* ITEM_MAX_REPEL x10 */
    AddBagItem(68, 20);    /* ITEM_RARE_CANDY x20 */
    AddBagItem(4, 20);     /* ITEM_POKE_BALL x20 */

    /* HM03 Surf in TM/HM pocket */
    AddBagItem(341, 1);    /* ITEM_HM03 */

    /* Give Surf to party slot 0, replacing move slot 3 */
    if (gPlayerPartyCount > 0) {
        SetMonMoveSlot(&gPlayerParty[0], 57, 3);  /* MOVE_SURF = 57, slot 3 */
    }

    /* Set all 8 badges */
    {
        int i;
        for (i = 0; i < 8; i++)
            FlagSet(0x820 + i);
    }

    fprintf(stderr, "[PFR-INJECT] Test items/moves/badges injected\n");
}

/* ── Full game state as JSON ── */

int pfr_game_get_state_json(char *buf, int bufsize)
{
    int n = 0;
    int i;

    #define JP(...) do { \
        int _w = snprintf(buf + n, bufsize - n, __VA_ARGS__); \
        if (_w > 0) n += _w; \
        if (n >= bufsize - 1) return n; \
    } while(0)

    JP("{");

    /* Position & Map */
    JP("\"pos\":{\"x\":%d,\"y\":%d},",
       (int)gSaveBlock1Ptr->pos.x, (int)gSaveBlock1Ptr->pos.y);
    JP("\"map\":{\"group\":%d,\"num\":%d},",
       (int)gSaveBlock1Ptr->location.mapGroup,
       (int)gSaveBlock1Ptr->location.mapNum);

    /* Player direction & state */
    JP("\"direction\":%d,",
       gObjectEvents[gPlayerAvatar.objectEventId].facingDirection & 0xF);
    JP("\"avatar_flags\":%d,", gPlayerAvatar.flags);
    JP("\"in_battle\":%s,", gMain.inBattle ? "true" : "false");

    /* Money */
    {
        extern u32 GetMoney(u32 *moneyPtr);
        u32 money = GetMoney(&gSaveBlock1Ptr->money);
        JP("\"money\":%u,", (unsigned)money);
    }

    /* Badges */
    {
        int badges = 0;
        for (i = 0; i < 8; i++)
            if (FlagGet(0x820 + i)) badges |= (1 << i);
        JP("\"badges\":%d,\"badge_count\":%d,", badges, __builtin_popcount(badges));
    }

    /* Party */
    JP("\"party_count\":%d,", (int)gPlayerPartyCount);
    JP("\"party\":[");
    for (i = 0; i < 6; i++) {
        struct Pokemon *mon = &gPlayerParty[i];
        u16 species = (u16)GetMonData(mon, MON_DATA_SPECIES);
        if (i > 0) JP(",");
        if (species == 0) { JP("null"); continue; }
        JP("{\"slot\":%d,\"species\":%d,\"level\":%d,",
           i, (int)species, (int)GetMonData(mon, MON_DATA_LEVEL));
        JP("\"hp\":%d,\"max_hp\":%d,",
           (int)GetMonData(mon, MON_DATA_HP),
           (int)GetMonData(mon, MON_DATA_MAX_HP));
        JP("\"atk\":%d,\"def\":%d,\"speed\":%d,\"spatk\":%d,\"spdef\":%d,",
           (int)GetMonData(mon, MON_DATA_ATK),
           (int)GetMonData(mon, MON_DATA_DEF),
           (int)GetMonData(mon, MON_DATA_SPEED),
           (int)GetMonData(mon, MON_DATA_SPATK),
           (int)GetMonData(mon, MON_DATA_SPDEF));
        JP("\"status\":%u,", (unsigned)GetMonData(mon, MON_DATA_STATUS));
        JP("\"moves\":[%d,%d,%d,%d],",
           (int)GetMonData(mon, MON_DATA_MOVE1),
           (int)GetMonData(mon, MON_DATA_MOVE2),
           (int)GetMonData(mon, MON_DATA_MOVE3),
           (int)GetMonData(mon, MON_DATA_MOVE4));
        JP("\"pp\":[%d,%d,%d,%d]}",
           (int)GetMonData(mon, MON_DATA_PP1),
           (int)GetMonData(mon, MON_DATA_PP2),
           (int)GetMonData(mon, MON_DATA_PP3),
           (int)GetMonData(mon, MON_DATA_PP4));
    }
    JP("],");

    /* Bag - Items pocket */
    JP("\"bag_items\":[");
    { int first = 1;
      for (i = 0; i < BAG_ITEMS_COUNT; i++) {
          u16 id = gSaveBlock1Ptr->bagPocket_Items[i].itemId;
          u16 qty = gSaveBlock1Ptr->bagPocket_Items[i].quantity;
          if (id == 0) continue;
          if (!first) JP(",");
          JP("{\"id\":%d,\"qty\":%d}", (int)id, (int)qty);
          first = 0;
      }
    }
    JP("],");

    /* Bag - Key Items */
    JP("\"bag_key_items\":[");
    { int first = 1;
      for (i = 0; i < BAG_KEYITEMS_COUNT; i++) {
          u16 id = gSaveBlock1Ptr->bagPocket_KeyItems[i].itemId;
          if (id == 0) continue;
          if (!first) JP(",");
          JP("{\"id\":%d}", (int)id);
          first = 0;
      }
    }
    JP("],");

    /* Bag - PokeBalls */
    JP("\"bag_pokeballs\":[");
    { int first = 1;
      for (i = 0; i < BAG_POKEBALLS_COUNT; i++) {
          u16 id = gSaveBlock1Ptr->bagPocket_PokeBalls[i].itemId;
          u16 qty = gSaveBlock1Ptr->bagPocket_PokeBalls[i].quantity;
          if (id == 0) continue;
          if (!first) JP(",");
          JP("{\"id\":%d,\"qty\":%d}", (int)id, (int)qty);
          first = 0;
      }
    }
    JP("],");

    /* Bag - TMs/HMs */
    JP("\"bag_tmhm\":[");
    { int first = 1;
      for (i = 0; i < BAG_TMHM_COUNT; i++) {
          u16 id = gSaveBlock1Ptr->bagPocket_TMHM[i].itemId;
          u16 qty = gSaveBlock1Ptr->bagPocket_TMHM[i].quantity;
          if (id == 0) continue;
          if (!first) JP(",");
          JP("{\"id\":%d,\"qty\":%d}", (int)id, (int)qty);
          first = 0;
      }
    }
    JP("],");

    /* Menu state */
    /* menu_cursor: not easily accessible (static in start_menu.c) */

    /* Dialog/text — gStringVar4 uses GBA text encoding (0xFF = terminator) */
    JP("\"text_raw\":[");
    { int len = 0;
      while (len < 200 && gStringVar4[len] != 0xFF && gStringVar4[len] != 0) len++;
      for (i = 0; i < len; i++) {
          if (i > 0) JP(",");
          JP("%d", (int)gStringVar4[i]);
      }
    }
    JP("],");

    /* Callback addresses for mode detection */
    JP("\"cb1\":\"%p\",", (void*)gMain.callback1);
    JP("\"cb2\":\"%p\",", (void*)gMain.callback2);

    /* Surf debug info */
    {
        extern bool8 PartyHasMonWithSurf(void);
        extern bool8 IsPlayerFacingSurfableFishableWater(void);
        extern u32 MapGridGetMetatileBehaviorAt(s16 x, s16 y);
        extern void PlayerGetDestCoords(s16 *x, s16 *y);
        extern void MoveCoords(u8 dir, s16 *x, s16 *y);
        extern u8 GetPlayerFacingDirection(void);

        int badge5 = FlagGet(0x824);
        int hasSurf = PartyHasMonWithSurf();
        s16 px, py;
        PlayerGetDestCoords(&px, &py);
        MoveCoords(GetPlayerFacingDirection(), &px, &py);
        u32 facingBehavior = MapGridGetMetatileBehaviorAt(px, py);
        int facingWater = IsPlayerFacingSurfableFishableWater();

        JP("\"surf_debug\":{\"badge5\":%d,", badge5);
        JP("\"party_has_surf\":%d,", hasSurf);
        JP("\"facing_xy\":[%d,%d],", (int)px, (int)py);
        JP("\"facing_behavior\":%d,", (int)facingBehavior);
        JP("\"facing_water\":%d}", facingWater);
    }

    JP("}");
    #undef JP
    return n;
}
