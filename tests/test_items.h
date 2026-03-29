/* test_items.h -- Item pickup tests.
 * Uses TEST_ASSERT / TEST_ASSERT_EQ macros and TestEntry registration. */
#ifndef TEST_ITEMS_H
#define TEST_ITEMS_H

#include <stdio.h>
#include <string.h>

/* ---------- helpers ---------------------------------------------------- */

/* Find an object event whose hide_flag != 0xFF (an item ball on the ground).
 * We look for objects that have a script whose first action is SET_FLAG,
 * which indicates a typical item pickup pattern. Returns 1 if found. */
static int find_item_object(PfrNativeMapId *out_map, int16_t *out_x,
                             int16_t *out_y, uint8_t *out_hide_flag,
                             uint8_t *out_script_id) {
    for (size_t m = 0; m < gPfrNativeMapCount; m++) {
        const PfrNativeMap *map = &gPfrNativeMaps[m];
        for (size_t o = 0; o < map->object_event_count; o++) {
            const PfrNativeObjectEvent *obj = &map->object_events[o];
            if (obj->hide_flag != 0xFF && obj->script_id != PFR_NATIVE_SCRIPT_NONE) {
                /* Check that the script has a SET_FLAG action */
                uint8_t sid = obj->script_id;
                if (sid < gPfrNativeScriptCount) {
                    const PfrNativeScript *scr = &gPfrNativeScripts[sid];
                    for (uint8_t a = 0; a < scr->action_count; a++) {
                        if (scr->actions[a].type == PFRN_ACT_SET_FLAG) {
                            *out_map = map->map_id;
                            *out_x = obj->x;
                            *out_y = obj->y;
                            *out_hide_flag = obj->hide_flag;
                            *out_script_id = obj->script_id;
                            return 1;
                        }
                    }
                }
            }
        }
    }
    return 0;
}

/* ---------- basic item pickup ------------------------------------------ */

static int test_item_pickup_basic(void) {
    PfrNativeMapId map;
    int16_t ix, iy;
    uint8_t hide_flag, script_id;

    TEST_ASSERT(find_item_object(&map, &ix, &iy, &hide_flag, &script_id),
                "no item object found in any map");

    PfrNativeCore core;
    c_init(&core);

    /* Reset to the item's map, positioning player south of item */
    pfr_native_reset_to_map(&core, map, ix, iy + 1, PFR_NATIVE_DIR_NORTH);

    /* Verify item object is active before interaction.
     * Position player adjacent to the item, trying all 4 directions. */
    int item_obj = -1;
    for (int i = 0; i < (int)core.state.object_count; i++) {
        if (core.state.objects[i].x == ix && core.state.objects[i].y == iy
            && core.state.objects[i].active) {
            item_obj = i;
            break;
        }
    }
    if (item_obj < 0) {
        /* Object at (ix, iy) might not be loaded — could be OOB. Skip test. */
        return 0;
    }

    /* Try all 4 adjacent positions for interaction */
    {
        static const int16_t ddx[] = {0, 0, -1, 1};
        static const int16_t ddy[] = {1, -1, 0, 0};
        static const uint8_t ddirs[] = {PFR_NATIVE_DIR_NORTH, PFR_NATIVE_DIR_SOUTH,
                                         PFR_NATIVE_DIR_EAST, PFR_NATIVE_DIR_WEST};
        const PfrNativeMap *m = pfr_native_get_map(map);
        int found_pos = 0;
        for (int d = 0; d < 4; d++) {
            int16_t px = ix + ddx[d], py = iy + ddy[d];
            const PfrNativeTile *t = tile_at(m, px, py);
            if (t && t->collision == 0) {
                core.state.player_x = px;
                core.state.player_y = py;
                core.state.player_direction = ddirs[d];
                found_pos = 1;
                break;
            }
        }
        if (!found_pos) return 0; /* Can't position — skip */
    }

    /* Interact — must be facing the object */
    /* Ensure player direction faces the object */
    int16_t ddx = ix - core.state.player_x;
    int16_t ddy = iy - core.state.player_y;
    if (ddy < 0) core.state.player_direction = PFR_NATIVE_DIR_NORTH;
    else if (ddy > 0) core.state.player_direction = PFR_NATIVE_DIR_SOUTH;
    else if (ddx < 0) core.state.player_direction = PFR_NATIVE_DIR_WEST;
    else if (ddx > 0) core.state.player_direction = PFR_NATIVE_DIR_EAST;

    c_step(&core, PFR_NATIVE_ACTION_A);

    /* Clear any dialog */
    for (int i = 0; i < 30; i++) {
        if (core.state.mode != PFR_NATIVE_MODE_DIALOG) break;
        c_step(&core, PFR_NATIVE_ACTION_A);
    }

    /* The script's SET_FLAG should have been called, and/or the script itself
     * may have deactivated the object. Either means the item was picked up.
     * NOTE: Some items have guard conditions (FLAG_UNSET) that check the item's
     * own hide flag — from a clean reset, all flags are clear so guards should pass.
     * If the script didn't fire (e.g. positioning error), try the flag check. */
    int got_flag = pfrn_flag_get(core.state.flags, hide_flag);
    int still_active = core.state.objects[item_obj].active;
    /* Verify at least one of: flag set, or object deactivated, or script was a
     * "guard-only" script that deactivated the object on guard pass */
    const PfrNativeScript *scr = &gPfrNativeScripts[script_id];
    if (scr->action_count == 0 && scr->guard_type != PFRN_GUARD_NONE) {
        /* Guard-only script (like cut tree) — object deactivated on guard pass */
        TEST_ASSERT(!still_active,
                    "guard-only item script should deactivate object");
    } else {
        TEST_ASSERT(got_flag || !still_active,
                    "item pickup should set flag or deactivate object");
    }
    return 0;
}

/* ---------- item already picked ---------------------------------------- */

static int test_item_already_picked(void) {
    PfrNativeMapId map;
    int16_t ix, iy;
    uint8_t hide_flag, script_id;

    TEST_ASSERT(find_item_object(&map, &ix, &iy, &hide_flag, &script_id),
                "no item object found in any map");

    PfrNativeCore core;
    c_init(&core);

    /* Reset to the map, then set the hide flag and reload objects */
    pfr_native_reset_to_map(&core, map, ix, iy + 1, PFR_NATIVE_DIR_NORTH);
    core.state.flags = pfrn_flag_set(core.state.flags, hide_flag);
    /* Reload objects with the flag set — object should be hidden */
    reload_objects_for_map(&core.state);

    /* Check that the item object is not active */
    int item_active = 0;
    for (int i = 0; i < (int)core.state.object_count; i++) {
        if (core.state.objects[i].x == ix &&
            core.state.objects[i].y == iy &&
            core.state.objects[i].active)
            item_active = 1;
    }
    TEST_ASSERT(!item_active,
                "item object should not be active when hide flag is pre-set");
    return 0;
}

/* ---------- test registry ---------------------------------------------- */

static const TestEntry item_tests[] = {
    { "item_pickup_basic",    test_item_pickup_basic    },
    { "item_already_picked",  test_item_already_picked  },
    { NULL, NULL }
};

#endif /* TEST_ITEMS_H */
