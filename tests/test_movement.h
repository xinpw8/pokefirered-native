/*
 * test_movement.h -- Movement, blocking, ledge, and connection crossing tests.
 */

#ifndef TEST_MOVEMENT_H
#define TEST_MOVEMENT_H

/* Helper: bootstrap to Pallet Town and return pointer to the static core. */
static PfrNativeCore *test_core_pallet(void)
{
    static PfrNativeCore core;
    c_init(&core);
    c_reset(&core, PFR_NATIVE_BOOTSTRAP_PALLET_TOWN, NULL);
    return &core;
}

/* Helper: reset core to a specific position on a given map. */
static PfrNativeCore *test_core_at(PfrNativeMapId map, int16_t x, int16_t y,
                                   PfrNativeDirection dir)
{
    static PfrNativeCore core;
    c_init(&core);
    pfr_native_reset_to_map(&core, map, x, y, dir);
    return &core;
}

/*
 * test_movement_normal
 *
 * From Pallet Town (6,8), move south to (6,9).
 * Both tiles are passable with behavior 0x000.
 */
static int test_movement_normal(void)
{
    PfrNativeCore *core = test_core_pallet();
    int16_t start_x = core->state.player_x;
    int16_t start_y = core->state.player_y;
    PfrNativeStepResult r;

    /* Sanity: starts at (6,8) */
    TEST_ASSERT_EQ(start_x, 6, "start x");
    TEST_ASSERT_EQ(start_y, 8, "start y");

    r = c_step(core, PFR_NATIVE_ACTION_DOWN);
    TEST_ASSERT_EQ(core->state.player_y, start_y + 1, "y increased by 1 after moving south");
    TEST_ASSERT_EQ(core->state.player_x, start_x, "x unchanged");
    TEST_ASSERT_EQ(r.event, PFR_NATIVE_EVENT_MOVED, "event == MOVED");
    TEST_ASSERT_EQ(r.changed, 1, "changed == 1");

    return 0;
}

/*
 * test_movement_blocked_wall
 *
 * Place player at (2,8) on Pallet Town and try moving LEFT into (1,8)
 * which has collision=1.
 */
static int test_movement_blocked_wall(void)
{
    PfrNativeCore *core = test_core_at(PFR_NATIVE_MAP_PALLET_TOWN, 2, 8,
                                       PFR_NATIVE_DIR_WEST);
    PfrNativeStepResult r;

    /* Verify the destination tile (1,8) is actually blocked */
    const PfrNativeMap *map = pfr_native_get_map(PFR_NATIVE_MAP_PALLET_TOWN);
    const PfrNativeTile *dest = tile_at(map, 1, 8);
    TEST_ASSERT(dest != NULL, "dest tile exists");
    TEST_ASSERT_EQ(dest->collision, 1, "dest tile has collision=1");

    r = c_step(core, PFR_NATIVE_ACTION_LEFT);
    TEST_ASSERT_EQ(core->state.player_x, 2, "x unchanged after blocked move");
    TEST_ASSERT_EQ(core->state.player_y, 8, "y unchanged after blocked move");
    TEST_ASSERT_EQ(r.event, PFR_NATIVE_EVENT_BLOCKED, "event == BLOCKED");

    return 0;
}

/*
 * test_movement_direction_change
 *
 * Start facing SOUTH, press UP. Direction should change to NORTH.
 */
static int test_movement_direction_change(void)
{
    PfrNativeCore *core = test_core_at(PFR_NATIVE_MAP_PALLET_TOWN, 6, 9,
                                       PFR_NATIVE_DIR_SOUTH);

    TEST_ASSERT_EQ(core->state.player_direction, PFR_NATIVE_DIR_SOUTH, "initial dir SOUTH");

    c_step(core, PFR_NATIVE_ACTION_UP);
    TEST_ASSERT_EQ(core->state.player_direction, PFR_NATIVE_DIR_NORTH,
                   "direction changed to NORTH after pressing UP");

    return 0;
}

/*
 * test_movement_none_action
 *
 * NONE action should cause no change.
 */
