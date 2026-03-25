#ifndef GUARD_DMA3_H
#define GUARD_DMA3_H

#include "global.h"

#define MAX_DMA_BLOCK_SIZE 0x1000

#define DMA_REQUEST_COPY32 1
#define DMA_REQUEST_FILL32 2
#define DMA_REQUEST_COPY16 3
#define DMA_REQUEST_FILL16 4

#define DMA3_16BIT 0
#define DMA3_32BIT 1

/*
 * Dma3*Large* macros are overridden in host gba/macro.h to use
 * memset/memcpy directly.  The Dma3CopyLarge_/Dma3FillLarge_ macros
 * used by the upstream dma3_manager.c are defined here for that TU.
 */
#define Dma3CopyLarge_(src, dest, size, bit) \
    memcpy((void *)(uintptr_t)(dest), (const void *)(uintptr_t)(src), (size_t)(size))
#define Dma3CopyLarge16_(src, dest, size) Dma3CopyLarge_(src, dest, size, 16)
#define Dma3CopyLarge32_(src, dest, size) Dma3CopyLarge_(src, dest, size, 32)

#define Dma3FillLarge_(value, dest, size, bit)                       \
do {                                                                 \
    void *_dfl_p = (void *)(uintptr_t)(dest);                       \
    u32 _dfl_sz = (u32)(size);                                      \
    if ((value) == 0) {                                              \
        memset(_dfl_p, 0, _dfl_sz);                                  \
    } else if ((bit) == 16) {                                        \
        u16 _dfl_v = (u16)(value);                                   \
        for (u32 _i = 0; _i < _dfl_sz; _i += 2)                     \
            *(u16 *)((u8 *)_dfl_p + _i) = _dfl_v;                   \
    } else {                                                         \
        u32 _dfl_v = (u32)(value);                                   \
        for (u32 _i = 0; _i < _dfl_sz; _i += 4)                     \
            *(u32 *)((u8 *)_dfl_p + _i) = _dfl_v;                   \
    }                                                                \
} while (0)
#define Dma3FillLarge16_(value, dest, size) Dma3FillLarge_(value, dest, size, 16)
#define Dma3FillLarge32_(value, dest, size) Dma3FillLarge_(value, dest, size, 32)

void ClearDma3Requests(void);
void ProcessDma3Requests(void);
s16 RequestDma3Copy(const void *src, void *dest, u16 size, u8 mode);
s16 RequestDma3Fill(s32 value, void *dest, u16 size, u8 mode);
s16 WaitDma3Request(s16 index);

#endif
