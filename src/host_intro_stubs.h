#ifndef POKEFIRERED_NATIVE_HOST_INTRO_STUBS_H
#define POKEFIRERED_NATIVE_HOST_INTRO_STUBS_H

#include "global.h"

extern u32 gHostIntroStubGameCubeMultiBootInitCalls;
extern u32 gHostIntroStubGameCubeMultiBootMainCalls;
extern u32 gHostIntroStubGameCubeMultiBootHandleSerialCalls;
extern u32 gHostIntroStubGameCubeMultiBootQuitCalls;
extern u32 gHostIntroStubGameCubeMultiBootExecuteProgramCalls;
/* ResetMenuAndMonGlobals, Sav2_ClearSetDefault, SetPokemonCryStereo,
   SerialCB, ResetSerial now from upstream */
extern u32 gHostIntroStubResetTempTileDataBuffersCalls;
extern u32 gHostIntroStubFreeTempTileDataBuffersCalls;
extern u32 gHostIntroStubDecompressAndCopyTileDataToVramCalls;
extern u32 gHostIntroStubResetBgPositionsCalls;
extern u32 gHostIntroStubStartBlendTaskCalls;
extern u32 gHostIntroStubIsBlendTaskActiveCalls;
/* PlaySE, PlayCry_ByMode, SetPokemonCryStereo now from upstream */
extern u8 gHostIntroStubLoadGameSaveResult;

void HostIntroStubReset(void);

#endif
