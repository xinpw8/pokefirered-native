#define _GNU_SOURCE

#include "host_savestate.h"

#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include "gba/gba.h"

#define HOST_SAVESTATE_MAGIC   0x50465356u
#define HOST_SAVESTATE_VERSION 2u

/* Build fingerprint forward declaration — defined after linker symbol externs */
static u32 ComputeBuildFingerprint(void);

struct HostSavestateSegment
{
    uintptr_t base;
    size_t size;
};

struct HostSavestateBuffer
{
    void *data;
    size_t size;
};

#define HOST_SAVESTATE_MAX_PROTECT 64

struct HostProtectedRegion
{
    uintptr_t addr;
    size_t size;
};

struct HostSavestateRuntime
{
    u64 sessionId;
    u32 segmentCount;
    bool8 hotValid;
    struct HostSavestateSegment *segments;
    struct HostSavestateBuffer *hotBuffers;
    char lastError[256];
    struct HostProtectedRegion protect[HOST_SAVESTATE_MAX_PROTECT];
    u32 protectCount;
    size_t fileSavedSizes[8]; /* original sizes from the last loaded file */
};

struct HostSavestateFileHeader
{
    u32 magic;
    u32 version;
    u64 sessionId;
    u32 segmentCount;
    u32 buildFingerprint; /* v2: reject savestates from different builds */
};

struct HostSavestateFileSegment
{
    u64 base;
    u64 size;
};

struct HostSegmentCollection
{
    struct HostSavestateSegment *segments;
    u32 count;
    u32 capacity;
};

static struct HostSavestateRuntime *sRuntime;
extern char __data_start[];
extern char _edata[];
extern char __bss_start[];
extern char _end[];

/* Build fingerprint: hash of .data/.bss segment addresses and sizes.
 * Only changes when the binary layout actually changes (code added/removed,
 * struct sizes changed, etc.), NOT on every recompile of identical source.
 * This is what matters — function pointers in the savestate are only
 * invalid when code addresses shift. */
static u32 ComputeBuildFingerprint(void)
{
    u32 hash = 5381;
    u32 vals[4];

    vals[0] = (u32)(uintptr_t)__data_start;
    vals[1] = (u32)(_edata - __data_start);
    vals[2] = (u32)(uintptr_t)__bss_start;
    vals[3] = (u32)(_end - __bss_start);

    hash = ((hash << 5) + hash) ^ vals[0];
    hash = ((hash << 5) + hash) ^ vals[1];
    hash = ((hash << 5) + hash) ^ vals[2];
    hash = ((hash << 5) + hash) ^ vals[3];
    return hash;
}

#define HOST_GBA_REGION_COUNT 6

static void FillGbaRegions(struct HostSavestateSegment *out)
{
    out[0].base = EWRAM_START; out[0].size = EWRAM_END - EWRAM_START;
    out[1].base = IWRAM_START; out[1].size = IWRAM_END - IWRAM_START;
    out[2].base = REG_BASE;   out[2].size = 0x1000;
    out[3].base = PLTT;       out[3].size = PLTT_SIZE;
    out[4].base = VRAM;       out[4].size = VRAM_SIZE;
    out[5].base = OAM;        out[5].size = OAM_SIZE;
}

static void SetError(const char *fmt, ...)
{
    va_list args;

    if (sRuntime == NULL)
        return;

    va_start(args, fmt);
    vsnprintf(sRuntime->lastError, sizeof(sRuntime->lastError), fmt, args);
    va_end(args);
}

static void ClearError(void)
{
    if (sRuntime != NULL)
        sRuntime->lastError[0] = '\0';
}

static void *MapRuntimeBytes(size_t size)
{
    void *mapped = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    if (mapped == MAP_FAILED)
        return NULL;

    memset(mapped, 0, size);
    return mapped;
}

static bool8 AppendSegment(struct HostSegmentCollection *collection, uintptr_t base, size_t size)
{
    if (size == 0 || collection->count >= collection->capacity)
        return FALSE;

    collection->segments[collection->count].base = base;
    collection->segments[collection->count].size = size;
    collection->count++;
    return TRUE;
}

