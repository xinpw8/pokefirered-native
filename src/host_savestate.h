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

/* Degraded load: extract only the g_ctx segment from a mismatched state.
 * Returns a malloc'd buffer (caller frees).  *outSize receives the size. */
void *HostSavestateExtractGCtx(const char *path, size_t *outSize);

/* Returns TRUE if the state file's fingerprint matches the current build. */
bool8 HostSavestateCheckFingerprint(const char *path);

/* Register a memory region that must NOT be overwritten during
 * savestate restore (e.g. SDL pointers, host-side handles).
 * Call during module init, before any savestate operation. */
/* Register an additional memory segment to save/restore. */
bool8 HostSavestateAddSegment(void *base, size_t size);

void HostSavestateProtectRegion(void *addr, size_t size);

#endif /* POKEFIRERED_NATIVE_HOST_SAVESTATE_H */
