#include "global.h"

#include "bg.h"
#include "gba/m4a_internal.h"
#include "libgcnmultiboot.h"
#include "link.h"
#include "malloc.h"
#include "menu.h"
#include "new_game.h"
#include "new_menu_helpers.h"
#include "save.h"
#include "sound.h"
#include "util.h"
#include "window.h"

void UpstreamResetMenuAndMonGlobals(void);
void UpstreamSav2_ClearSetDefault(void);

u32 gHostIntroStubGameCubeMultiBootInitCalls = 0;
u32 gHostIntroStubGameCubeMultiBootMainCalls = 0;
u32 gHostIntroStubGameCubeMultiBootHandleSerialCalls = 0;
u32 gHostIntroStubGameCubeMultiBootQuitCalls = 0;
u32 gHostIntroStubGameCubeMultiBootExecuteProgramCalls = 0;
u32 gHostIntroStubResetMenuAndMonGlobalsCalls = 0;
u32 gHostIntroStubSaveResetSaveCountersCalls = 0;
u32 gHostIntroStubLoadGameSaveCalls = 0;
u32 gHostIntroStubSav2ClearSetDefaultCalls = 0;
u32 gHostIntroStubSetPokemonCryStereoCalls = 0;
u32 gHostIntroStubSerialCBCalls = 0;
u32 gHostIntroStubResetSerialCalls = 0;
u32 gHostIntroStubResetTempTileDataBuffersCalls = 0;
u32 gHostIntroStubFreeTempTileDataBuffersCalls = 0;
u32 gHostIntroStubDecompressAndCopyTileDataToVramCalls = 0;
u32 gHostIntroStubResetBgPositionsCalls = 0;
u32 gHostIntroStubStartBlendTaskCalls = 0;
u32 gHostIntroStubIsBlendTaskActiveCalls = 0;
u32 gHostIntroStubM4aSongNumStartCalls = 0;
u32 gHostIntroStubPlaySECalls = 0;
u32 gHostIntroStubPlayCryByModeCalls = 0;
u32 gHostIntroStubLastPokemonCryStereoValue = 0;
u8 gHostIntroStubLoadGameSaveResult = SAVE_STATUS_EMPTY;
u16 gSaveFileStatus = SAVE_STATUS_EMPTY;

const u32 gMultiBootProgram_PokemonColosseum_Start[1] = {0};
const u32 gMultiBootProgram_PokemonColosseum_End[1] = {0};

