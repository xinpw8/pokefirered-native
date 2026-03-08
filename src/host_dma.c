#include <stdint.h>
#include <string.h>

#include "gba/gba.h"
#include "host_dma.h"

#define HOST_DMA_CHANNEL_COUNT 4

static uintptr_t sDmaSrcAddrs[HOST_DMA_CHANNEL_COUNT];
static uintptr_t sDmaDestAddrs[HOST_DMA_CHANNEL_COUNT];

static volatile u32 *GetDmaRegs(u8 dma_num)
{
    return (volatile u32 *)(REG_ADDR_DMA0 + (dma_num * 12));
}

static size_t GetTransferUnitSize(u32 control)
{
    if (control & (DMA_32BIT << 16))
        return sizeof(u32);
    return sizeof(u16);
}

static u32 GetTransferCount(u8 dma_num, u32 control)
{
    u32 count = control & 0xFFFF;

    if (count != 0)
        return count;
    if (dma_num == 3)
        return 0x10000;
    return 0x4000;
}

static intptr_t GetSrcStep(u32 control, size_t unit_size)
{
    switch ((control >> 16) & 0x0180)
    {
    case DMA_SRC_DEC:
        return -(intptr_t)unit_size;
    case DMA_SRC_FIXED:
        return 0;
    default:
        return (intptr_t)unit_size;
    }
}

static intptr_t GetDestStep(u32 control, size_t unit_size)
{
    switch ((control >> 16) & 0x0060)
    {
    case DMA_DEST_DEC:
        return -(intptr_t)unit_size;
    case DMA_DEST_FIXED:
        return 0;
    case DMA_DEST_RELOAD:
        return (intptr_t)unit_size;
    default:
        return (intptr_t)unit_size;
    }
}

static void SyncRegs(u8 dma_num, uintptr_t src, uintptr_t dest, u32 control)
{
    volatile u32 *dma_regs = GetDmaRegs(dma_num);

    sDmaSrcAddrs[dma_num] = src;
    sDmaDestAddrs[dma_num] = dest;
    dma_regs[0] = (u32)src;
    dma_regs[1] = (u32)dest;
    dma_regs[2] = control;
}

static void ExecuteTransfer(u8 dma_num)
{
    volatile u32 *dma_regs = GetDmaRegs(dma_num);
    uintptr_t initial_src = sDmaSrcAddrs[dma_num] ? sDmaSrcAddrs[dma_num] : dma_regs[0];
    uintptr_t initial_dest = sDmaDestAddrs[dma_num] ? sDmaDestAddrs[dma_num] : dma_regs[1];
    uintptr_t src_addr = initial_src;
    uintptr_t dest_addr = initial_dest;
    u32 control = dma_regs[2];
    size_t unit_size = GetTransferUnitSize(control);
    u32 count = GetTransferCount(dma_num, control);
    intptr_t src_step = GetSrcStep(control, unit_size);
    intptr_t dest_step = GetDestStep(control, unit_size);
    u32 control_hi = control >> 16;
    u32 i;

    for (i = 0; i < count; ++i)
    {
        memcpy((void *)dest_addr, (const void *)src_addr, unit_size);
        src_addr += src_step;
        dest_addr += dest_step;
    }

    if ((control_hi & 0x0060) == DMA_DEST_RELOAD)
        dest_addr = initial_dest;

    if ((control_hi & DMA_REPEAT) == 0 || (control_hi & DMA_START_MASK) == DMA_START_NOW)
        control &= ~(DMA_ENABLE << 16);

    SyncRegs(dma_num, src_addr, dest_addr, control);
}

static void TriggerByStartMode(u32 start_mode)
{
    u8 dma_num;

    for (dma_num = 0; dma_num < HOST_DMA_CHANNEL_COUNT; ++dma_num)
    {
        volatile u32 *dma_regs = GetDmaRegs(dma_num);
        u32 control = dma_regs[2];

        if (((control >> 16) & DMA_ENABLE) == 0)
            continue;
        if (((control >> 16) & DMA_START_MASK) != start_mode)
            continue;

        ExecuteTransfer(dma_num);
    }
}

void HostDmaReset(void)
{
    memset(sDmaSrcAddrs, 0, sizeof(sDmaSrcAddrs));
    memset(sDmaDestAddrs, 0, sizeof(sDmaDestAddrs));
}

void HostDmaSet(u8 dma_num, const void *src, void *dest, u32 control)
{
    if (dma_num >= HOST_DMA_CHANNEL_COUNT)
        return;

    SyncRegs(dma_num, (uintptr_t)src, (uintptr_t)dest, control);

    if (((control >> 16) & DMA_ENABLE) != 0 && (((control >> 16) & DMA_START_MASK) == DMA_START_NOW))
        ExecuteTransfer(dma_num);
}

void HostDmaStop(u8 dma_num)
{
    volatile u32 *dma_regs;
    u32 control;

    if (dma_num >= HOST_DMA_CHANNEL_COUNT)
        return;

    dma_regs = GetDmaRegs(dma_num);
    control = dma_regs[2];
    control &= ~((DMA_START_MASK | DMA_DREQ_ON | DMA_REPEAT | DMA_ENABLE) << 16);
    SyncRegs(dma_num,
             sDmaSrcAddrs[dma_num] ? sDmaSrcAddrs[dma_num] : dma_regs[0],
             sDmaDestAddrs[dma_num] ? sDmaDestAddrs[dma_num] : dma_regs[1],
             control);
}

void HostDmaTriggerHBlank(void)
{
    TriggerByStartMode(DMA_START_HBLANK);
}

void HostDmaTriggerVBlank(void)
{
    TriggerByStartMode(DMA_START_VBLANK);
}
