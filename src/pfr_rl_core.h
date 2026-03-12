#ifndef POKEFIRERED_NATIVE_PFR_RL_CORE_H
#define POKEFIRERED_NATIVE_PFR_RL_CORE_H

#include "global.h"

#include "pfr_firered_state.h"

struct PfrRlCore
{
    u32 frame;
    u16 heldButtons;
    u8 bootMode;
    bool8 bootComplete;
    bool8 hotCaptured;
    char savePath[4096];
    char statePath[4096];
    char lastError[256];
};

bool8 PfrRlCoreInit(struct PfrRlCore *core, const char *savePath, const char *statePath);
bool8 PfrRlCoreReset(struct PfrRlCore *core);
bool8 PfrRlCoreStep(struct PfrRlCore *core, u16 buttons, u32 frames);
void PfrRlCoreFillPacket(const struct PfrRlCore *core, struct PfrRlPacket *packet);
const char *PfrRlCoreLastError(const struct PfrRlCore *core);

#endif /* POKEFIRERED_NATIVE_PFR_RL_CORE_H */

