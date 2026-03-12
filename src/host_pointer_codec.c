#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "host_pointer_codec.h"

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

#define PFR_PATCH_TOKEN_BASE 0xF0000000u
#define PFR_ALIAS_BASE_START ((uintptr_t)0x10000000u)
#define PFR_ALIAS_BASE_LIMIT ((uintptr_t)0x70000000u)

struct BlobAlias
{
    const u8 *blob;
    size_t size;
    u8 *alias;
};

static struct BlobAlias *sBlobAliases;
static size_t sBlobAliasCount;
static size_t sBlobAliasCapacity;
static const void **sTokenTargets;
static u32 sTokenCount;
static size_t sTokenCapacity;
static uintptr_t sNextAliasBase = PFR_ALIAS_BASE_START;

static size_t RoundUpToPage(size_t size)
{
    const size_t pageSize = 4096;
    return (size + pageSize - 1) & ~(pageSize - 1);
}

static void EnsureBlobAliasCapacity(void)
{
    size_t newCapacity;
    struct BlobAlias *newEntries;

    if (sBlobAliasCount < sBlobAliasCapacity)
        return;

    newCapacity = sBlobAliasCapacity == 0 ? 8 : sBlobAliasCapacity * 2;
    newEntries = realloc(sBlobAliases, newCapacity * sizeof(*newEntries));
    if (newEntries == NULL)
    {
        fprintf(stderr, "host_pointer_codec: failed to grow blob alias table\n");
        abort();
    }

    sBlobAliases = newEntries;
    sBlobAliasCapacity = newCapacity;
}

static void EnsureTokenCapacity(void)
{
    size_t newCapacity;
    const void **newEntries;

    if ((size_t)sTokenCount < sTokenCapacity)
        return;

    newCapacity = sTokenCapacity == 0 ? 1024 : sTokenCapacity * 2;
    newEntries = realloc(sTokenTargets, newCapacity * sizeof(*newEntries));
    if (newEntries == NULL)
    {
        fprintf(stderr, "host_pointer_codec: failed to grow token table\n");
        abort();
    }

    sTokenTargets = newEntries;
    sTokenCapacity = newCapacity;
}

static void WriteLe32(u8 *dst, u32 value)
{
    dst[0] = (u8)value;
    dst[1] = (u8)(value >> 8);
    dst[2] = (u8)(value >> 16);
    dst[3] = (u8)(value >> 24);
}

static u32 ReadLe32(const u8 *src)
{
    return (u32)src[0]
         | ((u32)src[1] << 8)
         | ((u32)src[2] << 16)
         | ((u32)src[3] << 24);
}

static void *MapLowAlias(size_t size)
{
    size_t alignedSize = RoundUpToPage(size);
    uintptr_t base = sNextAliasBase;

    while (base + alignedSize <= PFR_ALIAS_BASE_LIMIT)
    {
        void *mapped = mmap((void *)base,
                            alignedSize,
                            PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE,
                            -1,
                            0);
        if (mapped != MAP_FAILED)
        {
            sNextAliasBase = base + alignedSize + 0x1000u;
            return mapped;
        }

        if (errno != EEXIST)
        {
            fprintf(stderr,
                    "host_pointer_codec: failed to map alias at 0x%08lx (%zu bytes): %s\n",
                    (unsigned long)base,
                    alignedSize,
                    strerror(errno));
            abort();
        }

        base += alignedSize + 0x1000u;
    }

    fprintf(stderr, "host_pointer_codec: exhausted low-address alias space\n");
    abort();
}

static struct BlobAlias *FindBlobAlias(const void *blobBase, size_t blobSize)
{
    size_t i;

    for (i = 0; i < sBlobAliasCount; i++)
    {
        if (sBlobAliases[i].blob == blobBase && sBlobAliases[i].size == blobSize)
            return &sBlobAliases[i];
    }

    return NULL;
}

static struct BlobAlias *EnsureBlobAlias(const void *blobBase, size_t blobSize)
{
    struct BlobAlias *entry = FindBlobAlias(blobBase, blobSize);

    if (entry != NULL)
        return entry;

    EnsureBlobAliasCapacity();
    entry = &sBlobAliases[sBlobAliasCount++];
    entry->blob = blobBase;
    entry->size = blobSize;
    entry->alias = MapLowAlias(blobSize);
    memcpy(entry->alias, blobBase, blobSize);
    return entry;
}

u32 HostEncodePointerToken(const void *target)
{
    uintptr_t raw = (uintptr_t)target;

    if (raw <= (uintptr_t)(PFR_PATCH_TOKEN_BASE - 1u))
        return (u32)raw;

    EnsureTokenCapacity();
    sTokenTargets[sTokenCount] = target;
    return PFR_PATCH_TOKEN_BASE + sTokenCount++;
}

void HostPointerCodecReset(void)
{
    sTokenCount = 0;
}

const void *HostDecodePointerToken(u32 encoded)
{
    if (encoded < PFR_PATCH_TOKEN_BASE)
        return (const void *)(uintptr_t)encoded;

    encoded -= PFR_PATCH_TOKEN_BASE;
    if (encoded >= sTokenCount)
    {
        fprintf(stderr, "host_pointer_codec: invalid pointer token %u\n", encoded);
        abort();
    }

    return sTokenTargets[encoded];
}

void HostStorePointerHalfwords(u16 *dst, const void *value)
{
    const u32 encoded = HostEncodePointerToken(value);
    dst[0] = (u16)encoded;
    dst[1] = (u16)(encoded >> 16);
}

void *HostLoadPointerHalfwords(const u16 *src)
{
    const u8 *bytes = (const u8 *)src;
    return (void *)HostDecodePointerToken(ReadLe32(bytes));
}

const void *HostDecodeScriptPointer(u32 encoded)
{
    return HostDecodePointerToken(encoded);
}

void HostWritePatchedPointer(const void *blobBase, size_t blobSize, u8 *dst, const void *target)
{
    const u8 *blobStart = blobBase;
    const u8 *blobEnd = blobStart + blobSize;
    const u8 *targetBytes = target;
    struct BlobAlias *alias = EnsureBlobAlias(blobBase, blobSize);
    size_t offset;
    u32 encoded;

    assert(dst >= blobStart);
    assert(dst + sizeof(u32) <= blobEnd);

    offset = (size_t)(dst - blobStart);
    if (targetBytes >= blobStart && targetBytes < blobEnd)
        encoded = (u32)(uintptr_t)(alias->alias + (size_t)(targetBytes - blobStart));
    else
        encoded = HostEncodePointerToken(target);

    WriteLe32(dst, encoded);
    WriteLe32(alias->alias + offset, encoded);
}
