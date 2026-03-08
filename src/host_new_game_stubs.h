#ifndef POKEFIRERED_NATIVE_HOST_NEW_GAME_STUBS_H
#define POKEFIRERED_NATIVE_HOST_NEW_GAME_STUBS_H

#include "global.h"

extern u32 gHostNewGameSetWarpDestinationCalls;
extern u32 gHostNewGameWarpIntoMapCalls;
extern u32 gHostNewGameRunScriptImmediatelyCalls;
extern const u8 *gHostNewGameLastRunScript;
extern struct WarpData gHostNewGameWarpDestination;

void HostNewGameStubReset(void);

#endif
