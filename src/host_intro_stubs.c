#include "global.h"

#include "bg.h"
#include "gba/m4a_internal.h"
#include "gpu_regs.h"
#include "libgcnmultiboot.h"
#include "link.h"
#include "malloc.h"
#include "menu.h"
#include "new_game.h"
#include "new_menu_helpers.h"
#include "save.h"
#include "sound.h"
#include "task.h"
#include "util.h"
#include "window.h"

void UpstreamResetMenuAndMonGlobals(void);
void UpstreamSav2_ClearSetDefault(void);

u32 gHostIntroStubGameCubeMultiBootInitCalls = 0;
u32 gHostIntroStubGameCubeMultiBootMainCalls = 0;
u32 gHostIntroStubGameCubeMultiBootHandleSerialCalls = 0;
u32 gHostIntroStubGameCubeMultiBootQuitCalls = 0;
u32 gHostIntroStubGameCubeMultiBootExecuteProgramCalls = 0;
/* ResetMenuAndMonGlobals, Sav2_ClearSetDefault now from new_game.c */
/* SetPokemonCryStereo, SerialCB, ResetSerial now upstream */
u32 gHostIntroStubResetTempTileDataBuffersCalls = 0;
u32 gHostIntroStubFreeTempTileDataBuffersCalls = 0;
u32 gHostIntroStubDecompressAndCopyTileDataToVramCalls = 0;
u32 gHostIntroStubResetBgPositionsCalls = 0;
u32 gHostIntroStubStartBlendTaskCalls = 0;
u32 gHostIntroStubIsBlendTaskActiveCalls = 0;
/* m4aSongNumStart, PlaySE, PlayCry_ByMode now upstream */
u8 gHostIntroStubLoadGameSaveResult = SAVE_STATUS_EMPTY;
/* gSaveFileStatus now from save.c */

const u32 gMultiBootProgram_PokemonColosseum_Start[1] = {0};
const u32 gMultiBootProgram_PokemonColosseum_End[1] = {0};
const u32 gMultiBootProgram_EReader_Start[1] = {0};
const u32 gMultiBootProgram_EReader_End[1] = {0};

void HostIntroStubReset(void)
{
    gHostIntroStubGameCubeMultiBootInitCalls = 0;
    gHostIntroStubGameCubeMultiBootMainCalls = 0;
    gHostIntroStubGameCubeMultiBootHandleSerialCalls = 0;
    gHostIntroStubGameCubeMultiBootQuitCalls = 0;
    gHostIntroStubGameCubeMultiBootExecuteProgramCalls = 0;
    /* removed counters for upstream functions */
    gHostIntroStubResetTempTileDataBuffersCalls = 0;
    gHostIntroStubFreeTempTileDataBuffersCalls = 0;
    gHostIntroStubDecompressAndCopyTileDataToVramCalls = 0;
    gHostIntroStubResetBgPositionsCalls = 0;
    gHostIntroStubStartBlendTaskCalls = 0;
    gHostIntroStubIsBlendTaskActiveCalls = 0;
    /* PlaySE, PlayCry_ByMode, SetPokemonCryStereo now from upstream */
    gHostIntroStubLoadGameSaveResult = SAVE_STATUS_EMPTY;
}

void GameCubeMultiBoot_Main(struct GcmbStruct *pStruct)
{
    gHostIntroStubGameCubeMultiBootMainCalls++;
    pStruct->gcmb_field_2 = 0;
}

void GameCubeMultiBoot_ExecuteProgram(struct GcmbStruct *pStruct)
{
    gHostIntroStubGameCubeMultiBootExecuteProgramCalls++;
    (void)pStruct;
}

void GameCubeMultiBoot_Init(struct GcmbStruct *pStruct)
{
    gHostIntroStubGameCubeMultiBootInitCalls++;
    pStruct->gcmb_field_0 = 0;
    pStruct->gcmb_field_2 = 0;
}

void GameCubeMultiBoot_HandleSerialInterrupt(struct GcmbStruct *pStruct)
{
    gHostIntroStubGameCubeMultiBootHandleSerialCalls++;
    (void)pStruct;
}

void GameCubeMultiBoot_Quit(void)
{
    gHostIntroStubGameCubeMultiBootQuitCalls++;
}

/* ResetMenuAndMonGlobals and Sav2_ClearSetDefault: the OBJECT target
   pfr_upstream_new_game compiles new_game.c with these names aliased to
   Upstream* — so these wrappers provide the real names and call through. */
void ResetMenuAndMonGlobals(void) { UpstreamResetMenuAndMonGlobals(); }
void Sav2_ClearSetDefault(void)   { UpstreamSav2_ClearSetDefault(); }

/* SetPokemonCryStereo now from m4a.c */
/* SerialCB, ResetSerial now from link.c */
/* m4aSongNumStart now from m4a.c */
/* PlaySE, PlayCry_ByMode now from sound.c */
/* StoreWordInTwoHalfwords, LoadWordFromTwoHalfwords now from util.c */
