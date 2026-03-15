#ifndef GUARD_GBA_GBA_H
#define GUARD_GBA_GBA_H

#include <string.h>
#include <stdint.h>

/*
 * pokefirered-native: Dynamic GBA memory addresses
 *
 * The upstream defines.h sets EWRAM_START, IWRAM_START, PLTT, VRAM, OAM
 * as compile-time constants (fixed physical GBA addresses like 0x02000000).
 * For multi-instance support via dlmopen, each game instance needs its own
 * mmap'd memory at dynamic addresses.
 *
 * Strategy: include upstream headers first, then #undef and redefine the
 * address constants as extern globals. HostMemoryInit() sets them from mmap().
 * All existing code (REG_DISPCNT, BG_CHAR_ADDR, etc.) works unchanged
 * because the macros expand to (base + offset) regardless of whether
 * base is a literal or a variable.
 */

/* Include upstream headers — these set the original #defines */
#include "../../../../pokefirered/include/gba/defines.h"
#include "../../../../pokefirered/include/gba/io_reg.h"
#include "../../../../pokefirered/include/gba/types.h"
#include "../../../../pokefirered/include/gba/multiboot.h"
#include "../../../../pokefirered/include/gba/syscall.h"
#include "macro.h"
#include "../../../../pokefirered/include/gba/isagbprint.h"

/* ── Override fixed addresses with dynamic globals ── */

/* Primary region bases */
#undef EWRAM_START
#undef EWRAM_END
#undef IWRAM_START
#undef IWRAM_END
#undef PLTT
#undef BG_PLTT
#undef OBJ_PLTT
#undef VRAM
#undef BG_VRAM
#undef OBJ_VRAM0
#undef OBJ_VRAM1
#undef OAM
#undef REG_BASE

/* Derived macros that use the base addresses */
#undef BG_CHAR_ADDR
#undef BG_SCREEN_ADDR
#undef BG_TILE_ADDR

/* IWRAM-resident special locations */
#undef SOUND_INFO_PTR
#undef INTR_CHECK
#undef INTR_VECTOR

/* Extern globals — defined in host_memory.c, one copy per dlmopen namespace */
extern uintptr_t pfr_ewram_base;
extern uintptr_t pfr_iwram_base;
extern uintptr_t pfr_reg_base;
extern uintptr_t pfr_pltt_base;
extern uintptr_t pfr_vram_base;
extern uintptr_t pfr_oam_base;

/* Redefine as runtime values */
#define EWRAM_START  pfr_ewram_base
#define EWRAM_END    (pfr_ewram_base + 0x40000)
#define IWRAM_START  pfr_iwram_base
#define IWRAM_END    (pfr_iwram_base + 0x8000)

#define REG_BASE     pfr_reg_base

#define PLTT         pfr_pltt_base
#define BG_PLTT      pfr_pltt_base
#define OBJ_PLTT     (pfr_pltt_base + 0x200)

#define VRAM         pfr_vram_base
#define BG_VRAM      pfr_vram_base
#define OBJ_VRAM0    (void *)(pfr_vram_base + 0x10000)
#define OBJ_VRAM1    (void *)(pfr_vram_base + 0x14000)

#define OAM          pfr_oam_base

#define BG_CHAR_ADDR(n)   (void *)(pfr_vram_base + (0x4000u * (n)))
#define BG_SCREEN_ADDR(n) (void *)(pfr_vram_base + (0x800u * (n)))
#define BG_TILE_ADDR(n)   (void *)(pfr_vram_base + (0x80u * (n)))

/* IWRAM-resident special addresses (end of IWRAM) */
#define SOUND_INFO_PTR (*(struct SoundInfo **)(pfr_iwram_base + 0x7FF0))
#define INTR_CHECK     (*(u16 *)(pfr_iwram_base + 0x7FF8))
/* Use vu32 on 64-bit to avoid overrunning the mapped region */
#define INTR_VECTOR    (*(vu32 *)(pfr_iwram_base + 0x7FFC))

#endif /* GUARD_GBA_GBA_H */
