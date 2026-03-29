/*
 * game_ctx.c -- GameCtx allocation / deallocation
 *
 * Provides per-instance game state for multi-env parallelism.
 * Each PfrEnv owns a heap-allocated GameCtx.
 * The __thread pointer g_ctx is set before each c_step().
 */

/*
 * Include all type-defining headers needed by game_ctx.h's struct members.
 * game_ctx.h itself does NOT include any upstream headers, so we must
 * provide all the types it uses here.
 */
#include "global.h"
#include "gflib.h"
#include "battle.h"
#include "sprite.h"
#include "palette.h"
#include "scanline_effect.h"
#include "link.h"
#include "save.h"
#include "pokemon_storage_system.h"
#include "quest_log.h"
/* system allocator, not game heap */
#undef calloc
#undef malloc
#undef free
#include "string_util.h"
#include "trade.h"
#include "field_weather.h"
#include "event_object_movement.h"
#include "overworld.h"
#include "fieldmap.h"
#include "help_system.h"
#include "item.h"
#include "shop.h"
#include "sound.h"
#include "party_menu.h"
#include "window.h"
#include "trainer_card.h"
#include "union_room.h"
#include "battle_controllers.h"

/* Now include game_ctx.h which defines the GameCtx struct */
#include "game_ctx.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

/* Thread-local pointer to the active GameCtx for this thread */
__thread GameCtx *g_ctx = NULL;

/* Use mmap with guard pages to isolate GameCtx from the system heap.
 * This prevents game engine buffer overflows from corrupting Python's
 * memory allocator metadata. Layout:
 *   [guard page (PROT_NONE)] [GameCtx data (RW)] [guard page (PROT_NONE)]
 */
GameCtx *game_ctx_alloc(void)
{
    long page_size = sysconf(_SC_PAGESIZE);
    size_t ctx_size = sizeof(GameCtx);
    /* Round up to page boundary */
    size_t aligned_size = (ctx_size + page_size - 1) & ~(page_size - 1);
    size_t total_size = aligned_size + 2 * page_size; /* guard pages on both sides */

    void *base = mmap(NULL, total_size, PROT_NONE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED) {
// [DEBUG-STRIPPED]         fprintf(stderr, "game_ctx_alloc: mmap failed for %zu bytes\n", total_size);
        return NULL;
    }

    /* Make the middle region read-write */
    void *data_start = (char *)base + page_size;
    if (mprotect(data_start, aligned_size, PROT_READ | PROT_WRITE) != 0) {
// [DEBUG-STRIPPED]         fprintf(stderr, "game_ctx_alloc: mprotect failed\n");
        munmap(base, total_size);
        return NULL;
    }

    GameCtx *ctx = (GameCtx *)data_start;
    memset(ctx, 0, ctx_size);
// [DEBUG-STRIPPED]     fprintf(stderr, "game_ctx_alloc: mmap'd %zu bytes at %p (guard pages at %p and %p)\n",
// [DEBUG-STRIPPED]             ctx_size, (void*)ctx, base, (char*)data_start + aligned_size);
    return ctx;
}

void game_ctx_free(GameCtx *ctx)
{
    if (ctx) {
        long page_size = sysconf(_SC_PAGESIZE);
        size_t ctx_size = sizeof(GameCtx);
        size_t aligned_size = (ctx_size + page_size - 1) & ~(page_size - 1);
        size_t total_size = aligned_size + 2 * page_size;
        void *base = (char *)ctx - page_size;
        munmap(base, total_size);
    }
}

size_t game_ctx_sizeof(void)
{
    return sizeof(GameCtx);
}
