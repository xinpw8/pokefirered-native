/* test_pokemon.h -- Pokemon party and starter tests.
 * Uses TEST_ASSERT / TEST_ASSERT_EQ macros and TestEntry registration. */
#ifndef TEST_POKEMON_H
#define TEST_POKEMON_H

#include <stdio.h>
#include <string.h>

/* ---------- bootstrap party -------------------------------------------- */

static int test_pokemon_bootstrap_party(void) {
    PfrNativeCore core;
    c_init(&core);
    c_reset(&core, PFR_NATIVE_BOOTSTRAP_PALLET_TOWN, NULL);

    TEST_ASSERT_EQ(core.state.party_count, 1,
                   "party_count should be 1 after PALLET_TOWN bootstrap");
    TEST_ASSERT_EQ(core.state.starter_mon, 0,
                   "starter_mon should be 0 (Bulbasaur) after bootstrap");
    return 0;
}

/* ---------- party cap -------------------------------------------------- */

static int test_pokemon_party_cap(void) {
    PfrNativeCore core;
    c_init(&core);
    c_reset(&core, PFR_NATIVE_BOOTSTRAP_PALLET_TOWN, NULL);

    /* Max out party */
    core.state.party_count = PFR_NATIVE_MAX_PARTY;

    /* Find Brock for an auto-battle */
    PfrNativeMapId map;
    int16_t ox, oy;
    for (size_t m = 0; m < gPfrNativeMapCount; m++) {
        const PfrNativeMap *mp = &gPfrNativeMaps[m];
        for (size_t o = 0; o < mp->object_event_count; o++) {
            if (mp->object_events[o].script_id == PFR_NATIVE_SCRIPT_PEWTERCITY_GYM_LEADER) {
                map = mp->map_id;
                ox = mp->object_events[o].x;
                oy = mp->object_events[o].y;
                goto found_brock;
            }
        }
    }
    TEST_ASSERT(0, "Brock object not found for party cap test");
found_brock:;

    /* Save state, reset map, restore */
    uint64_t sf = core.state.flags;
    uint16_t sv[PFR_NATIVE_MAX_VARS];
    memcpy(sv, core.state.vars, sizeof(sv));
    uint8_t sp = core.state.party_count;
    uint8_t ss = core.state.starter_mon;

    pfr_native_reset_to_map(&core, map, ox, oy + 1, PFR_NATIVE_DIR_NORTH);
    core.state.flags = sf;
    memcpy(core.state.vars, sv, sizeof(sv));
    core.state.party_count = sp;
    core.state.starter_mon = ss;

    c_step(&core, PFR_NATIVE_ACTION_A);
    for (int i = 0; i < 30; i++) {
        if (core.state.mode != PFR_NATIVE_MODE_DIALOG) break;
        c_step(&core, PFR_NATIVE_ACTION_A);
    }

    TEST_ASSERT_EQ(core.state.party_count, PFR_NATIVE_MAX_PARTY,
                   "party_count should stay at 6 after auto-battle with full party");
    return 0;
}

/* ---------- starter valid ---------------------------------------------- */

static int test_pokemon_starter_valid(void) {
    PfrNativeCore core;
    c_init(&core);
    c_reset(&core, PFR_NATIVE_BOOTSTRAP_PALLET_TOWN, NULL);

    TEST_ASSERT_EQ(core.state.starter_mon, 0,
                   "starter_mon after bootstrap should be 0 (Bulbasaur)");
    TEST_ASSERT(core.state.starter_mon <= 2,
                "starter_mon must be 0, 1, or 2");
    return 0;
}

/* ---------- snapshot preserves party ----------------------------------- */

static int test_pokemon_snapshot_preserves_party(void) {
    PfrNativeCore core;
    c_init(&core);
    c_reset(&core, PFR_NATIVE_BOOTSTRAP_PALLET_TOWN, NULL);

    /* Record original party state */
    uint8_t orig_party = core.state.party_count;
    uint8_t orig_starter = core.state.starter_mon;

    /* Save snapshot */
    PfrNativeSnapshot snap;
    c_save_snapshot(&core, &snap);

    /* Modify party */
    core.state.party_count = 5;
    core.state.starter_mon = 2;  /* Charmander */
    TEST_ASSERT_EQ(core.state.party_count, 5,
                   "party_count should be modified to 5");
    TEST_ASSERT_EQ(core.state.starter_mon, 2,
                   "starter_mon should be modified to 2");

    /* Restore from snapshot */
    c_reset(&core, PFR_NATIVE_BOOTSTRAP_PALLET_TOWN, &snap);

    TEST_ASSERT_EQ(core.state.party_count, orig_party,
                   "party_count not restored from snapshot");
    TEST_ASSERT_EQ(core.state.starter_mon, orig_starter,
                   "starter_mon not restored from snapshot");
    return 0;
}

/* ---------- test registry ---------------------------------------------- */

static const TestEntry pokemon_tests[] = {
    { "pokemon_bootstrap_party",        test_pokemon_bootstrap_party        },
    { "pokemon_party_cap",              test_pokemon_party_cap              },
    { "pokemon_starter_valid",          test_pokemon_starter_valid          },
    { "pokemon_snapshot_preserves_party", test_pokemon_snapshot_preserves_party },
    { NULL, NULL }
};

#endif /* TEST_POKEMON_H */
