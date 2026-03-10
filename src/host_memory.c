#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "gba/gba.h"
#include "host_dma.h"
#include "host_memory.h"
#include "host_timer.h"

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

struct HostRegion
{
    uintptr_t base;
    size_t size;
    const char *name;
};

static const struct HostRegion sHostRegions[] = {
    { EWRAM_START, EWRAM_END - EWRAM_START, "EWRAM" },
    { IWRAM_START, IWRAM_END - IWRAM_START, "IWRAM" },
    { REG_BASE, 0x1000, "IO" },
    { PLTT, PLTT_SIZE, "PLTT" },
    { VRAM, VRAM_SIZE, "VRAM" },
    { OAM, OAM_SIZE, "OAM" },
};

static bool8 sHostMemoryMapped = FALSE;

static void ResetAffineBgIdentity(void)
{
    *(vu16 *)(REG_BASE + REG_OFFSET_BG2PA) = 0x0100;
    *(vu16 *)(REG_BASE + REG_OFFSET_BG2PD) = 0x0100;
    *(vu16 *)(REG_BASE + REG_OFFSET_BG3PA) = 0x0100;
    *(vu16 *)(REG_BASE + REG_OFFSET_BG3PD) = 0x0100;
}

static void ZeroIoRange(u32 offset, size_t size)
{
    memset((void *)(REG_BASE + offset), 0, size);
}

static void MapRegion(const struct HostRegion *region)
{
    void *mapped;

    mapped = mmap((void *)region->base,
                  region->size,
                  PROT_READ | PROT_WRITE,
                  MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE,
                  -1,
                  0);
    if (mapped == MAP_FAILED)
    {
        fprintf(stderr,
                "failed to map host region %s at 0x%08lx (%zu bytes): %s\n",
                region->name,
                (unsigned long)region->base,
                region->size,
                strerror(errno));
        abort();
    }
}

void HostMemoryInit(void)
{
    size_t i;

    if (sHostMemoryMapped)
    {
        HostMemoryReset();
        return;
    }

    for (i = 0; i < ARRAY_COUNT(sHostRegions); ++i)
        MapRegion(&sHostRegions[i]);

    sHostMemoryMapped = TRUE;
    HostMemoryReset();
}

void HostMemoryReset(void)
{
    HostMemoryResetByFlags(RESET_ALL);
}

void HostMemoryResetByFlags(u32 resetFlags)
{
    if (!sHostMemoryMapped)
        return;

    if (resetFlags & RESET_EWRAM)
        memset((void *)EWRAM_START, 0, EWRAM_END - EWRAM_START);
    if (resetFlags & RESET_IWRAM)
        memset((void *)IWRAM_START, 0, IWRAM_END - IWRAM_START);
    if (resetFlags & RESET_PALETTE)
        memset((void *)PLTT, 0, PLTT_SIZE);
    if (resetFlags & RESET_VRAM)
        memset((void *)VRAM, 0, VRAM_SIZE);
    if (resetFlags & RESET_OAM)
        memset((void *)OAM, 0, OAM_SIZE);

    if (resetFlags & RESET_SOUND_REGS)
        ZeroIoRange(REG_OFFSET_SOUND1CNT_L, REG_OFFSET_FIFO_B + sizeof(u32) - REG_OFFSET_SOUND1CNT_L);

    if (resetFlags & RESET_SIO_REGS)
    {
        ZeroIoRange(REG_OFFSET_SIODATA32, REG_OFFSET_KEYINPUT - REG_OFFSET_SIODATA32);
        ZeroIoRange(REG_OFFSET_RCNT, sizeof(u16));
        ZeroIoRange(REG_OFFSET_JOYCNT, REG_OFFSET_JOYSTAT + sizeof(u16) - REG_OFFSET_JOYCNT);
    }

    if (resetFlags & RESET_REGS)
    {
        ZeroIoRange(REG_OFFSET_DISPCNT, REG_OFFSET_SOUND1CNT_L - REG_OFFSET_DISPCNT);
        ZeroIoRange(REG_OFFSET_DMA0, REG_OFFSET_TM0CNT - REG_OFFSET_DMA0);
        ZeroIoRange(REG_OFFSET_TM0CNT, REG_OFFSET_SIODATA32 - REG_OFFSET_TM0CNT);
        ZeroIoRange(REG_OFFSET_IE, REG_OFFSET_IME + sizeof(u16) - REG_OFFSET_IE);
        HostDmaReset();
        HostTimerReset();
        ResetAffineBgIdentity();
    }

    REG_KEYINPUT = KEYS_MASK;
}