static int test_movement_none_action(void)
{
    PfrNativeCore *core = test_core_pallet();
    int16_t x0 = core->state.player_x;
    int16_t y0 = core->state.player_y;
    uint8_t dir0 = core->state.player_direction;
    PfrNativeStepResult r;

    r = c_step(core, PFR_NATIVE_ACTION_NONE);
    TEST_ASSERT_EQ(core->state.player_x, x0, "x unchanged");
    TEST_ASSERT_EQ(core->state.player_y, y0, "y unchanged");
    TEST_ASSERT_EQ(core->state.player_direction, dir0, "direction unchanged");
    TEST_ASSERT_EQ(r.event, PFR_NATIVE_EVENT_NONE, "event == NONE");

    return 0;
}

/*
 * test_movement_ledge_jump
 *
 * On Route 1 (map 209), tile (2,5) has MB_JUMP_SOUTH (0x3B).
 * Place player at (2,4) facing SOUTH. Moving south should jump over
 * the ledge, landing at (2,6) -- position changes by +2 in y.
 */
static int test_movement_ledge_jump(void)
{
    PfrNativeCore *core = test_core_at((PfrNativeMapId)209, 2, 4, PFR_NATIVE_DIR_SOUTH);
    PfrNativeStepResult r;

    /* Verify ledge tile */
    const PfrNativeMap *map = pfr_native_get_map((PfrNativeMapId)209);
    const PfrNativeTile *ledge = tile_at(map, 2, 5);
    TEST_ASSERT(ledge != NULL, "ledge tile exists");
    TEST_ASSERT_EQ(ledge->behavior, 0x3B, "ledge tile has MB_JUMP_SOUTH behavior");

    /* Verify landing tile is passable */
    const PfrNativeTile *landing = tile_at(map, 2, 6);
    TEST_ASSERT(landing != NULL, "landing tile exists");
    TEST_ASSERT_EQ(landing->collision, 0, "landing tile is passable");

    r = c_step(core, PFR_NATIVE_ACTION_DOWN);
    TEST_ASSERT_EQ(core->state.player_x, 2, "x unchanged");
    TEST_ASSERT_EQ(core->state.player_y, 6, "y jumped to 6 (over ledge at 5)");
    TEST_ASSERT_EQ(r.event, PFR_NATIVE_EVENT_MOVED, "event == MOVED");
    TEST_ASSERT_EQ(r.changed, 1, "changed == 1");

    return 0;
}

/*
 * test_movement_ledge_wrong_direction
 *
 * Ledges only work when approached from the correct direction.
 * Tile (2,5) on Route 1 is MB_JUMP_SOUTH; trying to move NORTH from
 * (2,6) into (2,5) should be blocked (the ledge has collision=1).
 */
static int test_movement_ledge_wrong_direction(void)
{
    PfrNativeCore *core = test_core_at((PfrNativeMapId)209, 2, 6, PFR_NATIVE_DIR_NORTH);
    PfrNativeStepResult r;

    r = c_step(core, PFR_NATIVE_ACTION_UP);
    TEST_ASSERT_EQ(core->state.player_y, 6, "y unchanged -- can't jump ledge wrong way");
    TEST_ASSERT_EQ(r.event, PFR_NATIVE_EVENT_BLOCKED, "event == BLOCKED");

    return 0;
}

/*
 * test_movement_connection_crossing
 *
 * Walk north from Pallet Town (12,0) into Route 1 via map connection.
 * Pallet Town has a NORTH connection to map 209 (Route 1) with offset 0.
 * Player at (12,0) faces a passable tile. Stepping north crosses into Route 1.
 */
