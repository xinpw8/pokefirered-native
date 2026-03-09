#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "gba/gba.h"
#include "host_dma.h"
#include "host_memory.h"

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
    size_t i;

    if (!sHostMemoryMapped)
        return;

    for (i = 0; i < ARRAY_COUNT(sHostRegions); ++i)
        memset((void *)sHostRegions[i].base, 0, sHostRegions[i].size);

    /* GBA hardware defaults: affine BG params reset to identity (1.0 in 8.8 fixed) */
    *(vu16 *)(REG_BASE + 0x20) = 0x0100; /* BG2PA */
    *(vu16 *)(REG_BASE + 0x26) = 0x0100; /* BG2PD */
    *(vu16 *)(REG_BASE + 0x30) = 0x0100; /* BG3PA */
    *(vu16 *)(REG_BASE + 0x36) = 0x0100; /* BG3PD */

    HostDmaReset();
}
