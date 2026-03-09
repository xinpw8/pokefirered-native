/*
 * upstream_stubs.c
 *
 * Minimal stubs for symbols that have no upstream C implementation in the
 * native build (e.g. assembly-defined data, hardware-specific RFU library).
 *
 * As more upstream code gets compiled natively, stubs should be removed.
 * Sound: m4a.c + host_sound_mixer.c
 * Flash: host_flash.c
 * Save:  save.c + load_save.c (upstream)
 * Link:  link.c (upstream)
 */

#include "global.h"
#include "host_runtime_stubs.h"
#include "link.h"
#include "play_time.h"
#include "link_rfu.h"
#include "librfu.h"
#include "malloc.h"
#include "sprite.h"

/* ---- Global data that lives in assembly or in files not yet compiled ---- */

/* Heap lives here because main.c's definition might clash with our host_crt0 */
EWRAM_DATA u8 gHeap[HEAP_SIZE] = {0};

/* RNG2 used by battle system */
COMMON_DATA u32 gRng2Value = 0;

/* gSTWIStatus now from librfu_stwi.c */

/* Interrupt vector table — lives in assembly crt0.s */
u32 intr_main[0x200] = {
    [0] = 0x11223344,
    [1] = 0x55667788,
    [2] = 0x99AABBCC,
    [0x1FF] = 0xA5A5A5A5,
};

/* ---- Host stub tracking counters ---- */

u32 gHostStubMapMusicMainCalls = 0;
u32 gHostStubSoftResetCalls = 0;

void HostStubReset(void)
{
    gHostStubMapMusicMainCalls = 0;
    gHostStubSoftResetCalls = 0;
}

/* ---- Upstream function wrappers ---- */

/* pfr_upstream_play_time compiles play_time.c with
   PlayTimeCounter_Update renamed to UpstreamPlayTimeCounter_Update,
   allowing host interception. This wrapper provides the real name. */
void UpstreamPlayTimeCounter_Update(void);
void PlayTimeCounter_Update(void) { UpstreamPlayTimeCounter_Update(); }

/* ---- Hardware/assembly stubs (no upstream C source) ---- */

/* InitRFU now from link_rfu_2.c */
/* rfu_REQ_stopMode, rfu_waitREQComplete now from librfu_rfu.c */

/* EventScript_ResetAllMapFlags — now in upstream_event_scripts.c (real bytecode) */
