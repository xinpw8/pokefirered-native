/* test_bounds.h -- Bounds checking tests: warps, random walks, connections.
 * Uses TEST_ASSERT / TEST_ASSERT_EQ macros and TestEntry registration. */
#ifndef TEST_BOUNDS_H
#define TEST_BOUNDS_H

#include <stdio.h>
#include <string.h>

/* ---------- helpers ---------------------------------------------------- */

/* Simple seeded PRNG (xorshift32) */
static uint32_t bounds_rng_state = 54321;

static uint32_t bounds_rand(void) {
    uint32_t x = bounds_rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    bounds_rng_state = x;
    return x;
}

/* Verify player is within map bounds */
static int bounds_check_player_in_map(const PfrNativeCore *core) {
    const PfrNativeMap *map = pfr_native_get_map(core->state.current_map);
    if (!map) return 0;
    if (core->state.player_x < 0) return 0;
    if (core->state.player_y < 0) return 0;
    if (core->state.player_x >= (int16_t)map->width) return 0;
    if (core->state.player_y >= (int16_t)map->height) return 0;
    return 1;
}

/* ---------- warp bounds test ------------------------------------------- */

static int test_bounds_after_warps(void) {
    PfrNativeCore core;

    /* For each map, try each supported warp */
    for (size_t m = 0; m < gPfrNativeMapCount; m++) {
        const PfrNativeMap *map = &gPfrNativeMaps[m];

        for (size_t w = 0; w < map->warp_count; w++) {
            const PfrNativeWarp *warp = &map->warps[w];

            /* Only test supported warps */
            if (!warp->supported)
                continue;

            /* Skip warps to invalid destinations */
            if (warp->dest_map == PFR_NATIVE_MAP_INVALID)
                continue;

            c_init(&core);
            c_reset(&core, PFR_NATIVE_BOOTSTRAP_PALLET_TOWN, NULL);

            /* Teleport player to the warp tile */
            pfr_native_reset_to_map(&core, map->map_id, warp->x, warp->y,
                                     PFR_NATIVE_DIR_SOUTH);

            /* Step onto the warp (the reset already puts us there,
             * so just step to trigger warp logic) */
            c_step(&core, PFR_NATIVE_ACTION_DOWN);

            /* If we warped, verify new position is in bounds */
            if (core.last_step.event == PFR_NATIVE_EVENT_WARPED) {
                const PfrNativeMap *dest = pfr_native_get_map(core.state.current_map);
                char msg[256];
                snprintf(msg, sizeof(msg),
                         "warp from map %u (%d,%d) -> map %u: dest map NULL",
                         map->map_id, warp->x, warp->y, core.state.current_map);
                TEST_ASSERT(dest != NULL, msg);

                snprintf(msg, sizeof(msg),
                         "warp from map %u (%d,%d) -> map %u: player at (%d,%d) "
                         "out of bounds [%u x %u]",
                         map->map_id, warp->x, warp->y, core.state.current_map,
                         core.state.player_x, core.state.player_y,
                         dest->width, dest->height);
                TEST_ASSERT(bounds_check_player_in_map(&core), msg);
            }
        }
    }
    return 0;
}

/* ---------- random walk bounds test ------------------------------------ */

static int test_bounds_random_walk(void) {
    PfrNativeCore core;
    c_init(&core);
    c_reset(&core, PFR_NATIVE_BOOTSTRAP_PALLET_TOWN, NULL);

    bounds_rng_state = 99999;

    for (int step = 0; step < 10000; step++) {
        /* Random directional action (UP/DOWN/LEFT/RIGHT/A) */
        PfrNativeAction act = (PfrNativeAction)((bounds_rand() % 5) + 1);
        c_step(&core, act);

        /* After every step, verify player is within map bounds */
        const PfrNativeMap *map = pfr_native_get_map(core.state.current_map);
        char msg[256];
        snprintf(msg, sizeof(msg),
                 "step %d: map %u is NULL", step, core.state.current_map);
        TEST_ASSERT(map != NULL, msg);

        if (map) {
            snprintf(msg, sizeof(msg),
                     "step %d: player_x=%d out of bounds [0, %u)",
                     step, core.state.player_x, map->width);
            TEST_ASSERT(core.state.player_x >= 0, msg);
            TEST_ASSERT(core.state.player_x < (int16_t)map->width, msg);

            snprintf(msg, sizeof(msg),
                     "step %d: player_y=%d out of bounds [0, %u)",
                     step, core.state.player_y, map->height);
            TEST_ASSERT(core.state.player_y >= 0, msg);
            TEST_ASSERT(core.state.player_y < (int16_t)map->height, msg);
        }
    }
    return 0;
}

