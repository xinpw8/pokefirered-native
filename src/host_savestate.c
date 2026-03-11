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
#define HOST_SAVESTATE_VERSION 1u

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

struct HostSavestateRuntime
{
    u64 sessionId;
    u32 segmentCount;
    bool8 hotValid;
    struct HostSavestateSegment *segments;
    struct HostSavestateBuffer *hotBuffers;
    char lastError[256];
};

struct HostSavestateFileHeader
{
    u32 magic;
    u32 version;
    u64 sessionId;
    u32 segmentCount;
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

static const struct HostSavestateSegment sFixedHostRegions[] = {
    { EWRAM_START, EWRAM_END - EWRAM_START },
    { IWRAM_START, IWRAM_END - IWRAM_START },
    { REG_BASE, 0x1000 },
    { PLTT, PLTT_SIZE },
    { VRAM, VRAM_SIZE },
    { OAM, OAM_SIZE },
};

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
    collection.capacity = ARRAY_COUNT(sFixedHostRegions) + 2;
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

    for (i = 0; i < ARRAY_COUNT(sFixedHostRegions); i++)
    {
        if (!AppendSegment(&collection, sFixedHostRegions[i].base, sFixedHostRegions[i].size))
        {
            SetError("savestate: segment table exhausted");
            return FALSE;
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

    for (i = 0; i < sRuntime->segmentCount; i++)
    {
        memcpy((void *)sRuntime->segments[i].base, buffers[i].data, sRuntime->segments[i].size);
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

    header.magic = HOST_SAVESTATE_MAGIC;
    header.version = HOST_SAVESTATE_VERSION;
    header.sessionId = sRuntime->sessionId;
    header.segmentCount = sRuntime->segmentCount;

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

    if (link(tmpPath, path) != 0)
    {
        if (errno == EEXIST)
            SetError("savestate: refusing to overwrite existing %s", path);
        else
            SetError("savestate: link(%s -> %s) failed: %s", tmpPath, path, strerror(errno));
        goto cleanup;
    }
    if (unlink(tmpPath) != 0)
    {
        SetError("savestate: unlink(%s) failed: %s", tmpPath, strerror(errno));
        unlink(path);
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

    if (header.magic != HOST_SAVESTATE_MAGIC || header.version != HOST_SAVESTATE_VERSION)
    {
        SetError("savestate: %s is not a compatible savestate", path);
        fclose(file);
        return FALSE;
    }

    if (header.sessionId != sRuntime->sessionId)
    {
        SetError("savestate: %s belongs to a different live session", path);
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

        if (segment.base != sRuntime->segments[i].base || segment.size != sRuntime->segments[i].size)
        {
            SetError("savestate: %s does not match the current executable layout", path);
            fclose(file);
            return FALSE;
        }
    }

    for (i = 0; i < sRuntime->segmentCount; i++)
    {
        if (fread(sRuntime->hotBuffers[i].data, 1, sRuntime->hotBuffers[i].size, file) != sRuntime->hotBuffers[i].size)
        {
            SetError("savestate: could not read segment %u from %s", i, path);
            fclose(file);
            return FALSE;
        }
    }

    fclose(file);
    sRuntime->hotValid = TRUE;
    RestoreFromBuffers(sRuntime->hotBuffers);
    return TRUE;
}

const char *HostSavestateGetLastError(void)
{
    if (sRuntime == NULL || sRuntime->lastError[0] == '\0')
        return "savestate: no error";

    return sRuntime->lastError;
}