static int test_movement_connection_crossing(void)
{
    PfrNativeCore *core = test_core_at(PFR_NATIVE_MAP_PALLET_TOWN, 12, 0,
                                       PFR_NATIVE_DIR_NORTH);
    PfrNativeStepResult r;

    TEST_ASSERT_EQ(core->state.current_map, PFR_NATIVE_MAP_PALLET_TOWN,
                   "starts on Pallet Town");

    /* Verify the tile at (12,0) is passable (required to step off edge) */
    const PfrNativeMap *pallet = pfr_native_get_map(PFR_NATIVE_MAP_PALLET_TOWN);
    const PfrNativeTile *start_tile = tile_at(pallet, 12, 0);
    TEST_ASSERT(start_tile != NULL, "start tile exists");
    TEST_ASSERT_EQ(start_tile->collision, 0, "start tile passable");

    r = c_step(core, PFR_NATIVE_ACTION_UP);

    /* After crossing, we should be on Route 1 (map 209) */
    TEST_ASSERT_EQ(core->state.current_map, (PfrNativeMapId)209,
                   "crossed to Route 1");
    TEST_ASSERT(r.event == PFR_NATIVE_EVENT_MOVED || r.event == PFR_NATIVE_EVENT_WARPED,
                "event is MOVED or WARPED");
    TEST_ASSERT_EQ(r.changed, 1, "changed == 1");

    /* Player should be at the bottom edge of Route 1 (y = height - 1) */
    const PfrNativeMap *route1 = pfr_native_get_map((PfrNativeMapId)209);
    TEST_ASSERT(route1 != NULL, "Route 1 map exists");
    TEST_ASSERT_EQ(core->state.player_y, (int16_t)route1->height - 1,
                   "player at bottom row of Route 1");

    return 0;
}

/*
 * test_movement_multiple_steps
 *
 * Walk multiple steps and verify cumulative position change.
 */
static int test_movement_multiple_steps(void)
{
    PfrNativeCore *core = test_core_pallet();
    /* Start at (6,8). Move right, right, down. Should end at (8,9). */
    PfrNativeStepResult r;

    r = c_step(core, PFR_NATIVE_ACTION_RIGHT);
    TEST_ASSERT_EQ(r.event, PFR_NATIVE_EVENT_MOVED, "step 1 moved");
    TEST_ASSERT_EQ(core->state.player_x, 7, "x after step 1");

    r = c_step(core, PFR_NATIVE_ACTION_RIGHT);
    TEST_ASSERT_EQ(r.event, PFR_NATIVE_EVENT_MOVED, "step 2 moved");
    TEST_ASSERT_EQ(core->state.player_x, 8, "x after step 2");

    r = c_step(core, PFR_NATIVE_ACTION_DOWN);
    TEST_ASSERT_EQ(r.event, PFR_NATIVE_EVENT_MOVED, "step 3 moved");
    TEST_ASSERT_EQ(core->state.player_y, 9, "y after step 3");

    TEST_ASSERT_EQ(core->state.player_x, 8, "final x");
    TEST_ASSERT_EQ(core->state.player_y, 9, "final y");

    return 0;
}

/*
 * test_movement_direction_updates_on_block
 *
 * Even when blocked, the player's facing direction should update.
 */
static int test_movement_direction_updates_on_block(void)
{
    PfrNativeCore *core = test_core_at(PFR_NATIVE_MAP_PALLET_TOWN, 2, 8,
                                       PFR_NATIVE_DIR_SOUTH);

    TEST_ASSERT_EQ(core->state.player_direction, PFR_NATIVE_DIR_SOUTH, "initial dir");

    /* Move left into wall -- direction should still change */
    c_step(core, PFR_NATIVE_ACTION_LEFT);
    TEST_ASSERT_EQ(core->state.player_direction, PFR_NATIVE_DIR_WEST,
                   "direction updated to WEST even though blocked");

    return 0;
}

/* ---------- Test registration ---------- */

static const TestEntry movement_tests[] = {
    {"movement_normal",                test_movement_normal},
    {"movement_blocked_wall",          test_movement_blocked_wall},
    {"movement_direction_change",      test_movement_direction_change},
    {"movement_none_action",           test_movement_none_action},
    {"movement_ledge_jump",            test_movement_ledge_jump},
    {"movement_ledge_wrong_direction", test_movement_ledge_wrong_direction},
    {"movement_connection_crossing",   test_movement_connection_crossing},
    {"movement_multiple_steps",        test_movement_multiple_steps},
    {"movement_direction_on_block",    test_movement_direction_updates_on_block},
};

#endif /* TEST_MOVEMENT_H */
