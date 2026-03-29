/*
 * test_pfr_engine.c -- Main test runner for the pfr_native engine.
 *
 * Includes pfr_native.c directly so we can exercise static functions.
 * Compile with:
 *   cc -I../src -I../build -o test_pfr_engine test_pfr_engine.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Include engine source directly (has static functions we need to test) */
#include "../src/pfr_native.c"
/* Include generated data tables */
#include "../build/pfr_native_data.c"

/* ---------- Test framework macros ---------- */

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            printf("  FAIL: %s (line %d): %s\n", __func__, __LINE__, msg); \
            return 1; \
        } \
    } while (0)

#define TEST_ASSERT_EQ(a, b, msg) \
    do { \
        if ((a) != (b)) { \
            printf("  FAIL: %s (line %d): %s (got %d, want %d)\n", \
                   __func__, __LINE__, msg, (int)(a), (int)(b)); \
            return 1; \
        } \
    } while (0)

/* ---------- Test entry ---------- */

typedef struct {
    const char *name;
    int (*fn)(void);
} TestEntry;

/* ---------- Category headers ---------- */

#include "test_bootstrap.h"
#include "test_map_data.h"
#include "test_warps.h"
#include "test_movement.h"
#include "test_scripts.h"
#include "test_npc_visibility.h"
#include "test_dialog.h"
#include "test_state.h"
#include "test_items.h"
#include "test_hm.h"
#include "test_puzzles.h"
#include "test_battle.h"
#include "test_pokemon.h"
#include "test_progression.h"
#include "test_softlock.h"
#include "test_bounds.h"

/* ---------- Category runner ---------- */

static void run_category(const char *name, const TestEntry *tests, size_t count,
                         int *pass, int *fail, const char *filter)
{
    size_t i;

    if (filter && strcmp(filter, name) != 0)
        return;

    /* Skip stub arrays (single NULL sentinel entry) */
    if (count == 1 && tests[0].fn == NULL)
        return;

    printf("\n=== %s ===\n", name);
    for (i = 0; i < count; i++) {
        if (tests[i].fn == NULL)
            continue;
        if (tests[i].fn() == 0) {
            printf("  PASS: %s\n", tests[i].name);
            (*pass)++;
        } else {
            (*fail)++;
            printf("  FAILED: %s\n", tests[i].name);
        }
    }
}

#define RUN_CATEGORY(name, fn_array) \
    run_category(name, fn_array, sizeof(fn_array) / sizeof(fn_array[0]), \
                 &pass, &fail, filter)

/* ---------- main ---------- */

int main(int argc, char **argv)
{
    const char *filter = argc > 1 ? argv[1] : NULL;
    int pass = 0, fail = 0;
    clock_t start = clock();

    RUN_CATEGORY("bootstrap", bootstrap_tests);
    RUN_CATEGORY("map_data", map_data_tests);
    RUN_CATEGORY("warps", warps_tests);
    RUN_CATEGORY("movement", movement_tests);
    RUN_CATEGORY("scripts", scripts_tests);
    RUN_CATEGORY("npc_visibility", npc_visibility_tests);
    RUN_CATEGORY("dialog", dialog_tests);
    RUN_CATEGORY("state", state_tests);
    RUN_CATEGORY("items", item_tests);
    RUN_CATEGORY("hm", hm_tests);
    RUN_CATEGORY("puzzles", puzzle_tests);
    RUN_CATEGORY("battle", battle_tests);
    RUN_CATEGORY("pokemon", pokemon_tests);
    RUN_CATEGORY("progression", progression_tests);
    RUN_CATEGORY("softlock", softlock_tests);
    RUN_CATEGORY("bounds", bounds_tests);

    {
        double elapsed = (double)(clock() - start) / CLOCKS_PER_SEC;
        printf("\n%d passed, %d failed (%.2fs)\n", pass, fail, elapsed);
    }
    return fail > 0 ? 1 : 0;
}