static bool8 BuildSegmentTable(void)
{
    struct HostSegmentCollection collection;
    size_t i;

    memset(&collection, 0, sizeof(collection));
    collection.capacity = HOST_GBA_REGION_COUNT + 2;
    collection.segments = MapRuntimeBytes(collection.capacity * sizeof(*collection.segments));
    if (collection.segments == NULL)
    {
        SetError("savestate: could not allocate segment table");
        return FALSE;
    }

    if (!AppendSegment(&collection,
                       (uintptr_t)__data_start,
                       (size_t)(_edata - __data_start)))
    {
        SetError("savestate: could not register main data segment");
        return FALSE;
    }

    if (!AppendSegment(&collection,
                       (uintptr_t)__bss_start,
                       (size_t)(_end - __bss_start)))
    {
        SetError("savestate: could not register main bss segment");
        return FALSE;
    }

    {
        struct HostSavestateSegment gbaRegions[HOST_GBA_REGION_COUNT];
        FillGbaRegions(gbaRegions);
        for (i = 0; i < HOST_GBA_REGION_COUNT; i++)
        {
            if (!AppendSegment(&collection, gbaRegions[i].base, gbaRegions[i].size))
            {
                SetError("savestate: segment table exhausted");
                return FALSE;
            }
        }
    }

    sRuntime->segments = collection.segments;
    sRuntime->segmentCount = collection.count;
    return TRUE;
}

static bool8 EnsureHotBuffersAllocated(void)
{
    u32 i;

    if (sRuntime->hotBuffers == NULL)
    {
        sRuntime->hotBuffers = MapRuntimeBytes(sRuntime->segmentCount * sizeof(*sRuntime->hotBuffers));
        if (sRuntime->hotBuffers == NULL)
        {
            SetError("savestate: could not allocate hot buffer table");
            return FALSE;
        }
    }

    for (i = 0; i < sRuntime->segmentCount; i++)
    {
        if (sRuntime->hotBuffers[i].data != NULL)
            continue;

        sRuntime->hotBuffers[i].size = sRuntime->segments[i].size;
        sRuntime->hotBuffers[i].data = MapRuntimeBytes(sRuntime->segments[i].size);
        if (sRuntime->hotBuffers[i].data == NULL)
        {
            SetError("savestate: could not allocate hot buffer %u", i);
            return FALSE;
        }
    }

    return TRUE;
}

static bool8 CaptureIntoBuffers(struct HostSavestateBuffer *buffers)
{
    u32 i;

    for (i = 0; i < sRuntime->segmentCount; i++)
    {
        memcpy(buffers[i].data, (const void *)sRuntime->segments[i].base, sRuntime->segments[i].size);
    }

    return TRUE;
}

static void RestoreFromBuffers(const struct HostSavestateBuffer *buffers)
{
    u32 i;
    /* Snapshot the runtime state onto the stack BEFORE the bulk memcpy.
     * The .bss memcpy will overwrite the sRuntime pointer mid-loop,
     * making it point to a stale mmap address from the saved session.
     * Using stack-local copies avoids dereferencing sRuntime after
     * it's been clobbered. */
    struct HostSavestateRuntime *rt = sRuntime;
    u32 segmentCount = rt->segmentCount;
    struct HostSavestateSegment *segments = rt->segments;
    u32 protectCount = rt->protectCount;

    /* Back up protected regions (host pointers like SDL handles)
     * before the bulk memcpy overwrites .data/.bss. */
    u8 protectBackup[HOST_SAVESTATE_MAX_PROTECT * sizeof(void *)];
    size_t backupOffset = 0;

    for (i = 0; i < protectCount; i++)
    {
        memcpy(protectBackup + backupOffset,
               (const void *)rt->protect[i].addr,
               rt->protect[i].size);
        backupOffset += rt->protect[i].size;
    }

    for (i = 0; i < segmentCount; i++)
    {
        memcpy((void *)segments[i].base, buffers[i].data, segments[i].size);
    }

    /* Restore protected regions so host handles remain valid. */
    backupOffset = 0;
    for (i = 0; i < protectCount; i++)
    {
        memcpy((void *)rt->protect[i].addr,
               protectBackup + backupOffset,
               rt->protect[i].size);
        backupOffset += rt->protect[i].size;
    }
}

static bool8 BuildTempPath(char *outPath, size_t outSize, const char *path)
{
    int written;

    written = snprintf(outPath, outSize, "%s.tmp", path);
    return written > 0 && (size_t)written < outSize;
}

