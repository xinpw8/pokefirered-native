/* test_progression.c — Automated tests for pfr_native progression system.
 * Teleports player to each critical location and verifies state transitions. */
#include "pfr_native.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

#define TEST(name) do { printf("  %-50s ", name); } while(0)
#define PASS() do { printf("PASS\n"); passed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); failed++; } while(0)
#define CHECK(cond, msg) do { if (cond) { PASS(); } else { FAIL(msg); } } while(0)

static int passed = 0, failed = 0;

/* Step until not in dialog mode (press A repeatedly) */
static void clear_dialog(PfrNativeCore *core) {
    for (int i = 0; i < 20; i++) {
        if (core->state.mode != PFR_NATIVE_MODE_DIALOG) break;
        c_step(core, PFR_NATIVE_ACTION_A);
    }
}

/* Press A facing the tile at (x, y) from the given direction */
static void interact_at(PfrNativeCore *core, int16_t x, int16_t y, uint8_t dir) {
    /* Position player adjacent to target */
    int16_t px = x, py = y;
    switch (dir) {
        case PFR_NATIVE_DIR_NORTH: py = y + 1; break;
        case PFR_NATIVE_DIR_SOUTH: py = y - 1; break;
        case PFR_NATIVE_DIR_WEST:  px = x + 1; break;
        case PFR_NATIVE_DIR_EAST:  px = x - 1; break;
    }
    core->state.player_x = px;
    core->state.player_y = py;
    core->state.player_direction = dir;
    c_step(core, PFR_NATIVE_ACTION_A);
    clear_dialog(core);
}

