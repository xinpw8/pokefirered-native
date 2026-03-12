#ifndef GUARD_GBA_GBA_H
#define GUARD_GBA_GBA_H

#include <string.h>

#include "../../../third_party/pokefirered/include/gba/defines.h"
#include "../../../third_party/pokefirered/include/gba/io_reg.h"
#include "../../../third_party/pokefirered/include/gba/types.h"
#include "../../../third_party/pokefirered/include/gba/multiboot.h"
#include "../../../third_party/pokefirered/include/gba/syscall.h"
#include "macro.h"
#include "../../../third_party/pokefirered/include/gba/isagbprint.h"

// On GBA this is a 32-bit vector slot at the end of IWRAM.
// Using `void **` on aarch64 widens the store to 8 bytes and overruns the mapped range.
#undef INTR_VECTOR
#define INTR_VECTOR (*(vu32 *)0x3007FFC)

#endif
