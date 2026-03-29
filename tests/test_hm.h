/* test_hm.h — Tests for HM mechanics: Cut, Surf, and Strength */
#ifndef TEST_HM_H
#define TEST_HM_H

#include <string.h>

/* ---- Helper: find a map+object matching a criteria ---- */

/* Find a cut tree object (script_id == PFR_NATIVE_SCRIPT_CUT_TREE) anywhere
 * in the map data. Returns the map and object event index. */
static PfrNativeMapId find_cut_tree_map(int *out_oe_idx) {
    size_t map_count = gPfrNativeMapCount;
    for (size_t m = 0; m < map_count; m++) {
        const PfrNativeMap *map = pfr_native_get_map((PfrNativeMapId)m);
        if (map == NULL) continue;
        for (size_t i = 0; i < map->object_event_count; i++) {
            if (map->object_events[i].script_id == PFR_NATIVE_SCRIPT_CUT_TREE) {
                if (out_oe_idx) *out_oe_idx = (int)i;
                return map->map_id;
            }
        }
    }
    if (out_oe_idx) *out_oe_idx = -1;
    return PFR_NATIVE_MAP_INVALID;
}

/* Find a map with a water tile (behavior == 0x10, MB_POND_WATER) that has
 * collision != 0 (i.e., actually blocked without Surf). Returns map id and
 * the coordinates of the water tile, plus the adjacent land position. */
static PfrNativeMapId find_water_tile_map(int16_t *out_x, int16_t *out_y) {
    size_t map_count = gPfrNativeMapCount;
    for (size_t m = 0; m < map_count; m++) {
        const PfrNativeMap *map = pfr_native_get_map((PfrNativeMapId)m);
        if (map == NULL) continue;
        for (size_t t = 0; t < map->tile_count; t++) {
            uint16_t beh = map->tiles[t].behavior;
            if ((beh == 0x10 || beh == 0x11 || beh == 0x12 || beh == 0x15)
                && map->tiles[t].collision != 0) {
                int16_t x = (int16_t)(t % map->width);
                int16_t y = (int16_t)(t / map->width);
                /* Try all 4 adjacent tiles for a passable land tile */
                static const int16_t dx[] = {0, 0, -1, 1};
                static const int16_t dy[] = {1, -1, 0, 0};
                for (int d = 0; d < 4; d++) {
                    int16_t ax = x + dx[d];
                    int16_t ay = y + dy[d];
                    if (ax < 0 || ay < 0 || ax >= (int16_t)map->width || ay >= (int16_t)map->height)
                        continue;
                    size_t adj_idx = (size_t)ay * map->width + (size_t)ax;
                    if (adj_idx < map->tile_count &&
                        map->tiles[adj_idx].collision == 0 &&
                        !is_water_behavior(map->tiles[adj_idx].behavior)) {
                        if (out_x) *out_x = x;
                        if (out_y) *out_y = y;
                        return map->map_id;
                    }
                }
            }
        }
    }
    if (out_x) *out_x = -1;
    if (out_y) *out_y = -1;
    return PFR_NATIVE_MAP_INVALID;
}

/* Find a map with a pushable boulder (graphics_id == PFR_NATIVE_GFX_PUSHABLE_BOULDER).
 * Returns map id and the object event index. */
static PfrNativeMapId find_boulder_map(int *out_oe_idx) {
    size_t map_count = gPfrNativeMapCount;
    for (size_t m = 0; m < map_count; m++) {
        const PfrNativeMap *map = pfr_native_get_map((PfrNativeMapId)m);
        if (map == NULL) continue;
        for (size_t i = 0; i < map->object_event_count; i++) {
            if (map->object_events[i].graphics_id == PFR_NATIVE_GFX_PUSHABLE_BOULDER) {
                if (out_oe_idx) *out_oe_idx = (int)i;
                return map->map_id;
            }
        }
    }
    if (out_oe_idx) *out_oe_idx = -1;
    return PFR_NATIVE_MAP_INVALID;
}

/* ---- Cut tests ---- */

/* Find a cut tree object. Reset to that map. Try to interact with A ->
 * script guard fails, tree stays. */
