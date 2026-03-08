#include <string.h>

#include "battle_controllers.h"
#include "decompress.h"
#include "gba/flash_internal.h"
#include "help_system.h"
#include "host_runtime_stubs.h"
#include "intro.h"
#include "link.h"
#include "librfu.h"
#include "load_save.h"
#include "m4a.h"
#include "malloc.h"
#include "new_menu_helpers.h"
#include "overworld.h"
#include "play_time.h"
#include "pokemon.h"
#include "quest_log.h"
#include "save_failed_screen.h"
#include "sound.h"
#include "sprite.h"

EWRAM_DATA u8 gHeap[HEAP_SIZE] = {0};
COMMON_DATA u32 gRng2Value = 0;
EWRAM_DATA u16 gBattle_BG0_X = 0;
EWRAM_DATA u16 gBattle_BG0_Y = 0;
EWRAM_DATA u16 gBattle_BG1_X = 0;
EWRAM_DATA u16 gBattle_BG1_Y = 0;
EWRAM_DATA u16 gBattle_BG2_X = 0;
EWRAM_DATA u16 gBattle_BG2_Y = 0;
EWRAM_DATA u16 gBattle_BG3_X = 0;
EWRAM_DATA u16 gBattle_BG3_Y = 0;

struct SaveBlock1 gSaveBlock1 = {0};
struct SaveBlock2 gSaveBlock2 = {0};
struct SaveBlock1 *gSaveBlock1Ptr = NULL;
struct SaveBlock2 *gSaveBlock2Ptr = NULL;
struct STWIStatus *gSTWIStatus = NULL;
bool8 gHelpSystemEnabled = FALSE;
u8 gQuestLogPlaybackState = 0;
u8 gWirelessCommType = 0;
struct PokemonCrySong gPokemonCrySongs[MAX_POKEMON_CRIES] = {0};
struct SoundInfo gSoundInfo = {0};
u32 intr_main[0x200] = {
    [0] = 0x11223344,
    [1] = 0x55667788,
    [2] = 0x99AABBCC,
    [0x1FF] = 0xA5A5A5A5,
};

u32 gHostStubLinkVSyncCalls = 0;
u32 gHostStubRfuVSyncCalls = 0;
u32 gHostStubM4aSoundInitCalls = 0;
u32 gHostStubM4aSoundMainCalls = 0;
u32 gHostStubM4aSoundVSyncCalls = 0;
u32 gHostStubM4aSoundVSyncOffCalls = 0;
u32 gHostStubTimer3IntrCalls = 0;
u32 gHostStubCb2InitCopyrightCalls = 0;
u32 gHostStubCheckForFlashMemoryCalls = 0;
u32 gHostStubInitRFUCalls = 0;
u32 gHostStubPlayTimeCounterUpdateCalls = 0;
u32 gHostStubMapMusicMainCalls = 0;
u32 gHostStubSetDefaultFontsPointerCalls = 0;
u32 gHostStubSetNotInSaveFailedScreenCalls = 0;
u32 gHostStubSoftResetCalls = 0;
u32 gHostStubTryReceiveLinkBattleDataCalls = 0;
u32 gHostStubUpdateWirelessStatusIndicatorSpriteCalls = 0;
u8 gHostStubLastFlashTimerNum = 0xFF;
IntrFunc *gHostStubLastFlashTimerIntr = NULL;

static const u32 sHostBlankMonPicLz77[] = {0x00080000};

const struct CompressedSpriteSheet gMonFrontPicTable[NUM_SPECIES + 1] = {
    [0 ... NUM_SPECIES] = {
        .data = sHostBlankMonPicLz77,
        .size = 0x800,
        .tag = 0,
    },
};

const struct CompressedSpriteSheet gMonBackPicTable[NUM_SPECIES + 1] = {
    [0 ... NUM_SPECIES] = {
        .data = sHostBlankMonPicLz77,
        .size = 0x800,
        .tag = 0,
    },
};

