#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "gba/gba.h"
#include "host_dma.h"
#include "host_memory.h"
#include "host_savestate.h"
#include "host_timer.h"

/* Forward declaration — defined in host_bg_regs.c */
extern void HostBgRegsInit(void);

/* ── Dynamic GBA memory base addresses ──
 * These are the extern globals declared in gba.h.
 * Each dlmopen namespace gets its own copy.
 * HostMemoryInit() sets them from mmap() return values.
 */
uintptr_t pfr_ewram_base = 0;
uintptr_t pfr_iwram_base = 0;
uintptr_t pfr_reg_base   = 0;
uintptr_t pfr_pltt_base  = 0;
uintptr_t pfr_vram_base  = 0;
uintptr_t pfr_oam_base   = 0;

struct HostRegion
{
    uintptr_t *base_ptr;   /* pointer to the global holding the mapped address */
    size_t size;
    const char *name;
};

static const struct HostRegion sHostRegions[] = {
    { &pfr_ewram_base, 0x40000, "EWRAM" },
    { &pfr_iwram_base, 0x8000,  "IWRAM" },
    { &pfr_reg_base,   0x1000,  "IO" },
    { &pfr_pltt_base,  0x400,   "PLTT" },
    { &pfr_vram_base,  0x18000, "VRAM" },
    { &pfr_oam_base,   0x400,   "OAM" },
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

    mapped = mmap(NULL,
                  region->size,
                  PROT_READ | PROT_WRITE,
                  MAP_PRIVATE | MAP_ANONYMOUS,
                  -1,
                  0);
    if (mapped == MAP_FAILED)
    {
        fprintf(stderr,
                "failed to map host region %s (%zu bytes): %s\n",
                region->name,
                region->size,
                strerror(errno));
        abort();
    }

    *(region->base_ptr) = (uintptr_t)mapped;
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
    HostSavestateProtectRegion(&sHostMemoryMapped, sizeof(sHostMemoryMapped));
    HostSavestateProtectRegion(&pfr_ewram_base, sizeof(pfr_ewram_base));
    HostSavestateProtectRegion(&pfr_iwram_base, sizeof(pfr_iwram_base));
    HostSavestateProtectRegion(&pfr_reg_base,   sizeof(pfr_reg_base));
    HostSavestateProtectRegion(&pfr_pltt_base,  sizeof(pfr_pltt_base));
    HostSavestateProtectRegion(&pfr_vram_base,  sizeof(pfr_vram_base));
    HostSavestateProtectRegion(&pfr_oam_base,   sizeof(pfr_oam_base));

    HostMemoryReset();
    HostBgRegsInit();
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