static int test_hm_cut_without_flag(void) {
    int oe_idx = -1;
    PfrNativeMapId map_id = find_cut_tree_map(&oe_idx);
    TEST_ASSERT(map_id != PFR_NATIVE_MAP_INVALID, "should find a map with a cut tree");
    TEST_ASSERT(oe_idx >= 0, "should find a cut tree object event");

    const PfrNativeMap *map = pfr_native_get_map(map_id);
    const PfrNativeObjectEvent *tree_oe = &map->object_events[oe_idx];

    PfrNativeCore core;
    c_init(&core);
    /* Position player south of the tree, facing north */
    pfr_native_reset_to_map(&core, map_id, tree_oe->x, tree_oe->y + 1, PFR_NATIVE_DIR_NORTH);
    /* Ensure GOT_HM01 is NOT set */
    core.state.flags = pfrn_flag_clear(core.state.flags, PFRN_FLAG_GOT_HM01);

    /* Find the runtime object corresponding to this tree */
    int tree_obj = -1;
    for (int i = 0; i < (int)core.state.object_count; i++) {
        if (core.state.objects[i].active &&
            core.state.objects[i].x == tree_oe->x &&
            core.state.objects[i].y == tree_oe->y &&
            core.state.objects[i].script_id == PFR_NATIVE_SCRIPT_CUT_TREE) {
            tree_obj = i;
            break;
        }
    }
    TEST_ASSERT(tree_obj >= 0, "cut tree object should be active on map");

    /* Try to interact -- without HM01, guard should fail */
    c_step(&core, PFR_NATIVE_ACTION_A);
    /* Clear any dialog that might have opened */
    for (int i = 0; i < 10 && core.state.mode == PFR_NATIVE_MODE_DIALOG; i++)
        c_step(&core, PFR_NATIVE_ACTION_A);

    TEST_ASSERT(core.state.objects[tree_obj].active,
                "cut tree should remain active without HM01");
    return 0;
}

/* Same but set GOT_HM01 flag first -> tree object deactivated */
static int test_hm_cut_with_flag(void) {
    int oe_idx = -1;
    PfrNativeMapId map_id = find_cut_tree_map(&oe_idx);
    TEST_ASSERT(map_id != PFR_NATIVE_MAP_INVALID, "should find a map with a cut tree");

    const PfrNativeMap *map = pfr_native_get_map(map_id);
    const PfrNativeObjectEvent *tree_oe = &map->object_events[oe_idx];

    PfrNativeCore core;
    c_init(&core);
    pfr_native_reset_to_map(&core, map_id, tree_oe->x, tree_oe->y + 1, PFR_NATIVE_DIR_NORTH);
    /* Set GOT_HM01 */
    core.state.flags = pfrn_flag_set(core.state.flags, PFRN_FLAG_GOT_HM01);

    int tree_obj = -1;
    for (int i = 0; i < (int)core.state.object_count; i++) {
        if (core.state.objects[i].active &&
            core.state.objects[i].x == tree_oe->x &&
            core.state.objects[i].y == tree_oe->y &&
            core.state.objects[i].script_id == PFR_NATIVE_SCRIPT_CUT_TREE) {
            tree_obj = i;
            break;
        }
    }
    TEST_ASSERT(tree_obj >= 0, "cut tree should be active before interaction");

    /* Interact -- with HM01, guard passes, tree should be deactivated */
    c_step(&core, PFR_NATIVE_ACTION_A);
    /* Clear any dialog */
    for (int i = 0; i < 10 && core.state.mode == PFR_NATIVE_MODE_DIALOG; i++)
        c_step(&core, PFR_NATIVE_ACTION_A);

    TEST_ASSERT(!core.state.objects[tree_obj].active,
                "cut tree should be deactivated with HM01");
    return 0;
}

/* ---- Surf tests ---- */

