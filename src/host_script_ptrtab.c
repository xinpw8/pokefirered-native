/*
 * host_script_ptrtab.c — Pointer indirection table for script bytecode.
 *
 * On 64-bit native builds with PIC, script bytecode can't store actual
 * pointers in 4-byte slots (addresses exceed 32 bits). Instead, each
 * pointer gets a 32-bit index into this global table. T1_READ_PTR
 * resolves the index to the real 64-bit address.
 */
#include "global.h"
#include <stdio.h>

extern void HostSavestateProtectRegion(void *addr, size_t size);

#define HOST_SCRIPT_PTRTAB_MAX 32768
static const u8 *sScriptPtrTab[HOST_SCRIPT_PTRTAB_MAX];
static u32 sScriptPtrTabNext = 1; /* 0 = NULL */

void HostScriptPtrTabProtect(void)
{
    HostSavestateProtectRegion(sScriptPtrTab, sizeof(sScriptPtrTab));
    HostSavestateProtectRegion(&sScriptPtrTabNext, sizeof(sScriptPtrTabNext));
}

void HostScriptPtrTabReset(void)
{
    /* Zero the entire table so stale entries from a previous population
     * (or from a savestate restore that overwrote .bss) can't survive
     * as garbage pointers.  Without this, an un-re-patched bytecode
     * index could resolve to a truncated 64-bit pointer from a prior
     * session/build and cause SIGSEGV. */
    memset(sScriptPtrTab, 0, sizeof(sScriptPtrTab));
    sScriptPtrTabNext = 1;
}

u32 HostScriptPtrTabRegister(const u8 *ptr)
{
    u32 idx = sScriptPtrTabNext++;
    if (idx >= HOST_SCRIPT_PTRTAB_MAX) {
        fprintf(stderr, "FATAL: script pointer table overflow at %u\n", idx);
        __builtin_trap();
    }
    sScriptPtrTab[idx] = ptr;
    return idx;
}

const u8 *HostScriptPtrLookup(u32 index)
{
    const u8 *result;
    if (index == 0)
        return NULL;
    if (index >= HOST_SCRIPT_PTRTAB_MAX || index >= sScriptPtrTabNext) {
        fprintf(stderr, "FATAL: HostScriptPtrLookup: invalid index %u (max=%u)\n",
                index, sScriptPtrTabNext);
        __builtin_trap();
    }
    result = sScriptPtrTab[index];
    /* Catch bad table entries before they cause SIGSEGV elsewhere.
     * Valid native pointers on aarch64 are well above 0x10000. */
    if (result != NULL && (uintptr_t)result < 0x10000) {
        fprintf(stderr, "FATAL: HostScriptPtrLookup: index %u -> bad ptr %p "
                "(table has %u entries)\n",
                index, (const void *)result, sScriptPtrTabNext);
        __builtin_trap();
    }
    return result;
}
