/* test_progression.h -- Full critical path simulation through the game.
 * Uses TEST_ASSERT / TEST_ASSERT_EQ macros and TestEntry registration. */
#ifndef TEST_PROGRESSION_H
#define TEST_PROGRESSION_H

#include <stdio.h>
#include <string.h>

/* ---------- helpers ---------------------------------------------------- */

/* Find an object with the given script_id across all maps. */
static int prog_find_script_object(uint8_t script_id, PfrNativeMapId *out_map,
                                    int16_t *out_x, int16_t *out_y) {
    for (size_t m = 0; m < gPfrNativeMapCount; m++) {
        const PfrNativeMap *map = &gPfrNativeMaps[m];
        for (size_t o = 0; o < map->object_event_count; o++) {
            if (map->object_events[o].script_id == script_id) {
                *out_map = map->map_id;
                *out_x = map->object_events[o].x;
                *out_y = map->object_events[o].y;
                return 1;
            }
        }
    }
    return 0;
}

/* Teleport to a map, stand south of (obj_x, obj_y) facing north,
 * press A, and clear dialog. Preserves flags/vars/party across reset. */
static void prog_interact(PfrNativeCore *core, PfrNativeMapId map_id,
                           int16_t obj_x, int16_t obj_y) {
    uint64_t saved_flags = core->state.flags;
    uint16_t saved_vars[PFR_NATIVE_MAX_VARS];
    memcpy(saved_vars, core->state.vars, sizeof(saved_vars));
    uint8_t saved_party = core->state.party_count;
    uint8_t saved_starter = core->state.starter_mon;

    pfr_native_reset_to_map(core, map_id, obj_x, obj_y + 1, PFR_NATIVE_DIR_NORTH);
    core->state.flags = saved_flags;
    memcpy(core->state.vars, saved_vars, sizeof(saved_vars));
    core->state.party_count = saved_party;
    core->state.starter_mon = saved_starter;

    c_step(core, PFR_NATIVE_ACTION_A);
    for (int i = 0; i < 30; i++) {
        if (core->state.mode != PFR_NATIVE_MODE_DIALOG) break;
        c_step(core, PFR_NATIVE_ACTION_A);
    }
}

/* Find + interact with a script object. Returns 0 on success, 1 if not found. */
static int prog_find_and_interact(PfrNativeCore *core, uint8_t script_id,
                                   const char *label) {
    PfrNativeMapId map;
    int16_t ox, oy;
    if (!prog_find_script_object(script_id, &map, &ox, &oy)) {
        printf("  WARNING: %s (script %d) not found, skipping\n", label, script_id);
        return 1;
    }
    prog_interact(core, map, ox, oy);
    return 0;
}

/* ---------- full critical path ----------------------------------------- */

