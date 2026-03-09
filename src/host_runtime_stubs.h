#ifndef POKEFIRERED_NATIVE_HOST_RUNTIME_STUBS_H
#define POKEFIRERED_NATIVE_HOST_RUNTIME_STUBS_H

#include "main.h"

/* InitRFU now from link_rfu_2.c */
extern u32 gHostStubMapMusicMainCalls;
extern u32 gHostStubSoftResetCalls;

void HostStubReset(void);

#endif