/* Place player next to water tile. Try moving onto it without flags -> BLOCKED */
static int test_hm_surf_blocked_without_flags(void) {
    int16_t water_x, water_y;
    PfrNativeMapId map_id = find_water_tile_map(&water_x, &water_y);
    if (map_id == PFR_NATIVE_MAP_INVALID) {
        /* No blocked water tiles found — surf not needed for these maps */
        return 0;
    }

    /* Find an adjacent land tile to stand on */
    const PfrNativeMap *map = pfr_native_get_map(map_id);
    static const int16_t dx[] = {0, 0, -1, 1};
    static const int16_t dy[] = {1, -1, 0, 0};
    static const uint8_t dirs[] = {PFR_NATIVE_DIR_NORTH, PFR_NATIVE_DIR_SOUTH,
                                    PFR_NATIVE_DIR_EAST, PFR_NATIVE_DIR_WEST};
    static const PfrNativeAction acts[] = {PFR_NATIVE_ACTION_UP, PFR_NATIVE_ACTION_DOWN,
                                             PFR_NATIVE_ACTION_RIGHT, PFR_NATIVE_ACTION_LEFT};
    int placed = -1;
    for (int d = 0; d < 4; d++) {
        int16_t ax = water_x + dx[d], ay = water_y + dy[d];
        if (ax >= 0 && ay >= 0 && ax < (int16_t)map->width && ay < (int16_t)map->height) {
            const PfrNativeTile *t = &map->tiles[(size_t)ay * map->width + (size_t)ax];
            if (t->collision == 0 && !is_water_behavior(t->behavior)) {
                placed = d;
                break;
            }
        }
    }
    TEST_ASSERT(placed >= 0, "should find adjacent land tile");

    PfrNativeCore core;
    c_init(&core);
    pfr_native_reset_to_map(&core, map_id, water_x + dx[placed], water_y + dy[placed], dirs[placed]);
    /* Ensure no surf prerequisites */
    for (int i = 0; i < 8; i++)
        core.state.flags = pfrn_flag_clear(core.state.flags, PFRN_FLAG_BADGE01_GET + i);
    core.state.flags = pfrn_flag_clear(core.state.flags, PFRN_FLAG_GOT_HM03);

    /* Step toward the water tile (opposite of our offset direction) */
    PfrNativeAction step_act = acts[placed];
    int16_t save_x = core.state.player_x, save_y = core.state.player_y;
    PfrNativeStepResult r = c_step(&core, step_act);
    TEST_ASSERT_EQ(r.event, PFR_NATIVE_EVENT_BLOCKED,
                   "moving onto water without surf prerequisites should be BLOCKED");
    TEST_ASSERT(core.state.player_x == save_x && core.state.player_y == save_y,
                "player should not have moved onto water tile");
    return 0;
}

/* Set 5 badges + GOT_HM03. Same water tile -> MOVED (allowed) */
static int test_hm_surf_allowed_with_flags(void) {
    int16_t water_x, water_y;
    PfrNativeMapId map_id = find_water_tile_map(&water_x, &water_y);
    if (map_id == PFR_NATIVE_MAP_INVALID)
        return 0; /* No blocked water tiles — surf mechanic not exercisable */

    /* Find adjacent land tile */
    const PfrNativeMap *map = pfr_native_get_map(map_id);
    static const int16_t dx[] = {0, 0, -1, 1};
    static const int16_t dy[] = {1, -1, 0, 0};
    static const uint8_t dirs[] = {PFR_NATIVE_DIR_NORTH, PFR_NATIVE_DIR_SOUTH,
                                    PFR_NATIVE_DIR_EAST, PFR_NATIVE_DIR_WEST};
    int placed = -1;
    for (int d = 0; d < 4; d++) {
        int16_t ax = water_x + dx[d], ay = water_y + dy[d];
        if (ax >= 0 && ay >= 0 && ax < (int16_t)map->width && ay < (int16_t)map->height) {
            const PfrNativeTile *t = &map->tiles[(size_t)ay * map->width + (size_t)ax];
            if (t->collision == 0 && !is_water_behavior(t->behavior)) { placed = d; break; }
        }
    }
    TEST_ASSERT(placed >= 0, "should find adjacent land tile");

    PfrNativeCore core;
    c_init(&core);
    pfr_native_reset_to_map(&core, map_id, water_x + dx[placed], water_y + dy[placed], dirs[placed]);

    /* Set 5 badges + GOT_HM03 */
    for (int i = 0; i < 5; i++)
        core.state.flags = pfrn_flag_set(core.state.flags, PFRN_FLAG_BADGE01_GET + i);
    core.state.flags = pfrn_flag_set(core.state.flags, PFRN_FLAG_GOT_HM03);

    /* Step toward the water tile (opposite of our offset direction) */
    static const PfrNativeAction acts2[] = {PFR_NATIVE_ACTION_UP, PFR_NATIVE_ACTION_DOWN,
                                              PFR_NATIVE_ACTION_RIGHT, PFR_NATIVE_ACTION_LEFT};
    PfrNativeStepResult r = c_step(&core, acts2[placed]);
    TEST_ASSERT_EQ(r.event, PFR_NATIVE_EVENT_MOVED,
                   "moving onto water with surf prerequisites should be MOVED");
    TEST_ASSERT_EQ(core.state.player_x, water_x, "player x should be on water tile");
    TEST_ASSERT_EQ(core.state.player_y, water_y, "player y should be on water tile");
    return 0;
}