static int test_progression_critical_path(void) {
    PfrNativeCore core;
    c_init(&core);
    c_reset(&core, PFR_NATIVE_BOOTSTRAP_PALLET_TOWN, NULL);

    PfrNativeMapId map;
    int16_t ox, oy;

    /* Step 1: Bootstrap PALLET_TOWN - verify initial state */
    TEST_ASSERT_EQ(core.state.party_count, 1, "Step 1: party_count should be 1");
    TEST_ASSERT_EQ(core.state.starter_mon, 0, "Step 1: starter should be Bulbasaur");
    TEST_ASSERT(pfrn_flag_get(core.state.flags, PFRN_FLAG_SYS_POKEMON_GET),
                "Step 1: SYS_POKEMON_GET not set");
    TEST_ASSERT(pfrn_flag_get(core.state.flags, PFRN_FLAG_SYS_POKEDEX_GET),
                "Step 1: SYS_POKEDEX_GET not set");

    /* Step 2: Pewter Gym - Badge 1 */
    TEST_ASSERT(prog_find_script_object(PFR_NATIVE_SCRIPT_PEWTERCITY_GYM_LEADER,
                &map, &ox, &oy), "Step 2: Brock not found");
    prog_interact(&core, map, ox, oy);
    TEST_ASSERT(pfrn_flag_get(core.state.flags, PFRN_FLAG_BADGE01_GET),
                "Step 2: Badge 1 not set");
    TEST_ASSERT(pfrn_flag_get(core.state.flags, PFRN_FLAG_DEFEATED_BROCK),
                "Step 2: DEFEATED_BROCK not set");

    /* Step 3: Cerulean Gym - Badge 2 */
    TEST_ASSERT(prog_find_script_object(PFR_NATIVE_SCRIPT_CERULEANCITY_GYM_LEADER,
                &map, &ox, &oy), "Step 3: Misty not found");
    prog_interact(&core, map, ox, oy);
    TEST_ASSERT(pfrn_flag_get(core.state.flags, PFRN_FLAG_BADGE02_GET),
                "Step 3: Badge 2 not set");
    TEST_ASSERT(pfrn_flag_get(core.state.flags, PFRN_FLAG_DEFEATED_MISTY),
                "Step 3: DEFEATED_MISTY not set");

    /* Step 4: Bill - GOT_SS_TICKET */
    TEST_ASSERT(prog_find_script_object(PFR_NATIVE_SCRIPT_BILL_SEA_COTTAGE,
                &map, &ox, &oy), "Step 4: Bill not found");
    prog_interact(&core, map, ox, oy);
    TEST_ASSERT(pfrn_flag_get(core.state.flags, PFRN_FLAG_GOT_SS_TICKET),
                "Step 4: GOT_SS_TICKET not set");

    /* Step 5: SS Anne Captain - GOT_HM01 */
    TEST_ASSERT(prog_find_script_object(PFR_NATIVE_SCRIPT_SS_ANNE_CAPTAIN,
                &map, &ox, &oy), "Step 5: Captain not found");
    prog_interact(&core, map, ox, oy);
    TEST_ASSERT(pfrn_flag_get(core.state.flags, PFRN_FLAG_GOT_HM01),
                "Step 5: GOT_HM01 not set");

    /* Step 6: Vermilion Gym - Badge 3 */
    TEST_ASSERT(prog_find_script_object(PFR_NATIVE_SCRIPT_VERMILIONCITY_GYM_LEADER,
                &map, &ox, &oy), "Step 6: Lt. Surge not found");
    prog_interact(&core, map, ox, oy);
    TEST_ASSERT(pfrn_flag_get(core.state.flags, PFRN_FLAG_BADGE03_GET),
                "Step 6: Badge 3 not set");
    TEST_ASSERT(pfrn_flag_get(core.state.flags, PFRN_FLAG_DEFEATED_LT_SURGE),
                "Step 6: DEFEATED_LT_SURGE not set");

    /* Step 7: Celadon Gym - Badge 4 */
    TEST_ASSERT(prog_find_script_object(PFR_NATIVE_SCRIPT_CELADONCITY_GYM_LEADER,
                &map, &ox, &oy), "Step 7: Erika not found");
    prog_interact(&core, map, ox, oy);
    TEST_ASSERT(pfrn_flag_get(core.state.flags, PFRN_FLAG_BADGE04_GET),
                "Step 7: Badge 4 not set");
    TEST_ASSERT(pfrn_flag_get(core.state.flags, PFRN_FLAG_DEFEATED_ERIKA),
                "Step 7: DEFEATED_ERIKA not set");

    /* Step 8: Get Tea */
    TEST_ASSERT(prog_find_script_object(PFR_NATIVE_SCRIPT_CELADON_GET_TEA,
                &map, &ox, &oy), "Step 8: Tea script not found");
    prog_interact(&core, map, ox, oy);
    TEST_ASSERT(pfrn_flag_get(core.state.flags, PFRN_FLAG_GOT_TEA),
                "Step 8: GOT_TEA not set");

    /* Step 9: Saffron guard - open gates */
    TEST_ASSERT(prog_find_script_object(PFR_NATIVE_SCRIPT_SAFFRON_GUARD_ROUTE5,
                &map, &ox, &oy), "Step 9: Saffron guard not found");
    prog_interact(&core, map, ox, oy);
    /* The guard script sets the gate var to open */
    TEST_ASSERT_EQ(core.state.vars[PFRN_VAR_MAP_SCENE_ROUTE5_ROUTE6_ROUTE7_ROUTE8_GATES], 1,
                   "Step 9: Saffron gates not opened");

    /* Step 10: Game Corner Rocket - OPENED_ROCKET_HIDEOUT */
    TEST_ASSERT(prog_find_script_object(PFR_NATIVE_SCRIPT_GAME_CORNER_ROCKET,
                &map, &ox, &oy), "Step 10: Game Corner Rocket not found");
    prog_interact(&core, map, ox, oy);
    TEST_ASSERT(pfrn_flag_get(core.state.flags, PFRN_FLAG_OPENED_ROCKET_HIDEOUT),
                "Step 10: OPENED_ROCKET_HIDEOUT not set");

    /* Step 11: Rocket Hideout Lift Key - CAN_USE_LIFT */
    TEST_ASSERT(prog_find_script_object(PFR_NATIVE_SCRIPT_ROCKET_HIDEOUT_LIFT_KEY,
                &map, &ox, &oy), "Step 11: Lift key not found");
    prog_interact(&core, map, ox, oy);
    TEST_ASSERT(pfrn_flag_get(core.state.flags, PFRN_FLAG_CAN_USE_ROCKET_HIDEOUT_LIFT),
                "Step 11: CAN_USE_ROCKET_HIDEOUT_LIFT not set");

    /* Step 12: Giovanni hideout - HIDE_SILPH_SCOPE */
    TEST_ASSERT(prog_find_script_object(PFR_NATIVE_SCRIPT_ROCKET_HIDEOUT_GIOVANNI,
                &map, &ox, &oy), "Step 12: Giovanni hideout not found");
    prog_interact(&core, map, ox, oy);
    TEST_ASSERT(pfrn_flag_get(core.state.flags, PFRN_FLAG_HIDE_SILPH_SCOPE),
                "Step 12: HIDE_SILPH_SCOPE not set");

    /* Step 13: Pokemon Tower Mr Fuji - RESCUED + POKE_FLUTE */
    TEST_ASSERT(prog_find_script_object(PFR_NATIVE_SCRIPT_POKEMON_TOWER_MR_FUJI,
                &map, &ox, &oy), "Step 13: Mr Fuji not found");
    prog_interact(&core, map, ox, oy);
    TEST_ASSERT(pfrn_flag_get(core.state.flags, PFRN_FLAG_RESCUED_MR_FUJI),
                "Step 13: RESCUED_MR_FUJI not set");
    /* Poke Flute is given by Lavender Mr Fuji (script 19) after rescue */
    if (prog_find_script_object(PFR_NATIVE_SCRIPT_LAVENDER_MR_FUJI_FLUTE, &map, &ox, &oy) != 0) {
        prog_interact(&core, map, ox, oy);
    }
    TEST_ASSERT(pfrn_flag_get(core.state.flags, PFRN_FLAG_GOT_POKE_FLUTE),
                "Step 13: GOT_POKE_FLUTE not set");

    /* Step 14: Snorlax - HIDE_SNORLAX */
    TEST_ASSERT(prog_find_script_object(PFR_NATIVE_SCRIPT_ROUTE12_SNORLAX,
                &map, &ox, &oy), "Step 14: Snorlax not found");
    prog_interact(&core, map, ox, oy);
    TEST_ASSERT(pfrn_flag_get(core.state.flags, PFRN_FLAG_HIDE_ROUTE_12_SNORLAX),
                "Step 14: HIDE_ROUTE_12_SNORLAX not set");

    /* Step 15: Fuchsia Gym - Badge 5 */
    TEST_ASSERT(prog_find_script_object(PFR_NATIVE_SCRIPT_FUCHSIACITY_GYM_LEADER,
                &map, &ox, &oy), "Step 15: Koga not found");
    prog_interact(&core, map, ox, oy);
    TEST_ASSERT(pfrn_flag_get(core.state.flags, PFRN_FLAG_BADGE05_GET),
                "Step 15: Badge 5 not set");
    TEST_ASSERT(pfrn_flag_get(core.state.flags, PFRN_FLAG_DEFEATED_KOGA),
                "Step 15: DEFEATED_KOGA not set");

    /* Step 16: Warden - GOT_HM04 */
    TEST_ASSERT(prog_find_script_object(PFR_NATIVE_SCRIPT_WARDEN_HM04,
                &map, &ox, &oy), "Step 16: Warden not found");
    prog_interact(&core, map, ox, oy);
    TEST_ASSERT(pfrn_flag_get(core.state.flags, PFRN_FLAG_GOT_HM04),
                "Step 16: GOT_HM04 not set");

    /* Step 17: Saffron Gym - Badge 6 */
    TEST_ASSERT(prog_find_script_object(PFR_NATIVE_SCRIPT_SAFFRONCITY_GYM_LEADER,
                &map, &ox, &oy), "Step 17: Sabrina not found");
    prog_interact(&core, map, ox, oy);
    TEST_ASSERT(pfrn_flag_get(core.state.flags, PFRN_FLAG_BADGE06_GET),
                "Step 17: Badge 6 not set");
    TEST_ASSERT(pfrn_flag_get(core.state.flags, PFRN_FLAG_DEFEATED_SABRINA),
                "Step 17: DEFEATED_SABRINA not set");

    /* Step 18: Pokemon Mansion Secret Key */
    TEST_ASSERT(prog_find_script_object(PFR_NATIVE_SCRIPT_POKEMON_MANSION_SECRET_KEY,
                &map, &ox, &oy), "Step 18: Secret Key not found");
    prog_interact(&core, map, ox, oy);
    TEST_ASSERT(pfrn_flag_get(core.state.flags, PFRN_FLAG_HIDE_POKEMON_MANSION_B1F_SECRET_KEY),
                "Step 18: SECRET_KEY flag not set");

    /* Step 19: Cinnabar Gym - Badge 7 */
    TEST_ASSERT(prog_find_script_object(PFR_NATIVE_SCRIPT_CINNABARISLAND_GYM_LEADER,
                &map, &ox, &oy), "Step 19: Blaine not found");
    prog_interact(&core, map, ox, oy);
    TEST_ASSERT(pfrn_flag_get(core.state.flags, PFRN_FLAG_BADGE07_GET),
                "Step 19: Badge 7 not set");
    TEST_ASSERT(pfrn_flag_get(core.state.flags, PFRN_FLAG_DEFEATED_BLAINE),
                "Step 19: DEFEATED_BLAINE not set");

    /* Step 20: Viridian Gym unlock.
     * The gym unlock is a MAP_SCRIPT_ON_FRAME event, not an object interaction.
     * With 7 badges, entering Viridian City auto-sets the gym door var.
     * Simulate by setting it directly (the engine handles this on map load). */
    TEST_ASSERT_EQ(pfrn_badge_count(core.state.flags, PFRN_BADGE_FLAG_START), 7,
                   "Step 20: should have 7 badges before Viridian unlock");
    core.state.vars[PFRN_VAR_MAP_SCENE_VIRIDIAN_CITY_GYM_DOOR] = 1;

    /* Step 21: Viridian Gym - Badge 8 */
    TEST_ASSERT(prog_find_script_object(PFR_NATIVE_SCRIPT_VIRIDIANCITY_GYM_LEADER,
                &map, &ox, &oy), "Step 21: Giovanni not found");
    prog_interact(&core, map, ox, oy);
    TEST_ASSERT(pfrn_flag_get(core.state.flags, PFRN_FLAG_BADGE08_GET),
                "Step 21: Badge 8 not set");
    TEST_ASSERT(pfrn_flag_get(core.state.flags, PFRN_FLAG_DEFEATED_LEADER_GIOVANNI),
                "Step 21: DEFEATED_LEADER_GIOVANNI not set");

    /* Step 22: Elite Four */
    static const struct {
        uint8_t script_id;
        uint8_t defeat_flag;
        const char *name;
    } e4[] = {
        { PFR_NATIVE_SCRIPT_POKEMONLEAGUE_LORELEISROOM_BATTLE, PFRN_FLAG_DEFEATED_LORELEI, "Lorelei" },
        { PFR_NATIVE_SCRIPT_POKEMONLEAGUE_BRUNOSROOM_BATTLE,     PFRN_FLAG_DEFEATED_BRUNO,   "Bruno"   },
        { PFR_NATIVE_SCRIPT_POKEMONLEAGUE_AGATHASROOM_BATTLE,   PFRN_FLAG_DEFEATED_AGATHA,  "Agatha"  },
        { PFR_NATIVE_SCRIPT_POKEMONLEAGUE_LANCESROOM_BATTLE,     PFRN_FLAG_DEFEATED_LANCE,   "Lance"   },
    };
    for (int i = 0; i < 4; i++) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Step 22: %s not found", e4[i].name);
        TEST_ASSERT(prog_find_script_object(e4[i].script_id, &map, &ox, &oy), msg);
        prog_interact(&core, map, ox, oy);
        snprintf(msg, sizeof(msg), "Step 22: %s defeat flag not set", e4[i].name);
        TEST_ASSERT(pfrn_flag_get(core.state.flags, e4[i].defeat_flag), msg);
    }

    /* Step 23: Final verification */
    TEST_ASSERT_EQ(pfrn_badge_count(core.state.flags, PFRN_BADGE_FLAG_START), 8,
                   "Step 23: should have 8 badges at end");
    TEST_ASSERT(pfrn_flag_get(core.state.flags, PFRN_FLAG_DEFEATED_LORELEI),
                "Step 23: DEFEATED_LORELEI missing");
    TEST_ASSERT(pfrn_flag_get(core.state.flags, PFRN_FLAG_DEFEATED_BRUNO),
                "Step 23: DEFEATED_BRUNO missing");
    TEST_ASSERT(pfrn_flag_get(core.state.flags, PFRN_FLAG_DEFEATED_AGATHA),
                "Step 23: DEFEATED_AGATHA missing");
    TEST_ASSERT(pfrn_flag_get(core.state.flags, PFRN_FLAG_DEFEATED_LANCE),
                "Step 23: DEFEATED_LANCE missing");

    return 0;
}

/* ---------- test registry ---------------------------------------------- */

static const TestEntry progression_tests[] = {
    { "progression_critical_path",  test_progression_critical_path },
    { NULL, NULL }
};

#endif /* TEST_PROGRESSION_H */
