#ifndef POKEFIRERED_NATIVE_HOST_GBA_MACRO_H
#define POKEFIRERED_NATIVE_HOST_GBA_MACRO_H

#include <stdint.h>

#include "../../../third_party/pokefirered/include/gba/macro.h"

#include "host_dma.h"

#undef DmaSet
#define DmaSet(dmaNum, src, dest, control) \
{                                          \
    HostDmaSet((u8)(dmaNum), (const void *)(uintptr_t)(src), (void *)(uintptr_t)(dest), (u32)(control)); \
}

#undef DmaStop
#define DmaStop(dmaNum) \
{                       \
    HostDmaStop(dmaNum); \
}

#endif