/* ---------- connection crossing bounds test ----------------------------- */

static int test_bounds_connection_crossing(void) {
    PfrNativeCore core;

    /* For each map with connections, walk off the edge and verify bounds */
    for (size_t m = 0; m < gPfrNativeMapCount; m++) {
        const PfrNativeMap *map = &gPfrNativeMaps[m];

        if (map->connection_count == 0)
            continue;

        for (size_t c = 0; c < map->connection_count; c++) {
            const PfrNativeConnection *conn = &map->connections[c];

            /* Skip dive/emerge/unknown connections */
            if (conn->direction > PFR_NATIVE_CONN_EAST)
                continue;

            c_init(&core);
            c_reset(&core, PFR_NATIVE_BOOTSTRAP_PALLET_TOWN, NULL);

            /* Position player at the map edge corresponding to the connection */
            int16_t px = 0, py = 0;
            PfrNativeDirection dir = PFR_NATIVE_DIR_NORTH;
            PfrNativeAction walk_act = PFR_NATIVE_ACTION_UP;

            switch (conn->direction) {
            case PFR_NATIVE_CONN_NORTH:
                px = (int16_t)(map->width / 2);
                py = 0;
                dir = PFR_NATIVE_DIR_NORTH;
                walk_act = PFR_NATIVE_ACTION_UP;
                break;
            case PFR_NATIVE_CONN_SOUTH:
                px = (int16_t)(map->width / 2);
                py = (int16_t)(map->height - 1);
                dir = PFR_NATIVE_DIR_SOUTH;
                walk_act = PFR_NATIVE_ACTION_DOWN;
                break;
            case PFR_NATIVE_CONN_WEST:
                px = 0;
                py = (int16_t)(map->height / 2);
                dir = PFR_NATIVE_DIR_WEST;
                walk_act = PFR_NATIVE_ACTION_LEFT;
                break;
            case PFR_NATIVE_CONN_EAST:
                px = (int16_t)(map->width - 1);
                py = (int16_t)(map->height / 2);
                dir = PFR_NATIVE_DIR_EAST;
                walk_act = PFR_NATIVE_ACTION_RIGHT;
                break;
            }

            pfr_native_reset_to_map(&core, map->map_id, px, py, dir);

            /* Walk off the edge */
            PfrNativeStepResult result = c_step(&core, walk_act);

            /* If we moved to a new map, verify we are in bounds */
            if (core.state.current_map != map->map_id) {
                const PfrNativeMap *dest = pfr_native_get_map(core.state.current_map);
                char msg[256];

                snprintf(msg, sizeof(msg),
                         "connection from map %u dir=%d -> map %u: dest map NULL",
                         map->map_id, conn->direction, core.state.current_map);
                TEST_ASSERT(dest != NULL, msg);

                if (dest) {
                    snprintf(msg, sizeof(msg),
                             "connection from map %u dir=%d -> map %u: "
                             "player (%d,%d) out of bounds [%u x %u]",
                             map->map_id, conn->direction, core.state.current_map,
                             core.state.player_x, core.state.player_y,
                             dest->width, dest->height);
                    TEST_ASSERT(core.state.player_x >= 0, msg);
                    TEST_ASSERT(core.state.player_y >= 0, msg);
                    TEST_ASSERT(core.state.player_x < (int16_t)dest->width, msg);
                    TEST_ASSERT(core.state.player_y < (int16_t)dest->height, msg);
                }
            }

            (void)result;
        }
    }
    return 0;
}

/* ---------- test registry ---------------------------------------------- */

static const TestEntry bounds_tests[] = {
    { "bounds_after_warps",         test_bounds_after_warps         },
    { "bounds_random_walk",         test_bounds_random_walk         },
    { "bounds_connection_crossing", test_bounds_connection_crossing },
    { NULL, NULL }
};

#endif /* TEST_BOUNDS_H */
