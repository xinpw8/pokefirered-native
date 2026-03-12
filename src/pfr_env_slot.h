#ifndef POKEFIRERED_NATIVE_PFR_ENV_SLOT_H
#define POKEFIRERED_NATIVE_PFR_ENV_SLOT_H

#include "global.h"

#include "pfr_firered_state.h"

struct HostSavestateSnapshot;

struct PfrEnvSlot
{
    struct HostSavestateSnapshot *snapshot;
};

bool8 PfrEnvSlotCreate(struct PfrEnvSlot *slot);
void PfrEnvSlotDestroy(struct PfrEnvSlot *slot);
bool8 PfrEnvSlotCaptureCurrent(struct PfrEnvSlot *slot);
bool8 PfrEnvSlotRestore(const struct PfrEnvSlot *slot);
bool8 PfrEnvSlotReadPacket(const struct PfrEnvSlot *slot, struct PfrRlPacket *packet, u32 frame, u16 heldButtons, bool8 inOverworld, bool8 inBattle);

#endif /* POKEFIRERED_NATIVE_PFR_ENV_SLOT_H */

