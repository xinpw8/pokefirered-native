#include <string.h>

#include "global.h"

#include "host_savestate.h"
#include "pfr_env_slot.h"

bool8 PfrEnvSlotCreate(struct PfrEnvSlot *slot)
{
    if (slot == NULL)
        return FALSE;

    memset(slot, 0, sizeof(*slot));
    slot->snapshot = HostSavestateCreateSnapshot();
    return slot->snapshot != NULL;
}

void PfrEnvSlotDestroy(struct PfrEnvSlot *slot)
{
    if (slot == NULL)
        return;

    HostSavestateDestroySnapshot(slot->snapshot);
    slot->snapshot = NULL;
}

bool8 PfrEnvSlotCaptureCurrent(struct PfrEnvSlot *slot)
{
    if (slot == NULL || slot->snapshot == NULL)
        return FALSE;

    return HostSavestateCaptureSnapshot(slot->snapshot);
}

bool8 PfrEnvSlotRestore(const struct PfrEnvSlot *slot)
{
    if (slot == NULL || slot->snapshot == NULL)
        return FALSE;

    return HostSavestateRestoreSnapshot(slot->snapshot);
}

bool8 PfrEnvSlotReadPacket(const struct PfrEnvSlot *slot, struct PfrRlPacket *packet, u32 frame, u16 heldButtons, bool8 inOverworld, bool8 inBattle)
{
    if (packet == NULL || !PfrEnvSlotRestore(slot))
        return FALSE;

    PfrRlCapturePacket(packet, frame, heldButtons, inOverworld, inBattle);
    return TRUE;
}