void HostStubReset(void)
{
    gHostStubLinkVSyncCalls = 0;
    gHostStubRfuVSyncCalls = 0;
    gHostStubM4aSoundInitCalls = 0;
    gHostStubM4aSoundMainCalls = 0;
    gHostStubM4aSoundVSyncCalls = 0;
    gHostStubM4aSoundVSyncOffCalls = 0;
    gHostStubTimer3IntrCalls = 0;
    gHostStubCb2InitCopyrightCalls = 0;
    gHostStubCheckForFlashMemoryCalls = 0;
    gHostStubInitRFUCalls = 0;
    gHostStubPlayTimeCounterUpdateCalls = 0;
    gHostStubMapMusicMainCalls = 0;
    gHostStubSetDefaultFontsPointerCalls = 0;
    gHostStubSetNotInSaveFailedScreenCalls = 0;
    gHostStubSoftResetCalls = 0;
    gHostStubTryReceiveLinkBattleDataCalls = 0;
    gHostStubUpdateWirelessStatusIndicatorSpriteCalls = 0;
    gHostStubLastFlashTimerNum = 0xFF;
    gHostStubLastFlashTimerIntr = NULL;
    gWirelessCommType = 0;
    memset(&gSoundInfo, 0, sizeof(gSoundInfo));
}

void DrawSpindaSpots(u16 species, u32 personality, u8 *dest, bool8 isFrontPic)
{
    (void)species;
    (void)personality;
    (void)dest;
    (void)isFrontPic;
}

void CheckForFlashMemory(void)
{
    gHostStubCheckForFlashMemoryCalls++;
}

bool8 HandleLinkConnection(void)
{
    return FALSE;
}

void InitMapMusic(void)
{
}

void InitRFU(void)
{
    gHostStubInitRFUCalls++;
}

void LinkVSync(void)
{
    gHostStubLinkVSyncCalls++;
}

void MapMusicMain(void)
{
    gHostStubMapMusicMainCalls++;
}

bool32 Overworld_RecvKeysFromLinkIsRunning(void)
{
    return FALSE;
}

bool32 Overworld_SendKeysToLinkIsRunning(void)
{
    return FALSE;
}

void PlayTimeCounter_Update(void)
{
    gHostStubPlayTimeCounterUpdateCalls++;
}

void RfuVSync(void)
{
    gHostStubRfuVSyncCalls++;
}

bool8 RunHelpSystemCallback(void)
{
    return FALSE;
}

bool32 RunSaveFailedScreen(void)
{
    return FALSE;
}

void SetDefaultFontsPointer(void)
{
    gHostStubSetDefaultFontsPointerCalls++;
}

u16 SetFlashTimerIntr(u8 timerNum, void (**intrFunc)(void))
{
    gHostStubLastFlashTimerNum = timerNum;
    gHostStubLastFlashTimerIntr = intrFunc;
    return 0;
}

void SetNotInSaveFailedScreen(void)
{
    gHostStubSetNotInSaveFailedScreenCalls++;
}

void Timer3Intr(void)
{
    gHostStubTimer3IntrCalls++;
}

void TryReceiveLinkBattleData(void)
{
    gHostStubTryReceiveLinkBattleDataCalls++;
}

void UpdateWirelessStatusIndicatorSprite(void)
{
    gHostStubUpdateWirelessStatusIndicatorSpriteCalls++;
}

void m4aSoundInit(void)
{
    gHostStubM4aSoundInitCalls++;
}

void m4aSoundMain(void)
{
    gHostStubM4aSoundMainCalls++;
}

void m4aSoundVSync(void)
{
    gHostStubM4aSoundVSyncCalls++;
}

void m4aSoundVSyncOff(void)
{
    gHostStubM4aSoundVSyncOffCalls++;
}

void rfu_REQ_stopMode(void)
{
}

u16 rfu_waitREQComplete(void)
{
    return 0;
}
