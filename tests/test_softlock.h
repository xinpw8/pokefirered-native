/* test_softlock.h -- Softlock / stability tests via random walks.
 * Uses TEST_ASSERT / TEST_ASSERT_EQ macros and TestEntry registration. */
#ifndef TEST_SOFTLOCK_H
#define TEST_SOFTLOCK_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ---------- helpers ---------------------------------------------------- */

/* Simple seeded PRNG (xorshift32) to avoid depending on rand() seeding */
static uint32_t softlock_rng_state = 12345;

static uint32_t softlock_rand(void) {
    uint32_t x = softlock_rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    softlock_rng_state = x;
    return x;
}

/* Count unique (map, x, y) positions in a simple hash set.
 * Packs map_id:x:y into a uint64_t and uses linear probing. */
#define SOFTLOCK_HASHSET_SIZE 4096

static uint64_t softlock_positions[SOFTLOCK_HASHSET_SIZE];
static int softlock_position_count;

static void softlock_positions_clear(void) {
    memset(softlock_positions, 0xFF, sizeof(softlock_positions));
    softlock_position_count = 0;
}

static void softlock_positions_insert(uint16_t map_id, int16_t x, int16_t y) {
    uint64_t key = ((uint64_t)map_id << 32) | ((uint64_t)(uint16_t)x << 16) | (uint16_t)y;
    uint32_t idx = (uint32_t)(key * 2654435761ULL) % SOFTLOCK_HASHSET_SIZE;
    for (int i = 0; i < SOFTLOCK_HASHSET_SIZE; i++) {
        uint32_t slot = (idx + i) % SOFTLOCK_HASHSET_SIZE;
        if (softlock_positions[slot] == 0xFFFFFFFFFFFFFFFFULL) {
            softlock_positions[slot] = key;
            softlock_position_count++;
            return;
        }
        if (softlock_positions[slot] == key)
            return; /* already present */
    }
}

/* ---------- random walk (basic) ---------------------------------------- */

static int test_softlock_random_walk(void) {
    PfrNativeCore core;
    c_init(&core);
    c_reset(&core, PFR_NATIVE_BOOTSTRAP_PALLET_TOWN, NULL);

    softlock_rng_state = 42;
    softlock_positions_clear();

    /* Record initial position */
    softlock_positions_insert(core.state.current_map,
                               core.state.player_x, core.state.player_y);

    for (int step = 0; step < 10000; step++) {
        /* Random action: 0-8 covers NONE through SELECT */
        PfrNativeAction act = (PfrNativeAction)(softlock_rand() % 9);
        c_step(&core, act);

        /* Record position */
        softlock_positions_insert(core.state.current_map,
                                   core.state.player_x, core.state.player_y);

        /* Verify state is valid */
        TEST_ASSERT(core.state.mode == PFR_NATIVE_MODE_OVERWORLD ||
                    core.state.mode == PFR_NATIVE_MODE_DIALOG,
                    "invalid mode during random walk");

        const PfrNativeMap *cur_map = pfr_native_get_map(core.state.current_map);
        TEST_ASSERT(cur_map != NULL, "current map is NULL during random walk");
    }

    /* Should have visited more than 50 unique positions */
    TEST_ASSERT(softlock_position_count > 50,
                "random walk visited fewer than 50 unique positions");

    return 0;
}

/* ---------- random walk with HM flags ---------------------------------- */

static int test_softlock_with_hm_flags(void) {
    PfrNativeCore core;
    c_init(&core);
    c_reset(&core, PFR_NATIVE_BOOTSTRAP_PALLET_TOWN, NULL);

    /* Grant HMs and badges for maximum exploration */
    core.state.flags = pfrn_flag_set(core.state.flags, PFRN_FLAG_GOT_HM01);
    core.state.flags = pfrn_flag_set(core.state.flags, PFRN_FLAG_GOT_HM03);
    core.state.flags = pfrn_flag_set(core.state.flags, PFRN_FLAG_GOT_HM04);
    for (int b = 0; b < 5; b++)
        core.state.flags = pfrn_flag_set(core.state.flags, PFRN_FLAG_BADGE01_GET + b);

    softlock_rng_state = 7777;
    softlock_positions_clear();

    softlock_positions_insert(core.state.current_map,
                               core.state.player_x, core.state.player_y);

    for (int step = 0; step < 10000; step++) {
        PfrNativeAction act = (PfrNativeAction)(softlock_rand() % 9);
        c_step(&core, act);

        softlock_positions_insert(core.state.current_map,
                                   core.state.player_x, core.state.player_y);

        TEST_ASSERT(core.state.mode == PFR_NATIVE_MODE_OVERWORLD ||
                    core.state.mode == PFR_NATIVE_MODE_DIALOG,
                    "invalid mode during HM random walk");

        const PfrNativeMap *cur_map = pfr_native_get_map(core.state.current_map);
        TEST_ASSERT(cur_map != NULL, "current map is NULL during HM random walk");
    }

    /* With HMs and badges, should explore even more */
    TEST_ASSERT(softlock_position_count > 50,
                "HM random walk visited fewer than 50 unique positions");

    return 0;
}

/* ---------- test registry ---------------------------------------------- */

static const TestEntry softlock_tests[] = {
    { "softlock_random_walk",       test_softlock_random_walk       },
    { "softlock_with_hm_flags",     test_softlock_with_hm_flags     },
    { NULL, NULL }
};

#endif /* TEST_SOFTLOCK_H */
