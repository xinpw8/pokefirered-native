/* test_dialog.h — Tests for dialog open/close, formatting, and queued transitions */
#ifndef TEST_DIALOG_H
#define TEST_DIALOG_H

#include <string.h>

/* Open dialog, verify mode=DIALOG. Step with A until dialog closes,
 * verify mode returns to OVERWORLD. */
static int test_dialog_open_close(void) {
    PfrNativeCore core;
    c_init(&core);
    c_reset(&core, PFR_NATIVE_BOOTSTRAP_PLAYERS_HOUSE_2F, NULL);

    /* Player starts at (6,6) facing north. The NES is at (6,5) with
     * script PLAYERS_HOUSE_2F_NES which opens dialog PFR_NATIVE_DIALOG_PLAYERS_HOUSE_2F_NES. */
    PfrNativeStepResult r = c_step(&core, PFR_NATIVE_ACTION_A);
    TEST_ASSERT_EQ(r.event, PFR_NATIVE_EVENT_DIALOG_OPENED, "A should open dialog");
    TEST_ASSERT_EQ(core.state.mode, PFR_NATIVE_MODE_DIALOG, "mode should be DIALOG");

    /* Press A to advance through dialog pages */
    int steps = 0;
    while (core.state.mode == PFR_NATIVE_MODE_DIALOG && steps < 20) {
        r = c_step(&core, PFR_NATIVE_ACTION_A);
        TEST_ASSERT(r.event == PFR_NATIVE_EVENT_DIALOG_ADVANCED ||
                    r.event == PFR_NATIVE_EVENT_DIALOG_CLOSED,
                    "A in dialog should advance or close");
        steps++;
    }
    TEST_ASSERT_EQ(core.state.mode, PFR_NATIVE_MODE_OVERWORLD,
                   "dialog should eventually close to OVERWORLD");
    TEST_ASSERT(steps > 0, "dialog should have taken at least one step to close");
    return 0;
}

/* While in dialog mode, press UP -> event NONE, mode stays DIALOG */
static int test_dialog_non_ab_noop(void) {
    PfrNativeCore core;
    c_init(&core);
    c_reset(&core, PFR_NATIVE_BOOTSTRAP_PLAYERS_HOUSE_2F, NULL);

    /* Open dialog by pressing A (facing NES) */
    PfrNativeStepResult r = c_step(&core, PFR_NATIVE_ACTION_A);
    TEST_ASSERT_EQ(r.event, PFR_NATIVE_EVENT_DIALOG_OPENED, "should open dialog");
    TEST_ASSERT_EQ(core.state.mode, PFR_NATIVE_MODE_DIALOG, "mode should be DIALOG");

    /* Press UP while in dialog */
    r = c_step(&core, PFR_NATIVE_ACTION_UP);
    TEST_ASSERT_EQ(r.event, PFR_NATIVE_EVENT_NONE, "UP in dialog should be NONE");
    TEST_ASSERT_EQ(core.state.mode, PFR_NATIVE_MODE_DIALOG,
                   "mode should remain DIALOG after non-A/B input");
    return 0;
}

/* Call pfr_native_format_dialog_page with PFR_NATIVE_DIALOG_PLAYERS_HOUSE_2F_NES (8),
 * verify non-empty result. */
static int test_dialog_format_page(void) {
    PfrNativeCore core;
    c_init(&core);
    c_reset(&core, PFR_NATIVE_BOOTSTRAP_PLAYERS_HOUSE_2F, NULL);

    char buffer[256];
    memset(buffer, 0, sizeof(buffer));

    pfr_native_format_dialog_page(&core, PFR_NATIVE_DIALOG_PLAYERS_HOUSE_2F_NES, 0,
                                  buffer, sizeof(buffer));
    TEST_ASSERT(buffer[0] != '\0', "formatted dialog page should be non-empty");
    TEST_ASSERT(strlen(buffer) > 3, "formatted dialog should have meaningful content");
    return 0;
}

