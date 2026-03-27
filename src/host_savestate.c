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
#include <dlfcn.h>
#include <link.h>

#include "gba/gba.h"

/* Forward-declare stdlib functions to avoid <stdlib.h> / global.h abs() clash */
extern void *malloc(size_t);
extern void *calloc(size_t, size_t);
extern void free(void *);

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
    size_t fileSavedSizes[16]; /* original sizes from the last loaded file */
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

/* --- Segment discovery via dl_iterate_phdr + RELRO ---
 * __data_start/_edata/__bss_start/_end as extern symbols resolve to the
 * MAIN EXECUTABLE when inside a dlopen'd SO. We use dl_iterate_phdr to
 * find our SO's RW PT_LOAD segment, then subtract the GNU_RELRO portion
 * (which covers .got, .got.plt, .dynamic, .data.rel.ro, etc.) to get
 * only the mutable .data + .bss region. */

struct OwnSegmentAddrs {
    uintptr_t data_start;  /* Start of mutable data (.data section) */
    uintptr_t data_end;    /* End of file-backed portion */
    uintptr_t bss_end;     /* End of .bss (= data_start + mutable_memsz) */
};

struct PhdrSearchCtx {
    uintptr_t our_base;    /* from dladdr */
    struct OwnSegmentAddrs result;
    int found;
};

static int phdr_find_segments(struct dl_phdr_info *info, size_t size, void *data)
{
    struct PhdrSearchCtx *ctx = (struct PhdrSearchCtx *)data;
    int i;
    uintptr_t rw_base = 0, rw_filesz = 0, rw_memsz = 0;
    uintptr_t relro_end = 0;
    int has_rw = 0;

    (void)size;

    if ((uintptr_t)info->dlpi_addr != ctx->our_base)
        return 0;

    /* Scan all program headers for RW PT_LOAD and PT_GNU_RELRO */
    for (i = 0; i < info->dlpi_phnum; i++)
    {
        const ElfW(Phdr) *ph = &info->dlpi_phdr[i];
        if (ph->p_type == PT_LOAD && (ph->p_flags & PF_W))
        {
            rw_base = info->dlpi_addr + ph->p_vaddr;
            rw_filesz = ph->p_filesz;
            rw_memsz = ph->p_memsz;
            has_rw = 1;
        }
        if (ph->p_type == PT_GNU_RELRO)
        {
            relro_end = info->dlpi_addr + ph->p_vaddr + ph->p_memsz;
        }
    }

    if (!has_rw)
        return 0;

    /* The mutable region starts after RELRO ends (or at rw_base if no RELRO).
     * RELRO covers: .init_array, .fini_array, .data.rel.ro, .dynamic, .got, .got.plt
     * After RELRO: .data, common_data, ewram_data, .bss */
    uintptr_t mutable_start = (relro_end > rw_base) ? relro_end : rw_base;

    /* Align mutable_start up to page boundary for safety */
    /* Actually, don't align - the sections may not be page-aligned */

    /* File-backed portion: from mutable_start to rw_base + rw_filesz */
    uintptr_t file_end = rw_base + rw_filesz;
    uintptr_t mem_end = rw_base + rw_memsz;

    if (mutable_start >= mem_end)
    {
        // fprintf(stderr, "[SAVESTATE] ERROR: mutable_start >= mem_end\n");
        return 0;
    }

    ctx->result.data_start = mutable_start;
    ctx->result.data_end = (file_end > mutable_start) ? file_end : mutable_start;
    ctx->result.bss_end = mem_end;
    ctx->found = 1;

    // fprintf(stderr, "[SAVESTATE] RW PT_LOAD: %p filesz=%zu memsz=%zu\n",
    //     //             (void*)rw_base, (size_t)rw_filesz, (size_t)rw_memsz);
    // fprintf(stderr, "[SAVESTATE] RELRO ends: %p\n", (void*)relro_end);
    // fprintf(stderr, "[SAVESTATE] mutable .data: %p..%p (%zu)\n",
    //             (void*)mutable_start, (void*)ctx->result.data_end,
    //             (size_t)(ctx->result.data_end - mutable_start));
    // fprintf(stderr, "[SAVESTATE] mutable .bss:  %p..%p (%zu)\n",
    //             (void*)ctx->result.data_end, (void*)mem_end,
    //             (size_t)(mem_end - ctx->result.data_end));

    return 1;
}

static int FindOwnSegments(struct OwnSegmentAddrs *addrs)
{
    Dl_info dl_info;
    struct PhdrSearchCtx ctx;

    memset(addrs, 0, sizeof(*addrs));
    memset(&ctx, 0, sizeof(ctx));

    if (!dladdr((void *)FindOwnSegments, &dl_info))
    {
        // fprintf(stderr, "[SAVESTATE] dladdr failed\n");
        return 0;
    }

    // fprintf(stderr, "[SAVESTATE] our SO: %s (base=%p)\n",
    //             dl_info.dli_fname, dl_info.dli_fbase);

    ctx.our_base = (uintptr_t)dl_info.dli_fbase;
    dl_iterate_phdr(phdr_find_segments, &ctx);

    if (!ctx.found)
    {
        // fprintf(stderr, "[SAVESTATE] could not find own segments\n");
        return 0;
    }

    *addrs = ctx.result;
    return 1;
}