bool8 HostSavestateInit(void)
{
    struct timespec now;

    if (sRuntime != NULL)
    {
        ClearError();
        return TRUE;
    }

    sRuntime = MapRuntimeBytes(sizeof(*sRuntime));
    if (sRuntime == NULL)
        return FALSE;

    clock_gettime(CLOCK_MONOTONIC, &now);
    sRuntime->sessionId = ((u64)getpid() << 32) ^ (u64)now.tv_nsec ^ ((u64)now.tv_sec << 12);

    if (!BuildSegmentTable())
        return FALSE;

    /* sRuntime itself is a static pointer in .bss — it MUST be
     * protected so that RestoreFromBuffers doesn't overwrite it
     * with a stale mmap address from a captured state. */
    HostSavestateProtectRegion(&sRuntime, sizeof(sRuntime));

    return EnsureHotBuffersAllocated();
}

void HostSavestateShutdown(void)
{
}

bool8 HostSavestateHasHotState(void)
{
    return sRuntime != NULL && sRuntime->hotValid;
}

bool8 HostSavestateCaptureHot(void)
{
    ClearError();

    if (!HostSavestateInit())
        return FALSE;

    if (!CaptureIntoBuffers(sRuntime->hotBuffers))
        return FALSE;

    sRuntime->hotValid = TRUE;
    return TRUE;
}

bool8 HostSavestateRestoreHot(void)
{
    ClearError();

    if (sRuntime == NULL || !sRuntime->hotValid)
    {
        SetError("savestate: no hot state is available");
        return FALSE;
    }

    RestoreFromBuffers(sRuntime->hotBuffers);
    return TRUE;
}

bool8 HostSavestateSaveToFile(const char *path)
{
    struct HostSavestateFileHeader header;
    FILE *file = NULL;
    char tmpPath[4096];
    u32 i;
    bool8 ok = FALSE;

    ClearError();

    if (path == NULL || path[0] == '\0')
    {
        SetError("savestate: save path is empty");
        return FALSE;
    }

    if (!HostSavestateCaptureHot())
        return FALSE;

    if (!BuildTempPath(tmpPath, sizeof(tmpPath), path))
    {
        SetError("savestate: temp path too long");
        return FALSE;
    }

    file = fopen(tmpPath, "wb");
    if (file == NULL)
    {
        SetError("savestate: fopen(%s) failed: %s", tmpPath, strerror(errno));
        return FALSE;
    }

    memset(&header, 0, sizeof(header));
    header.magic = HOST_SAVESTATE_MAGIC;
    header.version = HOST_SAVESTATE_VERSION;
    header.sessionId = sRuntime->sessionId;
    header.segmentCount = sRuntime->segmentCount;
    header.buildFingerprint = ComputeBuildFingerprint();

    if (fwrite(&header, sizeof(header), 1, file) != 1)
        goto cleanup;

    for (i = 0; i < sRuntime->segmentCount; i++)
    {
        struct HostSavestateFileSegment segment = {
            .base = sRuntime->segments[i].base,
            .size = sRuntime->segments[i].size,
        };

        if (fwrite(&segment, sizeof(segment), 1, file) != 1)
            goto cleanup;
    }

    for (i = 0; i < sRuntime->segmentCount; i++)
    {
        if (fwrite(sRuntime->hotBuffers[i].data, 1, sRuntime->hotBuffers[i].size, file) != sRuntime->hotBuffers[i].size)
            goto cleanup;
    }

    if (fclose(file) != 0)
    {
        file = NULL;
        goto cleanup;
    }
    file = NULL;

    if (rename(tmpPath, path) != 0)
    {
        SetError("savestate: rename(%s -> %s) failed: %s", tmpPath, path, strerror(errno));
        goto cleanup;
    }
    tmpPath[0] = '\0';

    ok = TRUE;

cleanup:
    if (file != NULL)
        fclose(file);
    if (!ok)
    {
        unlink(tmpPath);
        if (sRuntime->lastError[0] == '\0')
            SetError("savestate: failed writing %s", path);
    }
    return ok;
}

