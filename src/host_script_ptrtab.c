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

#define HOST_SCRIPT_PTRTAB_MAX 32768
static const u8 *sScriptPtrTab[HOST_SCRIPT_PTRTAB_MAX];
static u32 sScriptPtrTabNext = 1; /* 0 = NULL */

void HostScriptPtrTabReset(void)
{
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
    if (index == 0)
        return NULL;
    if (index >= HOST_SCRIPT_PTRTAB_MAX || index >= sScriptPtrTabNext) {
        fprintf(stderr, "FATAL: HostScriptPtrLookup: invalid index %u (max=%u)\n",
                index, sScriptPtrTabNext);
        __builtin_trap();
    }
    return sScriptPtrTab[index];
}
