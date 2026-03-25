#ifndef POKEFIRERED_NATIVE_HOST_GBA_MACRO_H
#define POKEFIRERED_NATIVE_HOST_GBA_MACRO_H

#include <stdint.h>
#include <string.h>

#include "../../../../pokefirered/include/gba/macro.h"

#include "host_dma.h"

/* ── Low-level DMA register override ── */

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

/* ── DMA fill: memset/fill loop instead of per-unit DmaSet ── */

#undef DMA_FILL
#define DMA_FILL(dmaNum, value, dest, size, bit)                  \
do {                                                              \
    void *_df_p = (void *)(uintptr_t)(dest);                     \
    u32 _df_sz = (u32)(size);                                    \
    if ((bit) == 16) {                                            \
        u16 _df_v16 = (u16)(value);                              \
        if (_df_v16 == 0) {                                       \
            memset(_df_p, 0, _df_sz);                             \
        } else {                                                  \
            for (u32 _i = 0; _i < _df_sz; _i += 2)               \
                *(u16 *)((u8 *)_df_p + _i) = _df_v16;            \
        }                                                         \
    } else {                                                      \
        u32 _df_v32 = (u32)(value);                              \
        if (_df_v32 == 0) {                                       \
            memset(_df_p, 0, _df_sz);                             \
        } else {                                                  \
            for (u32 _i = 0; _i < _df_sz; _i += 4)               \
                *(u32 *)((u8 *)_df_p + _i) = _df_v32;            \
        }                                                         \
    }                                                             \
} while (0)

#undef DmaFill16
#define DmaFill16(dmaNum, value, dest, size) DMA_FILL(dmaNum, value, dest, size, 16)
#undef DmaFill32
#define DmaFill32(dmaNum, value, dest, size) DMA_FILL(dmaNum, value, dest, size, 32)

#undef DMA_CLEAR
#define DMA_CLEAR(dmaNum, dest, size, bit) DMA_FILL(dmaNum, 0, dest, size, bit)
#undef DmaClear16
#define DmaClear16(dmaNum, dest, size) DMA_FILL(dmaNum, 0, dest, size, 16)
#undef DmaClear32
#define DmaClear32(dmaNum, dest, size) DMA_FILL(dmaNum, 0, dest, size, 32)

/* ── DMA copy: memcpy instead of per-unit DmaSet ── */

#undef DMA_COPY
#define DMA_COPY(dmaNum, src, dest, size, bit)                    \
do {                                                              \
    memcpy((void *)(uintptr_t)(dest), (const void *)(uintptr_t)(src), (size_t)(size)); \
} while (0)

#undef DmaCopy16
#define DmaCopy16(dmaNum, src, dest, size) DMA_COPY(dmaNum, src, dest, size, 16)
#undef DmaCopy32
#define DmaCopy32(dmaNum, src, dest, size) DMA_COPY(dmaNum, src, dest, size, 32)

/* ── Large-block DMA: no block-size limit on native ── */

#undef DmaCopyLarge
#define DmaCopyLarge(dmaNum, src, dest, size, block, bit) \
    memcpy((void *)(uintptr_t)(dest), (const void *)(uintptr_t)(src), (size_t)(size))
#undef DmaCopyLarge16
#define DmaCopyLarge16(dmaNum, src, dest, size, block) DmaCopyLarge(dmaNum, src, dest, size, block, 16)
#undef DmaCopyLarge32
#define DmaCopyLarge32(dmaNum, src, dest, size, block) DmaCopyLarge(dmaNum, src, dest, size, block, 32)

#undef DmaClearLarge
#define DmaClearLarge(dmaNum, dest, size, block, bit) \
    memset((void *)(uintptr_t)(dest), 0, (size_t)(size))
#undef DmaClearLarge16
#define DmaClearLarge16(dmaNum, dest, size, block) DmaClearLarge(dmaNum, dest, size, block, 16)
#undef DmaClearLarge32
#define DmaClearLarge32(dmaNum, dest, size, block) DmaClearLarge(dmaNum, dest, size, block, 32)

#undef DmaFillLarge
#define DmaFillLarge(dmaNum, value, dest, size, block, bit) DMA_FILL(dmaNum, value, dest, size, bit)
#undef DmaFillLarge16
#define DmaFillLarge16(dmaNum, value, dest, size, block) DMA_FILL(dmaNum, value, dest, size, 16)
#undef DmaFillLarge32
#define DmaFillLarge32(dmaNum, value, dest, size, block) DMA_FILL(dmaNum, value, dest, size, 32)

#endif
