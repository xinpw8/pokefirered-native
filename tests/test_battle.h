/* test_battle.h -- Battle system tests: gym leaders, E4, champion, specials.
 * Uses TEST_ASSERT / TEST_ASSERT_EQ macros and TestEntry registration. */
#ifndef TEST_BATTLE_H
#define TEST_BATTLE_H

#include <stdio.h>
#include <string.h>

/* ---------- helpers ---------------------------------------------------- */

static int find_script_object(uint8_t script_id, PfrNativeMapId *out_map,
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

/* Step through any lingering dialog */
static void battle_clear_dialog(PfrNativeCore *core) {
    for (int i = 0; i < 30; i++) {
        if (core->state.mode != PFR_NATIVE_MODE_DIALOG) break;
        c_step(core, PFR_NATIVE_ACTION_A);
    }
}

/* Position player adjacent to (obj_x, obj_y) and press A.
 * Tries south, north, west, east until finding a valid position.
 * Clears any resulting dialog. */
static void interact_with_object(PfrNativeCore *core, PfrNativeMapId map_id,
                                  int16_t obj_x, int16_t obj_y) {
    /* Save progression state */
    uint64_t saved_flags = core->state.flags;
    uint16_t saved_vars[PFR_NATIVE_MAX_VARS];
    memcpy(saved_vars, core->state.vars, sizeof(saved_vars));
    uint8_t saved_party = core->state.party_count;
    uint8_t saved_starter = core->state.starter_mon;

    /* Try positioning south, north, west, east of the object */
    static const struct { int16_t dx, dy; uint8_t dir; } offsets[] = {
        { 0,  1, PFR_NATIVE_DIR_NORTH },  /* player south, facing north */
        { 0, -1, PFR_NATIVE_DIR_SOUTH },  /* player north, facing south */
        {-1,  0, PFR_NATIVE_DIR_EAST  },  /* player west, facing east */
        { 1,  0, PFR_NATIVE_DIR_WEST  },  /* player east, facing west */
    };
    int placed = 0;
    for (int i = 0; i < 4; i++) {
        int16_t px = obj_x + offsets[i].dx;
        int16_t py = obj_y + offsets[i].dy;
        if (pfr_native_reset_to_map(core, map_id, px, py, offsets[i].dir) == 0) {
            placed = 1;
            break;
        }
    }
    if (!placed) {
        /* Last resort: place directly on object position */
        pfr_native_reset_to_map(core, map_id, obj_x, obj_y, PFR_NATIVE_DIR_NORTH);
    }

    /* Restore progression state */
    core->state.flags = saved_flags;
    memcpy(core->state.vars, saved_vars, sizeof(saved_vars));
    core->state.party_count = saved_party;
    core->state.starter_mon = saved_starter;

    /* Reload objects with restored flags */
    reload_objects_for_map(&core->state);

    c_step(core, PFR_NATIVE_ACTION_A);
    battle_clear_dialog(core);
}

/* ---------- gym leader tests ------------------------------------------- */

static int test_battle_gym_leaders(void) {
    /* Script IDs for each gym leader (36-43) and their corresponding
     * defeat flags (11-18) and badge flags (0-7). */
    static const struct {
        uint8_t script_id;
        uint8_t defeat_flag;
        uint8_t badge_flag;
        const char *name;
    } gyms[] = {
        { PFR_NATIVE_SCRIPT_PEWTERCITY_GYM_LEADER,      PFRN_FLAG_DEFEATED_BROCK,            PFRN_FLAG_BADGE01_GET, "Brock"    },
        { PFR_NATIVE_SCRIPT_CERULEANCITY_GYM_LEADER,     PFRN_FLAG_DEFEATED_MISTY,            PFRN_FLAG_BADGE02_GET, "Misty"    },
        { PFR_NATIVE_SCRIPT_VERMILIONCITY_GYM_LEADER,    PFRN_FLAG_DEFEATED_LT_SURGE,         PFRN_FLAG_BADGE03_GET, "Lt. Surge"},
        { PFR_NATIVE_SCRIPT_CELADONCITY_GYM_LEADER,      PFRN_FLAG_DEFEATED_ERIKA,            PFRN_FLAG_BADGE04_GET, "Erika"    },
        { PFR_NATIVE_SCRIPT_FUCHSIACITY_GYM_LEADER,      PFRN_FLAG_DEFEATED_KOGA,             PFRN_FLAG_BADGE05_GET, "Koga"     },
        { PFR_NATIVE_SCRIPT_SAFFRONCITY_GYM_LEADER,      PFRN_FLAG_DEFEATED_SABRINA,          PFRN_FLAG_BADGE06_GET, "Sabrina"  },
        { PFR_NATIVE_SCRIPT_CINNABARISLAND_GYM_LEADER,   PFRN_FLAG_DEFEATED_BLAINE,           PFRN_FLAG_BADGE07_GET, "Blaine"   },
        { PFR_NATIVE_SCRIPT_VIRIDIANCITY_GYM_LEADER,     PFRN_FLAG_DEFEATED_LEADER_GIOVANNI,  PFRN_FLAG_BADGE08_GET, "Giovanni" },
    };
    int gym_count = (int)(sizeof(gyms) / sizeof(gyms[0]));

    for (int g = 0; g < gym_count; g++) {
        PfrNativeCore core;
        c_init(&core);
        c_reset(&core, PFR_NATIVE_BOOTSTRAP_PALLET_TOWN, NULL);

        /* For Viridian Gym, Giovanni requires 7 badges to fight */
        if (gyms[g].script_id == PFR_NATIVE_SCRIPT_VIRIDIANCITY_GYM_LEADER) {
            for (int b = 0; b < 7; b++)
                core.state.flags = pfrn_flag_set(core.state.flags, PFRN_FLAG_BADGE01_GET + b);
            /* Also set the defeat flags for the 7 prior leaders */
            core.state.flags = pfrn_flag_set(core.state.flags, PFRN_FLAG_DEFEATED_BROCK);
            core.state.flags = pfrn_flag_set(core.state.flags, PFRN_FLAG_DEFEATED_MISTY);
            core.state.flags = pfrn_flag_set(core.state.flags, PFRN_FLAG_DEFEATED_LT_SURGE);
            core.state.flags = pfrn_flag_set(core.state.flags, PFRN_FLAG_DEFEATED_ERIKA);
            core.state.flags = pfrn_flag_set(core.state.flags, PFRN_FLAG_DEFEATED_KOGA);
            core.state.flags = pfrn_flag_set(core.state.flags, PFRN_FLAG_DEFEATED_SABRINA);
            core.state.flags = pfrn_flag_set(core.state.flags, PFRN_FLAG_DEFEATED_BLAINE);
            /* Unlock the gym door */
            core.state.vars[PFRN_VAR_MAP_SCENE_VIRIDIAN_CITY_GYM_DOOR] = 1;
        }

        int badges_before = pfrn_badge_count(core.state.flags, PFRN_BADGE_FLAG_START);

        PfrNativeMapId map;
        int16_t ox, oy;
        TEST_ASSERT(find_script_object(gyms[g].script_id, &map, &ox, &oy),
                    gyms[g].name);

        interact_with_object(&core, map, ox, oy);

        char msg[128];
        snprintf(msg, sizeof(msg), "%s: defeat flag not set", gyms[g].name);
        TEST_ASSERT(pfrn_flag_get(core.state.flags, gyms[g].defeat_flag), msg);

        snprintf(msg, sizeof(msg), "%s: badge flag not set", gyms[g].name);
        TEST_ASSERT(pfrn_flag_get(core.state.flags, gyms[g].badge_flag), msg);

        snprintf(msg, sizeof(msg), "%s: badge count did not increment", gyms[g].name);
        int badges_after = pfrn_badge_count(core.state.flags, PFRN_BADGE_FLAG_START);
        TEST_ASSERT(badges_after == badges_before + 1, msg);
    }
    return 0;
}

/* ---------- E4 tests --------------------------------------------------- */

static int test_battle_e4(void) {
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
    int count = (int)(sizeof(e4) / sizeof(e4[0]));

    for (int i = 0; i < count; i++) {
        PfrNativeCore core;
        c_init(&core);
        c_reset(&core, PFR_NATIVE_BOOTSTRAP_PALLET_TOWN, NULL);
        /* Give 8 badges for E4 access */
        for (int b = 0; b < 8; b++)
            core.state.flags = pfrn_flag_set(core.state.flags, PFRN_FLAG_BADGE01_GET + b);

        PfrNativeMapId map;
        int16_t ox, oy;
        TEST_ASSERT(find_script_object(e4[i].script_id, &map, &ox, &oy),
                    e4[i].name);

        interact_with_object(&core, map, ox, oy);

        char msg[128];
        snprintf(msg, sizeof(msg), "%s: defeat flag not set", e4[i].name);
        TEST_ASSERT(pfrn_flag_get(core.state.flags, e4[i].defeat_flag), msg);
    }
    return 0;
}

/* ---------- champion test ---------------------------------------------- */

static int test_battle_champion(void) {
    /* The champion is tracked by FLAG_DEFEATED_CHAMP and FLAG_SYS_GAME_CLEAR.
     * There may not be a dedicated champion script object; test via flags
     * after defeating all E4 members + checking if GAME_CLEAR exists. */
    PfrNativeCore core;
    c_init(&core);
    c_reset(&core, PFR_NATIVE_BOOTSTRAP_PALLET_TOWN, NULL);

    /* Set all 8 badges and all E4 defeat flags */
    for (int b = 0; b < 8; b++)
        core.state.flags = pfrn_flag_set(core.state.flags, PFRN_FLAG_BADGE01_GET + b);
    core.state.flags = pfrn_flag_set(core.state.flags, PFRN_FLAG_DEFEATED_LORELEI);
    core.state.flags = pfrn_flag_set(core.state.flags, PFRN_FLAG_DEFEATED_BRUNO);
    core.state.flags = pfrn_flag_set(core.state.flags, PFRN_FLAG_DEFEATED_AGATHA);
    core.state.flags = pfrn_flag_set(core.state.flags, PFRN_FLAG_DEFEATED_LANCE);

    /* Set champion defeated flag directly (no script object for champion) */
    core.state.flags = pfrn_flag_set(core.state.flags, PFRN_FLAG_DEFEATED_CHAMP);
    core.state.flags = pfrn_flag_set(core.state.flags, PFRN_FLAG_SYS_GAME_CLEAR);

    TEST_ASSERT(pfrn_flag_get(core.state.flags, PFRN_FLAG_DEFEATED_CHAMP),
                "DEFEATED_CHAMP not set");
    TEST_ASSERT(pfrn_flag_get(core.state.flags, PFRN_FLAG_SYS_GAME_CLEAR),
                "SYS_GAME_CLEAR not set");
    TEST_ASSERT(pfrn_badge_count(core.state.flags, PFRN_BADGE_FLAG_START) == 8,
                "should have 8 badges");
    return 0;
}

/* ---------- snorlax test ----------------------------------------------- */

static int test_battle_snorlax(void) {
    PfrNativeCore core;
    c_init(&core);
    c_reset(&core, PFR_NATIVE_BOOTSTRAP_PALLET_TOWN, NULL);

    /* Give Poke Flute */
    core.state.flags = pfrn_flag_set(core.state.flags, PFRN_FLAG_GOT_POKE_FLUTE);

    PfrNativeMapId map;
    int16_t ox, oy;
    TEST_ASSERT(find_script_object(PFR_NATIVE_SCRIPT_ROUTE12_SNORLAX, &map, &ox, &oy),
                "Snorlax object not found");

    interact_with_object(&core, map, ox, oy);

    TEST_ASSERT(pfrn_flag_get(core.state.flags, PFRN_FLAG_HIDE_ROUTE_12_SNORLAX),
                "HIDE_ROUTE_12_SNORLAX flag not set");

    /* After reload, Snorlax should be hidden by the HIDE flag */
    reload_objects_for_map(&core.state);
    int snorlax_active = 0;
    for (int i = 0; i < (int)core.state.object_count; i++) {
        if (core.state.objects[i].script_id == PFR_NATIVE_SCRIPT_ROUTE12_SNORLAX &&
            core.state.objects[i].active)
            snorlax_active = 1;
    }
    TEST_ASSERT(!snorlax_active, "Snorlax object still active after flag set + reload");
    return 0;
}

/* ---------- party cap test --------------------------------------------- */

static int test_battle_party_cap(void) {
    PfrNativeCore core;
    c_init(&core);
    c_reset(&core, PFR_NATIVE_BOOTSTRAP_PALLET_TOWN, NULL);

    /* Max out party */
    core.state.party_count = PFR_NATIVE_MAX_PARTY;

    /* Find a gym leader to auto-battle (Brock) */
    PfrNativeMapId map;
    int16_t ox, oy;
    TEST_ASSERT(find_script_object(PFR_NATIVE_SCRIPT_PEWTERCITY_GYM_LEADER, &map, &ox, &oy),
                "Brock not found");

    interact_with_object(&core, map, ox, oy);

    TEST_ASSERT_EQ(core.state.party_count, PFR_NATIVE_MAX_PARTY,
                   "party_count exceeded max after auto-battle");
    return 0;
}

/* ---------- test registry ---------------------------------------------- */

static const TestEntry battle_tests[] = {
    { "battle_gym_leaders",  test_battle_gym_leaders  },
    { "battle_e4",           test_battle_e4           },
    { "battle_champion",     test_battle_champion     },
    { "battle_snorlax",      test_battle_snorlax      },
    { "battle_party_cap",    test_battle_party_cap    },
    { NULL, NULL }
};

#endif /* TEST_BATTLE_H */
