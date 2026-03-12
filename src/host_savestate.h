#ifndef POKEFIRERED_NATIVE_HOST_SAVESTATE_H
#define POKEFIRERED_NATIVE_HOST_SAVESTATE_H

#include "global.h"

struct HostSavestateSnapshot;

bool8 HostSavestateInit(void);
void HostSavestateShutdown(void);

bool8 HostSavestateHasHotState(void);
bool8 HostSavestateCaptureHot(void);
bool8 HostSavestateRestoreHot(void);

struct HostSavestateSnapshot *HostSavestateCreateSnapshot(void);
void HostSavestateDestroySnapshot(struct HostSavestateSnapshot *snapshot);
bool8 HostSavestateCaptureSnapshot(struct HostSavestateSnapshot *snapshot);
bool8 HostSavestateRestoreSnapshot(const struct HostSavestateSnapshot *snapshot);

bool8 HostSavestateSaveToFile(const char *path);
bool8 HostSavestateLoadFromFile(const char *path);

const char *HostSavestateGetLastError(void);

#endif /* POKEFIRERED_NATIVE_HOST_SAVESTATE_H */
