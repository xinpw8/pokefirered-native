#ifndef POKEFIRERED_NATIVE_HOST_SAVESTATE_H
#define POKEFIRERED_NATIVE_HOST_SAVESTATE_H

#include "global.h"

bool8 HostSavestateInit(void);
void HostSavestateShutdown(void);

bool8 HostSavestateHasHotState(void);
bool8 HostSavestateCaptureHot(void);
bool8 HostSavestateRestoreHot(void);

bool8 HostSavestateSaveToFile(const char *path);
bool8 HostSavestateLoadFromFile(const char *path);

const char *HostSavestateGetLastError(void);

/* Register a memory region that must NOT be overwritten during
 * savestate restore (e.g. SDL pointers, host-side handles).
 * Call during module init, before any savestate operation. */
void HostSavestateProtectRegion(void *addr, size_t size);

#endif /* POKEFIRERED_NATIVE_HOST_SAVESTATE_H */
