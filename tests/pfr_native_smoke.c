#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pfr_native.h"

static void fail(const char *message)
{
    fprintf(stderr, "pfr_native_smoke: %s\n", message);
    exit(1);
}

static void assert_true(int condition, const char *message)
{
    if (!condition)
        fail(message);
}

static void assert_state_eq(const PfrNativeSnapshot *a, const PfrNativeSnapshot *b,
                            const char *message)
{
    if (memcmp(a, b, sizeof(*a)) != 0)
        fail(message);
}

static void step_ok(PfrNativeCore *core, PfrNativeAction action, uint8_t expected_event)
{
    PfrNativeStepResult result = c_step(core, action);
    if (result.event != expected_event)
    {
        fprintf(stderr, "pfr_native_smoke: expected event %u, got %u\n",
                expected_event, result.event);
        exit(1);
    }
}

static void reset_to_map_ok(PfrNativeCore *core, PfrNativeMapId map_id, int16_t x, int16_t y,
                            PfrNativeDirection direction)
{
    if (pfr_native_reset_to_map(core, map_id, x, y, direction) != 0)
    {
        fprintf(stderr, "pfr_native_smoke: failed to reset to map %u at (%d,%d)\n",
                (unsigned)map_id, x, y);
        exit(1);
    }
}

int main(void)
{
    PfrNativeCore core;
    PfrNativeSnapshot base;
    PfrNativeSnapshot next_a;
    PfrNativeSnapshot next_b;
    PfrNativeMapId route1_id;
    const PfrNativeState *state;
    uint32_t framebuffer[PFR_NATIVE_SCREEN_WIDTH * PFR_NATIVE_SCREEN_HEIGHT];
    size_t i;
    size_t nonzero = 0;

    c_init(&core);
    route1_id = pfr_native_find_map_by_name("Route1");
    assert_true(route1_id != PFR_NATIVE_MAP_INVALID, "expected Route1 map id");
    assert_true(c_reset(&core, PFR_NATIVE_BOOTSTRAP_PLAYERS_HOUSE_2F, NULL) == 0,
                "bootstrap reset failed");

    state = pfr_native_state(&core);
    assert_true(state->current_map == PFR_NATIVE_MAP_PLAYERS_HOUSE_2F,
                "expected 2F bootstrap map");
    assert_true(state->player_x == 6 && state->player_y == 6,
                "unexpected bootstrap player position");
    assert_true(state->mode == PFR_NATIVE_MODE_OVERWORLD,
                "bootstrap should start in overworld");
    assert_true(state->player_direction == PFR_NATIVE_DIR_NORTH,
                "bootstrap should face north");

    c_save_snapshot(&core, &base);
    step_ok(&core, PFR_NATIVE_ACTION_NONE, PFR_NATIVE_EVENT_NONE);
    assert_state_eq(&base, pfr_native_state(&core), "NONE must not change state");

    assert_true(c_reset(&core, PFR_NATIVE_BOOTSTRAP_PLAYERS_HOUSE_2F, &base) == 0,
                "snapshot reset failed");
    step_ok(&core, PFR_NATIVE_ACTION_LEFT, PFR_NATIVE_EVENT_MOVED);
    c_save_snapshot(&core, &next_a);

    assert_true(c_reset(&core, PFR_NATIVE_BOOTSTRAP_PLAYERS_HOUSE_2F, &base) == 0,
                "snapshot reset failed");
    step_ok(&core, PFR_NATIVE_ACTION_LEFT, PFR_NATIVE_EVENT_MOVED);
    c_save_snapshot(&core, &next_b);
    assert_state_eq(&next_a, &next_b, "same action from same state must match");

    assert_true(c_reset(&core, PFR_NATIVE_BOOTSTRAP_PLAYERS_HOUSE_2F, &base) == 0,
                "snapshot reset failed");
    step_ok(&core, PFR_NATIVE_ACTION_UP, PFR_NATIVE_EVENT_BLOCKED);
    state = pfr_native_state(&core);
    assert_true(state->player_x == 6 && state->player_y == 6,
                "blocked movement should not translate position");

    assert_true(c_reset(&core, PFR_NATIVE_BOOTSTRAP_PLAYERS_HOUSE_2F, &base) == 0,
                "snapshot reset failed");
    step_ok(&core, PFR_NATIVE_ACTION_A, PFR_NATIVE_EVENT_DIALOG_OPENED);
    state = pfr_native_state(&core);
    assert_true(state->mode == PFR_NATIVE_MODE_DIALOG, "NES should open dialog");
    assert_true(state->active_dialog_id == PFR_NATIVE_DIALOG_PLAYERS_HOUSE_2F_NES,
                "unexpected dialog id");
    step_ok(&core, PFR_NATIVE_ACTION_A, PFR_NATIVE_EVENT_DIALOG_ADVANCED);
    step_ok(&core, PFR_NATIVE_ACTION_A, PFR_NATIVE_EVENT_DIALOG_CLOSED);
    assert_true(pfr_native_state(&core)->mode == PFR_NATIVE_MODE_OVERWORLD,
                "dialog should close");

    {
        PfrNativeSnapshot probe = base;
        probe.player_x = 2;
        probe.player_y = 4;
        probe.player_direction = PFR_NATIVE_DIR_NORTH;
        assert_true(c_reset(&core, PFR_NATIVE_BOOTSTRAP_PLAYERS_HOUSE_2F, &probe) == 0,
                    "probe reset failed");
        step_ok(&core, PFR_NATIVE_ACTION_UP, PFR_NATIVE_EVENT_MOVED);
        state = pfr_native_state(&core);
        assert_true(state->player_x == 2 && state->player_y == 3,
                    "south-blocked edge should not block north movement");

        assert_true(c_reset(&core, PFR_NATIVE_BOOTSTRAP_PLAYERS_HOUSE_2F, &probe) == 0,
                    "probe reset failed");
        step_ok(&core, PFR_NATIVE_ACTION_DOWN, PFR_NATIVE_EVENT_BLOCKED);
        state = pfr_native_state(&core);
        assert_true(state->player_x == 2 && state->player_y == 4,
                    "south-blocked edge should block south movement");

        probe.player_x = 2;
        probe.player_y = 3;
        assert_true(c_reset(&core, PFR_NATIVE_BOOTSTRAP_PLAYERS_HOUSE_2F, &probe) == 0,
                    "probe reset failed");
        step_ok(&core, PFR_NATIVE_ACTION_DOWN, PFR_NATIVE_EVENT_MOVED);
        state = pfr_native_state(&core);
        assert_true(state->player_x == 2 && state->player_y == 4,
                    "re-entering the south-blocked tile from the north should work");
    }

    assert_true(c_reset(&core, PFR_NATIVE_BOOTSTRAP_PLAYERS_HOUSE_2F, &base) == 0,
                "snapshot reset failed");
    step_ok(&core, PFR_NATIVE_ACTION_RIGHT, PFR_NATIVE_EVENT_MOVED);
    step_ok(&core, PFR_NATIVE_ACTION_RIGHT, PFR_NATIVE_EVENT_MOVED);
    step_ok(&core, PFR_NATIVE_ACTION_RIGHT, PFR_NATIVE_EVENT_MOVED);
    step_ok(&core, PFR_NATIVE_ACTION_RIGHT, PFR_NATIVE_EVENT_MOVED);
    step_ok(&core, PFR_NATIVE_ACTION_UP, PFR_NATIVE_EVENT_MOVED);
    step_ok(&core, PFR_NATIVE_ACTION_UP, PFR_NATIVE_EVENT_MOVED);
    step_ok(&core, PFR_NATIVE_ACTION_UP, PFR_NATIVE_EVENT_MOVED);
    step_ok(&core, PFR_NATIVE_ACTION_UP, PFR_NATIVE_EVENT_MOVED);
    state = pfr_native_state(&core);
    assert_true(state->current_map == PFR_NATIVE_MAP_PLAYERS_HOUSE_2F,
                "walking onto the stair tile should stay on 2F");
    assert_true(state->player_x == 10 && state->player_y == 2,
                "unexpected 2F stair tile position");
    step_ok(&core, PFR_NATIVE_ACTION_LEFT, PFR_NATIVE_EVENT_WARPED);
    state = pfr_native_state(&core);
    assert_true(state->current_map == PFR_NATIVE_MAP_PLAYERS_HOUSE_1F,
                "stairs should warp to 1F");
    assert_true(state->player_x == 10 && state->player_y == 2,
                "unexpected 1F warp destination");

    step_ok(&core, PFR_NATIVE_ACTION_DOWN, PFR_NATIVE_EVENT_MOVED);
    step_ok(&core, PFR_NATIVE_ACTION_DOWN, PFR_NATIVE_EVENT_MOVED);
    step_ok(&core, PFR_NATIVE_ACTION_LEFT, PFR_NATIVE_EVENT_MOVED);
    step_ok(&core, PFR_NATIVE_ACTION_A, PFR_NATIVE_EVENT_DIALOG_OPENED);
    state = pfr_native_state(&core);
    assert_true(state->active_dialog_id == PFR_NATIVE_DIALOG_PLAYERS_HOUSE_1F_MOM_MALE,
                "mom interaction should use male opening text");

    assert_true(c_reset(&core, PFR_NATIVE_BOOTSTRAP_PALLET_TOWN, NULL) == 0,
                "pallet bootstrap reset failed");
    state = pfr_native_state(&core);
    assert_true(state->current_map == PFR_NATIVE_MAP_PALLET_TOWN,
                "expected Pallet Town bootstrap map");
    assert_true(state->player_x == 6 && state->player_y == 8,
                "unexpected Pallet Town bootstrap position");
    assert_true(state->player_direction == PFR_NATIVE_DIR_NORTH,
                "unexpected Pallet Town bootstrap facing");

    step_ok(&core, PFR_NATIVE_ACTION_UP, PFR_NATIVE_EVENT_WARPED);
    state = pfr_native_state(&core);
    assert_true(state->current_map == PFR_NATIVE_MAP_PLAYERS_HOUSE_1F,
                "north door entry should warp into the house");
    assert_true(state->player_x == 4 && state->player_y == 8,
                "unexpected Players House 1F door destination");
    assert_true(state->player_direction == PFR_NATIVE_DIR_NORTH,
                "door entry should face north on the south-arrow tile");

    step_ok(&core, PFR_NATIVE_ACTION_DOWN, PFR_NATIVE_EVENT_WARPED);
    state = pfr_native_state(&core);
    assert_true(state->current_map == PFR_NATIVE_MAP_PALLET_TOWN,
                "south arrow exit should warp back outside");
    assert_true(state->player_x == 6 && state->player_y == 7,
                "unexpected Pallet Town door destination");
    assert_true(state->player_direction == PFR_NATIVE_DIR_SOUTH,
                "outside door warp should face south");

    reset_to_map_ok(&core, PFR_NATIVE_MAP_PALLET_TOWN, 12, 0, PFR_NATIVE_DIR_NORTH);
    step_ok(&core, PFR_NATIVE_ACTION_UP, PFR_NATIVE_EVENT_MOVED);
    state = pfr_native_state(&core);
    assert_true(state->current_map == route1_id,
                "north map connection should enter Route1");
    assert_true(state->player_x == 12 && state->player_y == 39,
                "unexpected Route1 connection destination");

    step_ok(&core, PFR_NATIVE_ACTION_DOWN, PFR_NATIVE_EVENT_MOVED);
    state = pfr_native_state(&core);
    assert_true(state->current_map == PFR_NATIVE_MAP_PALLET_TOWN,
                "south map connection should return to Pallet Town");
    assert_true(state->player_x == 12 && state->player_y == 0,
                "unexpected Pallet Town connection return position");

    memset(framebuffer, 0, sizeof(framebuffer));
    c_render(&core, framebuffer, PFR_NATIVE_SCREEN_WIDTH);
    for (i = 0; i < sizeof(framebuffer) / sizeof(framebuffer[0]); i++)
    {
        if (framebuffer[i] != 0)
            nonzero++;
    }
    assert_true(nonzero > 1000, "renderer did not draw enough pixels");

    printf("pfr_native_smoke: ok\n");
    return 0;
}
