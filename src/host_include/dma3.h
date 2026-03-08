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

static inline void HostDma3Copy16Impl(const void *src, void *dest, u32 size)
{
    CpuCopy16(src, dest, size);
}

static inline void HostDma3Copy32Impl(const void *src, void *dest, u32 size)
{
    CpuCopy32(src, dest, size);
}

static inline void HostDma3Fill16Impl(u16 value, void *dest, u32 size)
{
    CpuFill16(value, dest, size);
}

static inline void HostDma3Fill32Impl(u32 value, void *dest, u32 size)
{
    CpuFill32(value, dest, size);
}

#define Dma3CopyLarge_(src, dest, size, bit)                 \
{                                                            \
    const void *_src = src;                                  \
    void *_dest = dest;                                      \
    u32 _size = size;                                        \
    while (1)                                                \
    {                                                        \
        if (_size <= MAX_DMA_BLOCK_SIZE)                     \
        {                                                    \
            HostDma3Copy##bit##Impl(_src, _dest, _size);     \
            break;                                           \
        }                                                    \
        HostDma3Copy##bit##Impl(_src, _dest, MAX_DMA_BLOCK_SIZE); \
        _src += MAX_DMA_BLOCK_SIZE;                          \
        _dest += MAX_DMA_BLOCK_SIZE;                         \
        _size -= MAX_DMA_BLOCK_SIZE;                         \
    }                                                        \
}

#define Dma3CopyLarge16_(src, dest, size) Dma3CopyLarge_(src, dest, size, 16)
#define Dma3CopyLarge32_(src, dest, size) Dma3CopyLarge_(src, dest, size, 32)

#define Dma3FillLarge_(value, dest, size, bit)               \
{                                                            \
    void *_dest = dest;                                      \
    u32 _size = size;                                        \
    while (1)                                                \
    {                                                        \
        if (_size <= MAX_DMA_BLOCK_SIZE)                     \
        {                                                    \
            HostDma3Fill##bit##Impl(value, _dest, _size);    \
            break;                                           \
        }                                                    \
        HostDma3Fill##bit##Impl(value, _dest, MAX_DMA_BLOCK_SIZE); \
        _dest += MAX_DMA_BLOCK_SIZE;                         \
        _size -= MAX_DMA_BLOCK_SIZE;                         \
    }                                                        \
}

#define Dma3FillLarge16_(value, dest, size) Dma3FillLarge_(value, dest, size, 16)
#define Dma3FillLarge32_(value, dest, size) Dma3FillLarge_(value, dest, size, 32)

void ClearDma3Requests(void);
void ProcessDma3Requests(void);
s16 RequestDma3Copy(const void *src, void *dest, u16 size, u8 mode);
s16 RequestDma3Fill(s32 value, void *dest, u16 size, u8 mode);
s16 WaitDma3Request(s16 index);

#endif