bool8 HostSavestateLoadFromFile(const char *path)
{
    struct HostSavestateFileHeader header;
    FILE *file;
    u32 i;

    ClearError();

    if (path == NULL || path[0] == '\0')
    {
        SetError("savestate: load path is empty");
        return FALSE;
    }

    if (!HostSavestateInit())
        return FALSE;

    file = fopen(path, "rb");
    if (file == NULL)
    {
        SetError("savestate: fopen(%s) failed: %s", path, strerror(errno));
        return FALSE;
    }

    if (fread(&header, sizeof(header), 1, file) != 1)
    {
        SetError("savestate: could not read header from %s", path);
        fclose(file);
        return FALSE;
    }

    if (header.magic != HOST_SAVESTATE_MAGIC)
    {
        SetError("savestate: %s is not a valid savestate file", path);
        fclose(file);
        return FALSE;
    }

    if (header.version < 1 || header.version > HOST_SAVESTATE_VERSION)
    {
        SetError("savestate: %s has unsupported version %u (need %u)",
                 path, header.version, HOST_SAVESTATE_VERSION);
        fclose(file);
        return FALSE;
    }

    /* v1 savestates lack a build fingerprint — always reject them
     * since function pointers are almost certainly stale after rebuild. */
    if (header.version < 2)
    {
        SetError("savestate: %s is a v1 savestate from a prior build — "
                 "incompatible after recompile (re-save with current build)", path);
        fclose(file);
        return FALSE;
    }

    if (header.buildFingerprint != ComputeBuildFingerprint())
    {
        SetError("savestate: %s was created by a different build "
                 "(fingerprint 0x%08X vs current 0x%08X) — re-save with current build",
                 path, header.buildFingerprint, ComputeBuildFingerprint());
        fclose(file);
        return FALSE;
    }

    if (header.segmentCount != sRuntime->segmentCount)
    {
        SetError("savestate: %s has a mismatched segment count", path);
        fclose(file);
        return FALSE;
    }

    if (!EnsureHotBuffersAllocated())
    {
        fclose(file);
        return FALSE;
    }

    for (i = 0; i < sRuntime->segmentCount; i++)
    {
        struct HostSavestateFileSegment segment;

        if (fread(&segment, sizeof(segment), 1, file) != 1)
        {
            SetError("savestate: could not read segment table from %s", path);
            fclose(file);
            return FALSE;
        }

        /* Tolerate size differences for .data/.bss (segments 0,1).
         * GBA regions (segments 2+) must match exactly. */
        sRuntime->fileSavedSizes[i] = (size_t)segment.size;
        if (i >= 2 && segment.size != sRuntime->segments[i].size)
        {
            SetError("savestate: %s GBA region %u size mismatch (saved=%llu, current=%zu)",
                     path, i, (unsigned long long)segment.size, sRuntime->segments[i].size);
            fclose(file);
            return FALSE;
        }
    }

    for (i = 0; i < sRuntime->segmentCount; i++)
    {
        size_t savedSize = sRuntime->fileSavedSizes[i];
        size_t currentSize = sRuntime->hotBuffers[i].size;
        size_t copySize = (savedSize < currentSize) ? savedSize : currentSize;

        /* Read the portion we'll use */
        if (fread(sRuntime->hotBuffers[i].data, 1, copySize, file) != copySize)
        {
            SetError("savestate: could not read segment %u from %s", i, path);
            fclose(file);
            return FALSE;
        }

        /* Skip any extra bytes from a larger saved segment */
        if (savedSize > currentSize)
        {
            if (fseek(file, (long)(savedSize - currentSize), SEEK_CUR) != 0)
            {
                SetError("savestate: could not skip excess bytes in segment %u of %s", i, path);
                fclose(file);
                return FALSE;
            }
        }
    }

    fclose(file);
    sRuntime->hotValid = TRUE;
    RestoreFromBuffers(sRuntime->hotBuffers);
    return TRUE;
}

void HostSavestateProtectRegion(void *addr, size_t size)
{
    if (sRuntime == NULL && !HostSavestateInit())
        return;

    if (sRuntime->protectCount < HOST_SAVESTATE_MAX_PROTECT)
    {
        sRuntime->protect[sRuntime->protectCount].addr = (uintptr_t)addr;
        sRuntime->protect[sRuntime->protectCount].size = size;
        sRuntime->protectCount++;
    }
}

const char *HostSavestateGetLastError(void)
{
    if (sRuntime == NULL || sRuntime->lastError[0] == '\0')
        return "savestate: no error";

    return sRuntime->lastError;
}
