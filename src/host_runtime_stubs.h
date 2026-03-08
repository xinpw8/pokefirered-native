#ifndef POKEFIRERED_NATIVE_HOST_RUNTIME_STUBS_H
#define POKEFIRERED_NATIVE_HOST_RUNTIME_STUBS_H

#include "main.h"

extern u32 gHostStubLinkVSyncCalls;
extern u32 gHostStubRfuVSyncCalls;
extern u32 gHostStubM4aSoundInitCalls;
extern u32 gHostStubM4aSoundMainCalls;
extern u32 gHostStubM4aSoundVSyncCalls;
extern u32 gHostStubM4aSoundVSyncOffCalls;
extern u32 gHostStubTimer3IntrCalls;
extern u32 gHostStubCb2InitCopyrightCalls;
extern u32 gHostStubCheckForFlashMemoryCalls;
extern u32 gHostStubInitRFUCalls;
extern u32 gHostStubPlayTimeCounterUpdateCalls;
extern u32 gHostStubMapMusicMainCalls;
extern u32 gHostStubSetDefaultFontsPointerCalls;
extern u32 gHostStubSetNotInSaveFailedScreenCalls;
extern u32 gHostStubSoftResetCalls;
extern u32 gHostStubTryReceiveLinkBattleDataCalls;
extern u32 gHostStubUpdateWirelessStatusIndicatorSpriteCalls;
extern u8 gHostStubLastFlashTimerNum;
extern IntrFunc *gHostStubLastFlashTimerIntr;

void HostStubReset(void);

#endif
