/* test_state.h — Tests for state persistence, snapshots, and determinism */
#ifndef TEST_STATE_H
#define TEST_STATE_H

#include <string.h>

/* Set a flag, step multiple times, verify flag still set */
static int test_state_flag_persistence(void) {
    PfrNativeCore core;
    c_init(&core);
    c_reset(&core, PFR_NATIVE_BOOTSTRAP_PALLET_TOWN, NULL);

    /* Set a flag that isn't already set by bootstrap */
    core.state.flags = pfrn_flag_set(core.state.flags, PFRN_FLAG_GOT_HM01);
    TEST_ASSERT(pfrn_flag_get(core.state.flags, PFRN_FLAG_GOT_HM01),
                "HM01 flag should be set");

    /* Step several times (move around) */
    c_step(&core, PFR_NATIVE_ACTION_DOWN);
    c_step(&core, PFR_NATIVE_ACTION_LEFT);
    c_step(&core, PFR_NATIVE_ACTION_RIGHT);
    c_step(&core, PFR_NATIVE_ACTION_NONE);
    c_step(&core, PFR_NATIVE_ACTION_UP);

    TEST_ASSERT(pfrn_flag_get(core.state.flags, PFRN_FLAG_GOT_HM01),
                "HM01 flag should persist across steps");
    return 0;
}

/* Set a var, step, verify var still set */
static int test_state_var_persistence(void) {
    PfrNativeCore core;
    c_init(&core);
    c_reset(&core, PFR_NATIVE_BOOTSTRAP_PALLET_TOWN, NULL);

    core.state.vars[PFRN_VAR_MAP_SCENE_PEWTER_CITY] = 99;
    TEST_ASSERT_EQ(core.state.vars[PFRN_VAR_MAP_SCENE_PEWTER_CITY], 99,
                   "var should be set to 99");

    /* Step several times */
    c_step(&core, PFR_NATIVE_ACTION_DOWN);
    c_step(&core, PFR_NATIVE_ACTION_LEFT);
    c_step(&core, PFR_NATIVE_ACTION_NONE);

    TEST_ASSERT_EQ(core.state.vars[PFRN_VAR_MAP_SCENE_PEWTER_CITY], 99,
                   "var should persist across steps");
    return 0;
}

/* Save snapshot, modify state, restore snapshot, verify restored state matches original */
static int test_state_snapshot_roundtrip(void) {
    PfrNativeCore core;
    c_init(&core);
    c_reset(&core, PFR_NATIVE_BOOTSTRAP_PALLET_TOWN, NULL);

    /* Record original state */
    PfrNativeSnapshot snapshot;
    c_save_snapshot(&core, &snapshot);

    int16_t orig_x = core.state.player_x;
    int16_t orig_y = core.state.player_y;
    uint64_t orig_flags = core.state.flags;
    PfrNativeMapId orig_map = core.state.current_map;

    /* Modify state by stepping */
    c_step(&core, PFR_NATIVE_ACTION_DOWN);
    c_step(&core, PFR_NATIVE_ACTION_DOWN);
    core.state.flags = pfrn_flag_set(core.state.flags, PFRN_FLAG_GOT_HM03);
    core.state.vars[PFRN_VAR_MAP_SCENE_PEWTER_CITY] = 77;

    /* Verify state has changed */
    TEST_ASSERT(core.state.player_y != orig_y || core.state.flags != orig_flags,
                "state should be different after modifications");

    /* Restore from snapshot */
    c_reset(&core, PFR_NATIVE_BOOTSTRAP_PALLET_TOWN, &snapshot);

    TEST_ASSERT_EQ(core.state.player_x, orig_x, "player_x should be restored");
    TEST_ASSERT_EQ(core.state.player_y, orig_y, "player_y should be restored");
    TEST_ASSERT(core.state.flags == orig_flags, "flags should be restored");
    TEST_ASSERT(core.state.current_map == orig_map, "map should be restored");
    TEST_ASSERT_EQ(core.state.vars[PFRN_VAR_MAP_SCENE_PEWTER_CITY], 0,
                   "var should be restored to original value");

    return 0;
}

/* Run same sequence of actions from same bootstrap -> identical state */
static int test_state_determinism(void) {
    PfrNativeCore core_a, core_b;

    /* Run A */
    c_init(&core_a);
    c_reset(&core_a, PFR_NATIVE_BOOTSTRAP_PALLET_TOWN, NULL);
    c_step(&core_a, PFR_NATIVE_ACTION_DOWN);
    c_step(&core_a, PFR_NATIVE_ACTION_LEFT);
    c_step(&core_a, PFR_NATIVE_ACTION_UP);
    c_step(&core_a, PFR_NATIVE_ACTION_A);
    c_step(&core_a, PFR_NATIVE_ACTION_A);
    c_step(&core_a, PFR_NATIVE_ACTION_RIGHT);
    c_step(&core_a, PFR_NATIVE_ACTION_NONE);

    PfrNativeSnapshot snap_a;
    c_save_snapshot(&core_a, &snap_a);

    /* Run B with identical actions */
    c_init(&core_b);
    c_reset(&core_b, PFR_NATIVE_BOOTSTRAP_PALLET_TOWN, NULL);
    c_step(&core_b, PFR_NATIVE_ACTION_DOWN);
    c_step(&core_b, PFR_NATIVE_ACTION_LEFT);
    c_step(&core_b, PFR_NATIVE_ACTION_UP);
    c_step(&core_b, PFR_NATIVE_ACTION_A);
    c_step(&core_b, PFR_NATIVE_ACTION_A);
    c_step(&core_b, PFR_NATIVE_ACTION_RIGHT);
    c_step(&core_b, PFR_NATIVE_ACTION_NONE);

    PfrNativeSnapshot snap_b;
    c_save_snapshot(&core_b, &snap_b);

    /* Compare entire state */
    TEST_ASSERT(memcmp(&snap_a, &snap_b, sizeof(PfrNativeSnapshot)) == 0,
                "identical action sequences from same bootstrap should produce identical state");

    /* Spot-check key fields */
    TEST_ASSERT_EQ(snap_a.player_x, snap_b.player_x, "player_x should match");
    TEST_ASSERT_EQ(snap_a.player_y, snap_b.player_y, "player_y should match");
    TEST_ASSERT(snap_a.flags == snap_b.flags, "flags should match");
    TEST_ASSERT(snap_a.current_map == snap_b.current_map, "map should match");

    return 0;
}

/* pfr_native_state_size() == sizeof(PfrNativeSnapshot) */
static int test_state_size_matches(void) {
    size_t reported = pfr_native_state_size();
    size_t actual = sizeof(PfrNativeSnapshot);
    TEST_ASSERT(reported == actual,
                "pfr_native_state_size() should match sizeof(PfrNativeSnapshot)");
    return 0;
}

static const TestEntry state_tests[] = {
    { "state_flag_persistence",    test_state_flag_persistence },
    { "state_var_persistence",     test_state_var_persistence },
    { "state_snapshot_roundtrip",  test_state_snapshot_roundtrip },
    { "state_determinism",         test_state_determinism },
    { "state_size_matches",        test_state_size_matches },
    { NULL, NULL }
};

#endif /* TEST_STATE_H */