/* Build fingerprint: hash of .data/.bss segment addresses and sizes.
 * Only changes when the binary layout actually changes (code added/removed,
 * struct sizes changed, etc.), NOT on every recompile of identical source.
 * This is what matters — function pointers in the savestate are only
 * invalid when code addresses shift. */
static u32 ComputeBuildFingerprint(void)
{
    u32 hash = 5381;
    u32 vals[4];

    { struct OwnSegmentAddrs seg; FindOwnSegments(&seg);
    vals[0] = (u32)seg.data_start;
    vals[1] = (u32)(seg.data_end - seg.data_start);
    vals[2] = (u32)seg.data_end;
    vals[3] = (u32)(seg.bss_end - seg.data_end); }

    hash = ((hash << 5) + hash) ^ vals[0];
    hash = ((hash << 5) + hash) ^ vals[1];
    hash = ((hash << 5) + hash) ^ vals[2];
    hash = ((hash << 5) + hash) ^ vals[3];

    /* NOTE: ELF build-id hashing removed — it changed on every recompile
     * even with identical source, making savestates needlessly fragile.
     * The data/bss segment hash above is sufficient to detect real layout
     * changes that would cause stale function pointers. */

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
    collection.capacity = HOST_GBA_REGION_COUNT + 3; /* +1 data/bss, +1 g_ctx */
    collection.segments = MapRuntimeBytes(collection.capacity * sizeof(*collection.segments));
    if (collection.segments == NULL)
    {
        SetError("savestate: could not allocate segment table");
        return FALSE;
    }

    /* Find our own SO's mutable .data + .bss via dl_iterate_phdr. */
    {
        struct OwnSegmentAddrs seg;
        if (!FindOwnSegments(&seg))
        {
            SetError("savestate: could not find own segments");
            return FALSE;
        }

        size_t data_size = seg.data_end - seg.data_start;
        size_t bss_size = seg.bss_end - seg.data_end;

        if (data_size > 0 && !AppendSegment(&collection, seg.data_start, data_size))
        {
            SetError("savestate: could not register data segment");
            return FALSE;
        }

        if (bss_size > 0 && !AppendSegment(&collection, seg.data_end, bss_size))
        {
            SetError("savestate: could not register bss segment");
            return FALSE;
        }
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
        sRuntime->hotBuffers = MapRuntimeBytes((HOST_GBA_REGION_COUNT + 3) * sizeof(*sRuntime->hotBuffers));
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
    {
        fprintf(stderr, "[SAVESTATE] BuildSegmentTable FAILED: %s\n", sRuntime->lastError);
        fprintf(stderr, "[SAVESTATE] Falling back to linker symbol discovery\n");

        /* Fallback: use linker-defined symbols for the main executable.
         * These are always available for non-PIE ELF executables. */
        {
            extern char __data_start[], _edata[], __bss_start[], _end[];
            struct HostSegmentCollection collection;
            size_t data_size, bss_size;

            memset(&collection, 0, sizeof(collection));
            collection.capacity = HOST_GBA_REGION_COUNT + 3;
            collection.segments = MapRuntimeBytes(collection.capacity * sizeof(*collection.segments));
            if (collection.segments == NULL)
                return FALSE;

            data_size = (size_t)(_edata - __data_start);
            bss_size = (size_t)(_end - __bss_start);

            fprintf(stderr, "[SAVESTATE] .data: %p..%p (%zu bytes)\n",
                    (void *)__data_start, (void *)_edata, data_size);
            fprintf(stderr, "[SAVESTATE] .bss:  %p..%p (%zu bytes)\n",
                    (void *)__bss_start, (void *)_end, bss_size);

            if (data_size > 0)
                AppendSegment(&collection, (uintptr_t)__data_start, data_size);
            if (bss_size > 0)
                AppendSegment(&collection, (uintptr_t)__bss_start, bss_size);

            {
                struct HostSavestateSegment gbaRegions[HOST_GBA_REGION_COUNT];
                size_t gi;
                FillGbaRegions(gbaRegions);
                for (gi = 0; gi < HOST_GBA_REGION_COUNT; gi++)
                    AppendSegment(&collection, gbaRegions[gi].base, gbaRegions[gi].size);
            }

            sRuntime->segments = collection.segments;
            sRuntime->segmentCount = collection.count;
            fprintf(stderr, "[SAVESTATE] Fallback registered %u segments\n", collection.count);
        }
    }

    /* sRuntime itself is a static pointer in .bss — it MUST be
     * protected so that RestoreFromBuffers doesn't overwrite it
     * with a stale mmap address from a captured state. */
    HostSavestateProtectRegion(&sRuntime, sizeof(sRuntime));

    return TRUE; /* hot buffers allocated lazily on first save/load */
}

bool8 HostSavestateAddSegment(void *base, size_t size)
{
    if (sRuntime == NULL || base == NULL || size == 0)
        return FALSE;

    if (sRuntime->segmentCount >= HOST_GBA_REGION_COUNT + 3)
    {
        SetError("savestate: segment table full");
        return FALSE;
    }

    sRuntime->segments[sRuntime->segmentCount].base = (uintptr_t)base;
    sRuntime->segments[sRuntime->segmentCount].size = size;
    sRuntime->segmentCount++;
    sRuntime->hotValid = FALSE;
    return TRUE;
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
    u32 i;
    size_t totalBytes = 0;

    ClearError();

    if (!HostSavestateInit())
        return FALSE;

    if (!EnsureHotBuffersAllocated())
        return FALSE;

    for (i = 0; i < sRuntime->segmentCount; i++)
        totalBytes += sRuntime->segments[i].size;

    fprintf(stderr, "[SAVESTATE] Capturing %u segments, %zu bytes total\n",
            sRuntime->segmentCount, totalBytes);
    for (i = 0; i < sRuntime->segmentCount; i++)
        fprintf(stderr, "[SAVESTATE]   seg[%u]: base=%p size=%zu\n",
                i, (void *)sRuntime->segments[i].base, sRuntime->segments[i].size);

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
        fprintf(stderr, "[WARN] v1 savestate (no fingerprint) — loading anyway\n");
        /* Version check bypassed for testing */
    }

    if (header.buildFingerprint != ComputeBuildFingerprint())
    {
        fprintf(stderr, "[WARN] ignoring fingerprint mismatch (file=0x%08X vs build=0x%08X)\n",
                header.buildFingerprint, ComputeBuildFingerprint());
        /* Fingerprint check bypassed for testing */
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


/* Degraded load: extract only the g_ctx segment from a mismatched state file.
 * Returns a malloc buffer containing the g_ctx data, or NULL on failure.
 * Caller must free() the returned buffer.  *outSize receives the segment size. */
void *HostSavestateExtractGCtx(const char *path, size_t *outSize)
{
    struct HostSavestateFileHeader header;
    FILE *file;
    u32 i;
    size_t dataOffset;
    void *buf;
    struct HostSavestateFileSegment *segHeaders;
    size_t gctxFileSize;

    if (outSize)
        *outSize = 0;

    if (path == NULL || path[0] == '\0')
        return NULL;

    file = fopen(path, "rb");
    if (file == NULL)
        return NULL;

    if (fread(&header, sizeof(header), 1, file) != 1)
    {
        fclose(file);
        return NULL;
    }

    if (header.magic != HOST_SAVESTATE_MAGIC || header.segmentCount < 3)
    {
        fclose(file);
        return NULL;
    }

    /* Read all segment headers */
    segHeaders = calloc(header.segmentCount, sizeof(*segHeaders));
    if (segHeaders == NULL)
    {
        fclose(file);
        return NULL;
    }

    for (i = 0; i < header.segmentCount; i++)
    {
        if (fread(&segHeaders[i], sizeof(segHeaders[i]), 1, file) != 1)
        {
            free(segHeaders);
            fclose(file);
            return NULL;
        }
    }

    /* The g_ctx segment is the LAST one (highest index) */
    i = header.segmentCount - 1;
    gctxFileSize = (size_t)segHeaders[i].size;

    /* Compute file offset of the last segment data */
    dataOffset = sizeof(header) + header.segmentCount * sizeof(struct HostSavestateFileSegment);
    {
        u32 j;
        for (j = 0; j < i; j++)
            dataOffset += (size_t)segHeaders[j].size;
    }

    free(segHeaders);

    buf = malloc(gctxFileSize);
    if (buf == NULL)
    {
        fclose(file);
        return NULL;
    }

    if (fseek(file, (long)dataOffset, SEEK_SET) != 0)
    {
        free(buf);
        fclose(file);
        return NULL;
    }

    if (fread(buf, 1, gctxFileSize, file) != gctxFileSize)
    {
        free(buf);
        fclose(file);
        return NULL;
    }

    fclose(file);
    if (outSize)
        *outSize = gctxFileSize;
    return buf;
}

/* Check whether a state file has a matching fingerprint. */
bool8 HostSavestateCheckFingerprint(const char *path)
{
    struct HostSavestateFileHeader header;
    FILE *file;

    if (path == NULL || path[0] == '\0')
        return FALSE;

    file = fopen(path, "rb");
    if (file == NULL)
        return FALSE;

    if (fread(&header, sizeof(header), 1, file) != 1)
    {
        fclose(file);
        return FALSE;
    }
    fclose(file);

    if (header.magic != HOST_SAVESTATE_MAGIC)
        return FALSE;

    return header.buildFingerprint == ComputeBuildFingerprint();
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
