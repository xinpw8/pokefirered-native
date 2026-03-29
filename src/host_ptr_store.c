// host_ptr_store.c — Side table for storing 64-bit pointers in s16 pairs
//
// GBA code stores pointers in two s16 task data slots (32 bits total).
// On 64-bit, this truncates the upper 32 bits. Instead, we store the
// pointer in a side table and put a small index into the s16 pair.
//
// The index is encoded as: lo = index & 0xFFFF, hi = (index >> 16) | 0x8000
// The 0x8000 flag in hi distinguishes indexed pointers from NULL (0,0).

#include "global.h"

#if HOST_NATIVE && __SIZEOF_POINTER__ == 8

#include <string.h>

#define PTR_STORE_CAPACITY 256
#define HI_FLAG 0x4000  // Flag to mark that hi contains an index, not raw bits

static const void *sPtrStore[PTR_STORE_CAPACITY];
static u32 sNextSlot = 1; // slot 0 reserved for NULL

void HostPtrStore_Reset(void)
{
    memset(sPtrStore, 0, sizeof(sPtrStore));
    sNextSlot = 1;
}

void HostPtrStore_Put(s16 *lo, s16 *hi, const void *ptr)
{
    u16 slot;

    if (ptr == NULL)
    {
        *lo = 0;
        *hi = 0;
        return;
    }

    // Check if this pointer is already stored (reuse slot)
    for (slot = 1; slot < sNextSlot && slot < PTR_STORE_CAPACITY; slot++)
    {
        if (sPtrStore[slot] == ptr)
        {
            *lo = (s16)(slot & 0xFFFF);
            *hi = (s16)(HI_FLAG);
            return;
        }
    }

    // Allocate new slot
    if (sNextSlot >= PTR_STORE_CAPACITY)
    {
        // Wrap around and try to find an empty slot
        // In practice this table won't fill up because entries are short-lived
        for (slot = 1; slot < PTR_STORE_CAPACITY; slot++)
        {
            if (sPtrStore[slot] == NULL)
            {
                sPtrStore[slot] = ptr;
                *lo = (s16)(slot & 0xFFFF);
                *hi = (s16)(HI_FLAG);
                return;
            }
        }
        // Table full — this should never happen. Fall through to truncated store.
        *lo = (s16)((uintptr_t)ptr & 0xFFFF);
        *hi = (s16)(((uintptr_t)ptr >> 16) & 0xFFFF);
        return;
    }

    slot = sNextSlot++;
    sPtrStore[slot] = ptr;
    *lo = (s16)(slot & 0xFFFF);
    *hi = (s16)(HI_FLAG);
}

void *HostPtrStore_Get(s16 lo, s16 hi)
{
    if (lo == 0 && hi == 0)
        return NULL;

    if ((u16)hi == HI_FLAG)
    {
        u16 slot = (u16)lo;
        if (slot < PTR_STORE_CAPACITY)
            return (void *)sPtrStore[slot];
        return NULL;
    }

    // Legacy path — raw 32-bit value (shouldn't happen on 64-bit but be safe)
    return (void *)(uintptr_t)(u32)((u16)lo | ((u16)hi << 16));
}

#endif // HOST_NATIVE && 64-bit