int main(void) {
    PfrNativeCore core;

    printf("=== pfr_native progression tests ===\n\n");

    /* --- Bootstrap state tests --- */
    printf("[Bootstrap state]\n");
    c_init(&core);
    c_reset(&core, PFR_NATIVE_BOOTSTRAP_PALLET_TOWN, NULL);

    TEST("Starts in Pallet Town");
    CHECK(core.state.current_map == PFR_NATIVE_MAP_PALLET_TOWN, "wrong map");

    TEST("Has starter (party_count=1, starter=Bulbasaur)");
    CHECK(core.state.party_count == 1 && core.state.starter_mon == 0,
          "wrong party/starter");

    TEST("FLAG_SYS_POKEMON_GET set");
    CHECK(pfrn_flag_get(core.state.flags, PFRN_FLAG_SYS_POKEMON_GET), "not set");

    TEST("FLAG_SYS_POKEDEX_GET set");
    CHECK(pfrn_flag_get(core.state.flags, PFRN_FLAG_SYS_POKEDEX_GET), "not set");

    TEST("FLAG_BEAT_RIVAL_IN_OAKS_LAB set");
    CHECK(pfrn_flag_get(core.state.flags, PFRN_FLAG_BEAT_RIVAL_IN_OAKS_LAB), "not set");

    TEST("Oak hidden in Pallet Town overworld");
    CHECK(pfrn_flag_get(core.state.flags, PFRN_FLAG_HIDE_OAK_IN_PALLET_TOWN), "not set");

    TEST("No running shoes at start");
    CHECK(!pfrn_flag_get(core.state.flags, PFRN_FLAG_SYS_B_DASH), "should not be set");

    TEST("Badge count = 0");
    CHECK(pfrn_badge_count(core.state.flags, PFRN_BADGE_FLAG_START) == 0, "wrong count");

    /* Verify Oak NPC is hidden */
    {
        int oak_visible = 0;
        for (int i = 0; i < (int)core.state.object_count; i++) {
            /* Oak's graphics_id = 71 (ProfOak) */
            if (core.state.objects[i].graphics_id == 71 && core.state.objects[i].active)
                oak_visible = 1;
        }
        TEST("Oak NPC not active on map");
        CHECK(!oak_visible, "Oak still visible");
    }

    /* --- Mom dialog test (should NOT deactivate her) --- */
    printf("\n[Mom dialog]\n");
    c_init(&core);
    c_reset(&core, PFR_NATIVE_BOOTSTRAP_PLAYERS_HOUSE_2F, NULL);
    /* Mom is not in 2F. Let's go to 1F */
    c_reset(&core, PFR_NATIVE_BOOTSTRAP_PALLET_TOWN, NULL);
    pfr_native_reset_to_map(&core, PFR_NATIVE_MAP_PLAYERS_HOUSE_1F, 5, 6, PFR_NATIVE_DIR_NORTH);
    /* Inherit bootstrap flags */

    /* Find Mom — she has script_id for MOM script */
    int mom_idx = -1;
    for (int i = 0; i < (int)core.state.object_count; i++) {
        if (core.state.objects[i].active &&
            core.state.objects[i].script_id == PFR_NATIVE_SCRIPT_PLAYERS_HOUSE_1F_MOM)
            mom_idx = i;
    }
    TEST("Mom found on map");
    CHECK(mom_idx >= 0, "Mom not found");

    if (mom_idx >= 0) {
        interact_at(&core, core.state.objects[mom_idx].x,
                    core.state.objects[mom_idx].y, PFR_NATIVE_DIR_NORTH);
        TEST("Mom still active after interaction");
        CHECK(core.state.objects[mom_idx].active, "Mom was deactivated!");
    }

    /* --- Gym leader auto-battle test --- */
    printf("\n[Gym battles]\n");
    c_init(&core);
    c_reset(&core, PFR_NATIVE_BOOTSTRAP_PALLET_TOWN, NULL);

    /* Teleport to Pewter Gym, interact with Brock */
    {
        PfrNativeMapId gym = pfr_native_find_map_by_name("PewterCity_Gym");
        if (gym != PFR_NATIVE_MAP_INVALID) {
            pfr_native_reset_to_map(&core, gym, 6, 6, PFR_NATIVE_DIR_NORTH);
            /* Brock is at (6, 5) */
            interact_at(&core, 6, 5, PFR_NATIVE_DIR_NORTH);
            TEST("Badge 1 set after Brock interaction");
            CHECK(pfrn_flag_get(core.state.flags, PFRN_FLAG_BADGE01_GET), "badge not set");
            TEST("DEFEATED_BROCK set");
            CHECK(pfrn_flag_get(core.state.flags, PFRN_FLAG_DEFEATED_BROCK), "not set");
            TEST("Badge count = 1");
            CHECK(pfrn_badge_count(core.state.flags, PFRN_BADGE_FLAG_START) == 1,
                  "wrong count");
        } else {
            TEST("Pewter Gym map found");
            FAIL("map not found");
        }
    }

    /* --- Cut tree test --- */
    printf("\n[Cut trees]\n");
    c_init(&core);
    c_reset(&core, PFR_NATIVE_BOOTSTRAP_PALLET_TOWN, NULL);

    /* Teleport to Viridian City — has cut trees */
    {
        PfrNativeMapId vc = pfr_native_find_map_by_name("ViridianCity");
        if (vc != PFR_NATIVE_MAP_INVALID) {
            pfr_native_reset_to_map(&core, vc, 11, 25, PFR_NATIVE_DIR_NORTH);
            /* Cut tree at (11, 24) — find it */
            int tree_idx = -1;
            for (int i = 0; i < (int)core.state.object_count; i++) {
                if (core.state.objects[i].active &&
                    core.state.objects[i].x == 11 && core.state.objects[i].y == 24)
                    tree_idx = i;
            }
            TEST("Cut tree found at (11,24)");
            CHECK(tree_idx >= 0, "not found");

            if (tree_idx >= 0) {
                /* Without HM01: tree should NOT disappear */
                interact_at(&core, 11, 24, PFR_NATIVE_DIR_NORTH);
                TEST("Tree stays without HM01");
                CHECK(core.state.objects[tree_idx].active, "tree vanished without HM!");

                /* Give HM01 and try again */
                core.state.flags = pfrn_flag_set(core.state.flags, PFRN_FLAG_GOT_HM01);
                interact_at(&core, 11, 24, PFR_NATIVE_DIR_NORTH);
                TEST("Tree disappears with HM01");
                CHECK(!core.state.objects[tree_idx].active, "tree still there with HM!");
            }
        } else {
            TEST("Viridian City map found");
            FAIL("map not found");
        }
    }

    /* --- Snorlax test --- */
    printf("\n[Snorlax]\n");
    c_init(&core);
    c_reset(&core, PFR_NATIVE_BOOTSTRAP_PALLET_TOWN, NULL);
    {
        PfrNativeMapId r12 = pfr_native_find_map_by_name("Route12");
        if (r12 != PFR_NATIVE_MAP_INVALID) {
            pfr_native_reset_to_map(&core, r12, 14, 71, PFR_NATIVE_DIR_NORTH);
            int snor_idx = -1;
            for (int i = 0; i < (int)core.state.object_count; i++) {
                if (core.state.objects[i].active &&
                    core.state.objects[i].x == 14 && core.state.objects[i].y == 70)
                    snor_idx = i;
            }
            TEST("Snorlax found on Route 12");
            if (snor_idx >= 0) {
                PASS();
                /* Without poke flute: stays */
                interact_at(&core, 14, 70, PFR_NATIVE_DIR_NORTH);
                TEST("Snorlax stays without Poke Flute");
                CHECK(core.state.objects[snor_idx].active, "snorlax vanished!");

                /* With poke flute: hides */
                core.state.flags = pfrn_flag_set(core.state.flags, PFRN_FLAG_GOT_POKE_FLUTE);
                core.state.player_x = 14;
                core.state.player_y = 71;
                core.state.player_direction = PFR_NATIVE_DIR_NORTH;
                c_step(&core, PFR_NATIVE_ACTION_A);
                clear_dialog(&core);
                TEST("Snorlax hide flag set with Poke Flute");
                CHECK(pfrn_flag_get(core.state.flags, PFRN_FLAG_HIDE_ROUTE_12_SNORLAX),
                      "flag not set");
            } else {
                FAIL("not found at expected position");
            }
        } else {
            TEST("Route 12 map found");
            FAIL("map not found");
        }
    }

    printf("\n=== Results: %d passed, %d failed ===\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
