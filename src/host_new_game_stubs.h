#ifndef POKEFIRERED_NATIVE_HOST_NEW_GAME_STUBS_H
#define POKEFIRERED_NATIVE_HOST_NEW_GAME_STUBS_H

#include "global.h"

extern u32 gHostNewGameSetWarpDestinationCalls;
extern u32 gHostNewGameWarpIntoMapCalls;
extern u32 gHostNewGameRunScriptImmediatelyCalls;
extern u32 gHostNewGameStopMapMusicCalls;
extern u32 gHostNewGameResetSafariZoneFlagCalls;
extern u32 gHostNewGameResetInitialPlayerAvatarStateCalls;
extern u32 gHostNewGameScriptContextInitCalls;
extern u32 gHostNewGameUnlockPlayerFieldControlsCalls;
extern u32 gHostNewGameDoMapLoadLoopCalls;
extern u32 gHostNewGameSetFieldVBlankCallbackCalls;
extern const u8 *gHostNewGameLastRunScript;
extern struct WarpData gHostNewGameWarpDestination;

void StopMapMusic(void);
void ResetSafariZoneFlag(void);
void ResetInitialPlayerAvatarState(void);
void ScriptContext_Init(void);
void UnlockPlayerFieldControls(void);
void HostDoMapLoadLoop(u8 *state);
void HostSetFieldVBlankCallback(void);

void HostNewGameStubReset(void);

#endif
