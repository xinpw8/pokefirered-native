/*
 * host_new_game_stubs.c
 *
 * Minimal host-layer stubs for the native build. Only provides symbols that
 * are NOT yet supplied by any upstream .c file compiled into the build.
 *
 * As more upstream files get added to CMakeLists.txt, stubs here should be
 * removed to avoid duplicate-definition link errors.
 */

#include <string.h>

#include "global.h"
#include "overworld.h"

#include "host_new_game_stubs.h"

/* ---- Tracking counters for the smoke test ---- */
u32 gHostNewGameSetWarpDestinationCalls = 0;
u32 gHostNewGameWarpIntoMapCalls = 0;
u32 gHostNewGameRunScriptImmediatelyCalls = 0;
u32 gHostNewGameStopMapMusicCalls = 0;
u32 gHostNewGameResetSafariZoneFlagCalls = 0;
u32 gHostNewGameResetInitialPlayerAvatarStateCalls = 0;
u32 gHostNewGameScriptContextInitCalls = 0;
u32 gHostNewGameUnlockPlayerFieldControlsCalls = 0;
u32 gHostNewGameDoMapLoadLoopCalls = 0;
u32 gHostNewGameSetFieldVBlankCallbackCalls = 0;
const u8 *gHostNewGameLastRunScript = NULL;
struct WarpData gHostNewGameWarpDestination = {0};

/* These stub-only counters/callbacks are kept for test compatibility but
 * are no longer set by real upstream code. The smoke test will need to be
 * migrated to check real game state instead. */

void HostNewGameStubReset(void)
{
    gHostNewGameSetWarpDestinationCalls = 0;
    gHostNewGameWarpIntoMapCalls = 0;
    gHostNewGameRunScriptImmediatelyCalls = 0;
    gHostNewGameStopMapMusicCalls = 0;
    gHostNewGameResetSafariZoneFlagCalls = 0;
    gHostNewGameResetInitialPlayerAvatarStateCalls = 0;
    gHostNewGameScriptContextInitCalls = 0;
    gHostNewGameUnlockPlayerFieldControlsCalls = 0;
    gHostNewGameDoMapLoadLoopCalls = 0;
    gHostNewGameSetFieldVBlankCallbackCalls = 0;
    gHostNewGameLastRunScript = NULL;
}

/* Host overworld callback stubs removed — real implementations in
 * overworld.c (CB1_Overworld, CB2_Overworld) and
 * field_fadetransition.c (FieldCB_WarpExitFadeFromBlack) are now
 * compiled directly from upstream. */
