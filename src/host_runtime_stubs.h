#ifndef POKEFIRERED_NATIVE_HOST_RUNTIME_STUBS_H
#define POKEFIRERED_NATIVE_HOST_RUNTIME_STUBS_H

#include "main.h"

extern u32 gHostStubInitRFUCalls;
extern u32 gHostStubMapMusicMainCalls;
extern u32 gHostStubSoftResetCalls;

void HostStubReset(void);

#endif