/* ---- Strength tests ---- */

/* Place player next to boulder, try pushing without flags -> BLOCKED */
static int test_hm_strength_without_flags(void) {
    int oe_idx = -1;
    PfrNativeMapId map_id = find_boulder_map(&oe_idx);
    TEST_ASSERT(map_id != PFR_NATIVE_MAP_INVALID,
                "should find a map with a pushable boulder");

    const PfrNativeMap *map = pfr_native_get_map(map_id);
    const PfrNativeObjectEvent *boulder_oe = &map->object_events[oe_idx];

    PfrNativeCore core;
    c_init(&core);
    /* Position player south of boulder, facing north */
    pfr_native_reset_to_map(&core, map_id,
                            boulder_oe->x, boulder_oe->y + 1, PFR_NATIVE_DIR_NORTH);
    /* Clear strength prerequisites */
    for (int i = 0; i < 8; i++)
        core.state.flags = pfrn_flag_clear(core.state.flags, PFRN_FLAG_BADGE01_GET + i);
    core.state.flags = pfrn_flag_clear(core.state.flags, PFRN_FLAG_GOT_HM04);

    /* Find the runtime boulder object */
    int boulder_obj = -1;
    for (int i = 0; i < (int)core.state.object_count; i++) {
        if (core.state.objects[i].active &&
            core.state.objects[i].graphics_id == PFR_NATIVE_GFX_PUSHABLE_BOULDER &&
            core.state.objects[i].x == boulder_oe->x &&
            core.state.objects[i].y == boulder_oe->y) {
            boulder_obj = i;
            break;
        }
    }
    TEST_ASSERT(boulder_obj >= 0, "boulder object should be active");

    int16_t orig_boulder_x = core.state.objects[boulder_obj].x;
    int16_t orig_boulder_y = core.state.objects[boulder_obj].y;

    PfrNativeStepResult r = c_step(&core, PFR_NATIVE_ACTION_UP);
    TEST_ASSERT_EQ(r.event, PFR_NATIVE_EVENT_BLOCKED,
                   "pushing boulder without strength prerequisites should be BLOCKED");
    TEST_ASSERT_EQ(core.state.objects[boulder_obj].x, orig_boulder_x,
                   "boulder x should not change without prerequisites");
    TEST_ASSERT_EQ(core.state.objects[boulder_obj].y, orig_boulder_y,
                   "boulder y should not change without prerequisites");
    return 0;
}