/* Open Mom dialog (after rival beaten) -> verify queued dialog transitions work */
static int test_dialog_queued(void) {
    PfrNativeCore core;
    c_init(&core);
    c_reset(&core, PFR_NATIVE_BOOTSTRAP_PALLET_TOWN, NULL);

    /* Teleport to Player's House 1F where Mom is */
    pfr_native_reset_to_map(&core, PFR_NATIVE_MAP_PLAYERS_HOUSE_1F,
                            5, 6, PFR_NATIVE_DIR_NORTH);
    /* Restore bootstrap flags (reset_to_map clears them) */
    core.state.flags = pfrn_flag_set(core.state.flags, PFRN_FLAG_BEAT_RIVAL_IN_OAKS_LAB);
    core.state.flags = pfrn_flag_set(core.state.flags, PFRN_FLAG_SYS_POKEMON_GET);
    core.state.starter_mon = 0;
    core.state.party_count = 1;

    /* Find Mom by script_id */
    int mom_idx = -1;
    for (int i = 0; i < (int)core.state.object_count; i++) {
        if (core.state.objects[i].active &&
            core.state.objects[i].script_id == PFR_NATIVE_SCRIPT_PLAYERS_HOUSE_1F_MOM) {
            mom_idx = i;
            break;
        }
    }
    TEST_ASSERT(mom_idx >= 0, "Mom should be found on the map");

    /* Position player adjacent to Mom and interact */
    core.state.player_x = core.state.objects[mom_idx].x;
    core.state.player_y = core.state.objects[mom_idx].y + 1;
    core.state.player_direction = PFR_NATIVE_DIR_NORTH;
    PfrNativeStepResult r = c_step(&core, PFR_NATIVE_ACTION_A);
    TEST_ASSERT_EQ(r.event, PFR_NATIVE_EVENT_DIALOG_OPENED, "talking to Mom should open dialog");
    TEST_ASSERT_EQ(core.state.mode, PFR_NATIVE_MODE_DIALOG, "should be in dialog mode");

    /* With BEAT_RIVAL_IN_OAKS_LAB set, Mom should queue a heal dialog.
     * Verify queued_dialog_id is set. */
    TEST_ASSERT(core.state.active_dialog_id == PFR_NATIVE_DIALOG_PLAYERS_HOUSE_1F_MOM_HEAL_1,
                "active dialog should be Mom heal part 1");
    TEST_ASSERT(core.state.queued_dialog_id == PFR_NATIVE_DIALOG_PLAYERS_HOUSE_1F_MOM_HEAL_2,
                "queued dialog should be Mom heal part 2");

    /* Advance through first dialog until it transitions to queued */
    int saw_transition = 0;
    for (int i = 0; i < 30; i++) {
        uint8_t prev_dialog = core.state.active_dialog_id;
        r = c_step(&core, PFR_NATIVE_ACTION_A);
        if (core.state.active_dialog_id != prev_dialog &&
            core.state.active_dialog_id == PFR_NATIVE_DIALOG_PLAYERS_HOUSE_1F_MOM_HEAL_2) {
            saw_transition = 1;
        }
        if (core.state.mode == PFR_NATIVE_MODE_OVERWORLD)
            break;
    }
    TEST_ASSERT(saw_transition, "should have transitioned to queued dialog");
    TEST_ASSERT_EQ(core.state.mode, PFR_NATIVE_MODE_OVERWORLD,
                   "should return to OVERWORLD after all dialogs");
    return 0;
}

/* Format dialog with invalid dialog_id -> buffer stays empty, no crash */
static int test_dialog_out_of_bounds(void) {
    PfrNativeCore core;
    c_init(&core);
    c_reset(&core, PFR_NATIVE_BOOTSTRAP_PALLET_TOWN, NULL);

    char buffer[128];
    memset(buffer, 'X', sizeof(buffer));
    buffer[sizeof(buffer) - 1] = '\0';

    /* Use an invalid dialog ID beyond PFR_NATIVE_DIALOG_COUNT */
    pfr_native_format_dialog_page(&core, PFR_NATIVE_DIALOG_COUNT + 10, 0,
                                  buffer, sizeof(buffer));
    TEST_ASSERT(buffer[0] == '\0', "buffer should be empty for invalid dialog_id");

    /* Also test invalid page index on a valid dialog */
    memset(buffer, 'X', sizeof(buffer));
    buffer[sizeof(buffer) - 1] = '\0';
    pfr_native_format_dialog_page(&core, PFR_NATIVE_DIALOG_PLAYERS_HOUSE_2F_NES, 255,
                                  buffer, sizeof(buffer));
    TEST_ASSERT(buffer[0] == '\0', "buffer should be empty for invalid page_index");
    return 0;
}

static const TestEntry dialog_tests[] = {
    { "dialog_open_close",     test_dialog_open_close },
    { "dialog_non_ab_noop",    test_dialog_non_ab_noop },
    { "dialog_format_page",    test_dialog_format_page },
    { "dialog_queued",         test_dialog_queued },
    { "dialog_out_of_bounds",  test_dialog_out_of_bounds },
    { NULL, NULL }
};

#endif /* TEST_DIALOG_H */