void HostIntroStubReset(void)
{
    gHostIntroStubGameCubeMultiBootInitCalls = 0;
    gHostIntroStubGameCubeMultiBootMainCalls = 0;
    gHostIntroStubGameCubeMultiBootHandleSerialCalls = 0;
    gHostIntroStubGameCubeMultiBootQuitCalls = 0;
    gHostIntroStubGameCubeMultiBootExecuteProgramCalls = 0;
    gHostIntroStubResetMenuAndMonGlobalsCalls = 0;
    gHostIntroStubSaveResetSaveCountersCalls = 0;
    gHostIntroStubLoadGameSaveCalls = 0;
    gHostIntroStubSav2ClearSetDefaultCalls = 0;
    gHostIntroStubSetPokemonCryStereoCalls = 0;
    gHostIntroStubSerialCBCalls = 0;
    gHostIntroStubResetSerialCalls = 0;
    gHostIntroStubResetTempTileDataBuffersCalls = 0;
    gHostIntroStubFreeTempTileDataBuffersCalls = 0;
    gHostIntroStubDecompressAndCopyTileDataToVramCalls = 0;
    gHostIntroStubResetBgPositionsCalls = 0;
    gHostIntroStubStartBlendTaskCalls = 0;
    gHostIntroStubIsBlendTaskActiveCalls = 0;
    gHostIntroStubM4aSongNumStartCalls = 0;
    gHostIntroStubPlaySECalls = 0;
    gHostIntroStubPlayCryByModeCalls = 0;
    gHostIntroStubLastPokemonCryStereoValue = 0;
    gHostIntroStubLoadGameSaveResult = SAVE_STATUS_EMPTY;
    gSaveFileStatus = SAVE_STATUS_EMPTY;
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

void ResetMenuAndMonGlobals(void)
{
    gHostIntroStubResetMenuAndMonGlobalsCalls++;
    UpstreamResetMenuAndMonGlobals();
}

void Save_ResetSaveCounters(void)
{
    gHostIntroStubSaveResetSaveCountersCalls++;
}

u8 LoadGameSave(u8 saveType)
{
    gHostIntroStubLoadGameSaveCalls++;
    (void)saveType;
    gSaveFileStatus = gHostIntroStubLoadGameSaveResult;
    return gSaveFileStatus;
}

void Sav2_ClearSetDefault(void)
{
    gHostIntroStubSav2ClearSetDefaultCalls++;
    UpstreamSav2_ClearSetDefault();
}

void SetPokemonCryStereo(u32 val)
{
    gHostIntroStubSetPokemonCryStereoCalls++;
    gHostIntroStubLastPokemonCryStereoValue = val;
}

void SerialCB(void)
{
    gHostIntroStubSerialCBCalls++;
}

void ResetSerial(void)
{
    gHostIntroStubResetSerialCalls++;
}

static u16 sTempTileDataBufferCursor = 0;
static void *sTempTileDataBuffers[0x20] = {NULL};

static u16 CopyDecompressedTileDataToVram(u8 bgId, const void *src, u16 size, u16 offset, u8 mode)
{
    switch (mode)
    {
    case 1:
        break;
    case 0:
    default:
        return LoadBgTiles(bgId, src, size, offset);
    }
    return LoadBgTilemap(bgId, src, size, offset);
}

void ResetTempTileDataBuffers(void)
{
    int i;

    gHostIntroStubResetTempTileDataBuffersCalls++;
    for (i = 0; i < (s32)NELEMS(sTempTileDataBuffers); i++)
    {
        sTempTileDataBuffers[i] = NULL;
    }
    sTempTileDataBufferCursor = 0;
}

bool8 FreeTempTileDataBuffersIfPossible(void)
{
    int i;

    gHostIntroStubFreeTempTileDataBuffersCalls++;
    if (!IsDma3ManagerBusyWithBgCopy())
    {
        if (sTempTileDataBufferCursor)
        {
            for (i = 0; i < sTempTileDataBufferCursor; i++)
            {
                FREE_AND_SET_NULL(sTempTileDataBuffers[i]);
            }
            sTempTileDataBufferCursor = 0;
        }
        return FALSE;
    }
    else
    {
        return TRUE;
    }
}

void *DecompressAndCopyTileDataToVram(u8 bgId, const void *src, u32 size, u16 offset, u8 mode)
{
    u32 sizeOut;

    gHostIntroStubDecompressAndCopyTileDataToVramCalls++;
    if (sTempTileDataBufferCursor < NELEMS(sTempTileDataBuffers))
    {
        void *ptr = MallocAndDecompress(src, &sizeOut);
        if (!size)
            size = sizeOut;
        if (ptr)
        {
            CopyDecompressedTileDataToVram(bgId, ptr, size, offset, mode);
            sTempTileDataBuffers[sTempTileDataBufferCursor++] = ptr;
        }
        return ptr;
    }
    return NULL;
}

void ResetBgPositions(void)
{
    gHostIntroStubResetBgPositionsCalls++;
}

void StartBlendTask(u8 eva_start, u8 evb_start, u8 eva_end, u8 evb_end, u8 ev_step, u8 priority)
{
    gHostIntroStubStartBlendTaskCalls++;
    (void)eva_start;
    (void)evb_start;
    (void)eva_end;
    (void)evb_end;
    (void)ev_step;
    (void)priority;
}

bool8 IsBlendTaskActive(void)
{
    gHostIntroStubIsBlendTaskActiveCalls++;
    return FALSE;
}

void m4aSongNumStart(u16 songNum)
{
    gHostIntroStubM4aSongNumStartCalls++;
    (void)songNum;
}

void PlaySE(u16 songNum)
{
    gHostIntroStubPlaySECalls++;
    (void)songNum;
}

void PlayCry_ByMode(u16 species, s8 pan, u8 mode)
{
    gHostIntroStubPlayCryByModeCalls++;
    (void)species;
    (void)pan;
    (void)mode;
}

void StoreWordInTwoHalfwords(u16 *h, u32 w)
{
    h[0] = LOHALF(w);
    h[1] = HIHALF(w);
}

void LoadWordFromTwoHalfwords(u16 *h, u32 *w)
{
    *w = (u32)h[0] | ((u32)h[1] << 16);
}