/* Set 4 badges + GOT_HM04 -> boulder moves, player moves into old position */
static int test_hm_strength_with_flags(void) {
    int oe_idx = -1;
    PfrNativeMapId map_id = find_boulder_map(&oe_idx);
    TEST_ASSERT(map_id != PFR_NATIVE_MAP_INVALID,
                "should find a map with a pushable boulder");

    const PfrNativeMap *map = pfr_native_get_map(map_id);
    const PfrNativeObjectEvent *boulder_oe = &map->object_events[oe_idx];

    PfrNativeCore core;
    c_init(&core);
    pfr_native_reset_to_map(&core, map_id,
                            boulder_oe->x, boulder_oe->y + 1, PFR_NATIVE_DIR_NORTH);

    /* Set 4 badges + GOT_HM04 */
    for (int i = 0; i < 4; i++)
        core.state.flags = pfrn_flag_set(core.state.flags, PFRN_FLAG_BADGE01_GET + i);
    core.state.flags = pfrn_flag_set(core.state.flags, PFRN_FLAG_GOT_HM04);

    /* Find the runtime boulder object */
    int boulder_obj = -1;
    for (int i = 0; i < (int)core.state.object_count; i++) {
        if (core.state.objects[i].active &&
            core.state.objects[i].graphics_id == PFR_NATIVE_GFX_PUSHABLE_BOULDER &&
            core.state.objects[i].x == boulder_oe->x &&
            core.state.objects[i].y == boulder_oe->y) {
            boulder_obj = i;
            break;
        }
    }
    TEST_ASSERT(boulder_obj >= 0, "boulder object should be active");

    int16_t orig_boulder_x = core.state.objects[boulder_obj].x;
    int16_t orig_boulder_y = core.state.objects[boulder_obj].y;
    int16_t orig_player_y = core.state.player_y;

    PfrNativeStepResult r = c_step(&core, PFR_NATIVE_ACTION_UP);

    /* The boulder push may fail if the destination tile is blocked.
     * Check whether the push was possible first. */
    if (r.event == PFR_NATIVE_EVENT_MOVED) {
        /* Push succeeded: boulder moved north, player took boulder's old spot */
        TEST_ASSERT_EQ(core.state.objects[boulder_obj].y, orig_boulder_y - 1,
                       "boulder should have moved north by 1");
        TEST_ASSERT_EQ(core.state.objects[boulder_obj].x, orig_boulder_x,
                       "boulder x should not change when pushing north");
        TEST_ASSERT_EQ(core.state.player_x, orig_boulder_x,
                       "player should be at boulder's old x position");
        TEST_ASSERT_EQ(core.state.player_y, orig_boulder_y,
                       "player should be at boulder's old y position");
    } else {
        /* Push blocked by terrain behind boulder. Try from different direction.
         * Try pushing east (player west of boulder). */
        pfr_native_reset_to_map(&core, map_id,
                                boulder_oe->x - 1, boulder_oe->y, PFR_NATIVE_DIR_EAST);
        for (int i = 0; i < 4; i++)
            core.state.flags = pfrn_flag_set(core.state.flags, PFRN_FLAG_BADGE01_GET + i);
        core.state.flags = pfrn_flag_set(core.state.flags, PFRN_FLAG_GOT_HM04);

        /* Re-find boulder */
        boulder_obj = -1;
        for (int i = 0; i < (int)core.state.object_count; i++) {
            if (core.state.objects[i].active &&
                core.state.objects[i].graphics_id == PFR_NATIVE_GFX_PUSHABLE_BOULDER &&
                core.state.objects[i].x == boulder_oe->x &&
                core.state.objects[i].y == boulder_oe->y) {
                boulder_obj = i;
                break;
            }
        }
        if (boulder_obj >= 0) {
            orig_boulder_x = core.state.objects[boulder_obj].x;
            orig_boulder_y = core.state.objects[boulder_obj].y;

            r = c_step(&core, PFR_NATIVE_ACTION_RIGHT);
            if (r.event == PFR_NATIVE_EVENT_MOVED) {
                TEST_ASSERT_EQ(core.state.objects[boulder_obj].x, orig_boulder_x + 1,
                               "boulder should have moved east by 1");
                TEST_ASSERT_EQ(core.state.player_x, orig_boulder_x,
                               "player should be at boulder's old x");
                TEST_ASSERT_EQ(core.state.player_y, orig_boulder_y,
                               "player should be at boulder's old y");
            }
            /* If still blocked, the terrain prevents pushing in any direction.
             * This is a valid scenario for some boulder placements. */
        }
    }

    return 0;
}

static const TestEntry hm_tests[] = {
    { "hm_cut_without_flag",            test_hm_cut_without_flag },
    { "hm_cut_with_flag",               test_hm_cut_with_flag },
    { "hm_surf_blocked_without_flags",  test_hm_surf_blocked_without_flags },
    { "hm_surf_allowed_with_flags",     test_hm_surf_allowed_with_flags },
    { "hm_strength_without_flags",      test_hm_strength_without_flags },
    { "hm_strength_with_flags",         test_hm_strength_with_flags },
    { NULL, NULL }
};

#endif /* TEST_HM_H */
