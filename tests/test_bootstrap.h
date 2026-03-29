/*
 * test_bootstrap.h -- Tests for bootstrap / reset state correctness.
 */

#ifndef TEST_BOOTSTRAP_H
#define TEST_BOOTSTRAP_H

/* ---------- helpers ---------- */

static int popcount64(uint64_t v)
{
    int c = 0;
    while (v) { c += (int)(v & 1); v >>= 1; }
    return c;
}

/* ---------- tests ---------- */

static int test_bootstrap_pallet_town(void)
{
    PfrNativeCore core;
    const PfrNativeState *s;

    c_init(&core);
    TEST_ASSERT_EQ(c_reset(&core, PFR_NATIVE_BOOTSTRAP_PALLET_TOWN, NULL), 0,
                   "c_reset should succeed");

    s = &core.state;

    /* Position and direction */
    TEST_ASSERT_EQ(s->player_x, 6, "player_x");
    TEST_ASSERT_EQ(s->player_y, 8, "player_y");
    TEST_ASSERT_EQ(s->player_direction, PFR_NATIVE_DIR_NORTH, "direction should be NORTH");
    TEST_ASSERT_EQ(s->current_map, PFR_NATIVE_MAP_PALLET_TOWN, "current_map");

    /* Party / starter */
    TEST_ASSERT_EQ(s->starter_mon, 0, "starter_mon = Bulbasaur");
    TEST_ASSERT_EQ(s->party_count, 1, "party_count = 1");

    /* Mode */
    TEST_ASSERT_EQ(s->mode, PFR_NATIVE_MODE_OVERWORLD, "mode = OVERWORLD");

    /* Flags that SHOULD be set */
    TEST_ASSERT(pfrn_flag_get(s->flags, PFRN_FLAG_SYS_POKEMON_GET),
                "SYS_POKEMON_GET should be set");
    TEST_ASSERT(pfrn_flag_get(s->flags, PFRN_FLAG_SYS_POKEDEX_GET),
                "SYS_POKEDEX_GET should be set");
    TEST_ASSERT(pfrn_flag_get(s->flags, PFRN_FLAG_VISITED_OAKS_LAB),
                "VISITED_OAKS_LAB should be set");
    TEST_ASSERT(pfrn_flag_get(s->flags, PFRN_FLAG_BEAT_RIVAL_IN_OAKS_LAB),
                "BEAT_RIVAL should be set");
    TEST_ASSERT(pfrn_flag_get(s->flags, PFRN_FLAG_GOT_POKEBALLS_FROM_OAK_AFTER_22_RIVAL),
                "GOT_POKEBALLS should be set");
    TEST_ASSERT(pfrn_flag_get(s->flags, PFRN_FLAG_PALLET_LADY_NOT_BLOCKING_SIGN),
                "PALLET_LADY_NOT_BLOCKING_SIGN should be set");
    TEST_ASSERT(pfrn_flag_get(s->flags, PFRN_FLAG_HIDE_OAK_IN_PALLET_TOWN),
                "HIDE_OAK_IN_PALLET_TOWN should be set");
    TEST_ASSERT(pfrn_flag_get(s->flags, PFRN_FLAG_HIDE_BULBASAUR_BALL),
                "HIDE_BULBASAUR_BALL should be set");
    TEST_ASSERT(pfrn_flag_get(s->flags, PFRN_FLAG_HIDE_SQUIRTLE_BALL),
                "HIDE_SQUIRTLE_BALL should be set");
    TEST_ASSERT(pfrn_flag_get(s->flags, PFRN_FLAG_HIDE_CHARMANDER_BALL),
                "HIDE_CHARMANDER_BALL should be set");
    TEST_ASSERT(pfrn_flag_get(s->flags, PFRN_FLAG_HIDE_RIVAL_IN_LAB),
                "HIDE_RIVAL_IN_LAB should be set");
    TEST_ASSERT(pfrn_flag_get(s->flags, PFRN_FLAG_HIDE_POKEDEX),
                "HIDE_POKEDEX should be set");

    /* Flags that should NOT be set */
    for (int b = 0; b < 8; b++) {
        TEST_ASSERT(!pfrn_flag_get(s->flags, PFRN_FLAG_BADGE01_GET + b),
                    "no badges should be set");
    }
    TEST_ASSERT(!pfrn_flag_get(s->flags, PFRN_FLAG_GOT_HM01), "GOT_HM01 not set");
    TEST_ASSERT(!pfrn_flag_get(s->flags, PFRN_FLAG_GOT_HM03), "GOT_HM03 not set");
    TEST_ASSERT(!pfrn_flag_get(s->flags, PFRN_FLAG_GOT_HM04), "GOT_HM04 not set");
    TEST_ASSERT(!pfrn_flag_get(s->flags, PFRN_FLAG_GOT_POKE_FLUTE), "GOT_POKE_FLUTE not set");
    TEST_ASSERT(!pfrn_flag_get(s->flags, PFRN_FLAG_SYS_GAME_CLEAR), "SYS_GAME_CLEAR not set");

    /* Badge count exactly 0 */
    TEST_ASSERT_EQ(pfrn_badge_count(s->flags, PFRN_BADGE_FLAG_START), 0, "badge count == 0");

    /* Total flag popcount -- exactly 12 flags set in bootstrap */
    TEST_ASSERT_EQ(popcount64(s->flags), 12, "total flag popcount == 12");

    /* Vars */
    TEST_ASSERT_EQ(s->vars[PFRN_VAR_STARTER_MON], 0, "VAR_STARTER_MON == 0");
    TEST_ASSERT_EQ(s->vars[PFRN_VAR_MAP_SCENE_PALLET_TOWN_PROFESSOR_OAKS_LAB], 4,
                   "VAR_MAP_SCENE_OAKS_LAB == 4");
    TEST_ASSERT_EQ(s->vars[PFRN_VAR_MAP_SCENE_VIRIDIAN_CITY_MART], 2,
                   "VAR_MAP_SCENE_VIRIDIAN_MART == 2");

    return 0;
}

static int test_bootstrap_players_house_2f(void)
{
    PfrNativeCore core;
    const PfrNativeState *s;

    c_init(&core);
    TEST_ASSERT_EQ(c_reset(&core, PFR_NATIVE_BOOTSTRAP_PLAYERS_HOUSE_2F, NULL), 0,
                   "c_reset should succeed");

    s = &core.state;

    TEST_ASSERT_EQ(s->player_x, 6, "player_x == 6");
    TEST_ASSERT_EQ(s->player_y, 6, "player_y == 6");
    TEST_ASSERT_EQ(s->player_direction, PFR_NATIVE_DIR_NORTH, "direction == NORTH");
    TEST_ASSERT_EQ(s->current_map, PFR_NATIVE_MAP_PLAYERS_HOUSE_2F, "map == PLAYERS_HOUSE_2F");
    TEST_ASSERT_EQ(s->mode, PFR_NATIVE_MODE_OVERWORLD, "mode == OVERWORLD");

    /* House 2F var should be set to 1 */
    TEST_ASSERT_EQ(s->vars[PFRN_VAR_MAP_SCENE_PALLET_TOWN_PLAYERS_HOUSE_2F], 1,
                   "house 2f scene var == 1");

    return 0;
}

static int test_bootstrap_state_size(void)
{
    TEST_ASSERT_EQ((int)pfr_native_state_size(), (int)sizeof(PfrNativeState),
                   "pfr_native_state_size() == sizeof(PfrNativeState)");
    return 0;
}

static int test_bootstrap_snapshot_roundtrip(void)
{
    PfrNativeCore core, core2;
    PfrNativeSnapshot snap;

    c_init(&core);
    c_reset(&core, PFR_NATIVE_BOOTSTRAP_PALLET_TOWN, NULL);

    /* Save snapshot */
    c_save_snapshot(&core, &snap);

    /* Restore into a fresh core */
    c_init(&core2);
    TEST_ASSERT_EQ(c_reset(&core2, 0, &snap), 0, "reset from snapshot succeeds");

    /* Verify state matches */
    TEST_ASSERT_EQ(core2.state.player_x, core.state.player_x, "snapshot player_x matches");
    TEST_ASSERT_EQ(core2.state.player_y, core.state.player_y, "snapshot player_y matches");
    TEST_ASSERT_EQ(core2.state.current_map, core.state.current_map, "snapshot map matches");
    TEST_ASSERT(core2.state.flags == core.state.flags, "snapshot flags match");
    TEST_ASSERT_EQ(core2.state.party_count, core.state.party_count,
                   "snapshot party_count matches");

    return 0;
}

static int test_bootstrap_invalid_id(void)
{
    PfrNativeCore core;
    c_init(&core);
    int result = c_reset(&core, 999, NULL);
    TEST_ASSERT(result != 0, "invalid bootstrap id should return error");
    return 0;
}

/* ---------- Test registration ---------- */

static const TestEntry bootstrap_tests[] = {
    {"bootstrap_pallet_town",       test_bootstrap_pallet_town},
    {"bootstrap_players_house_2f",  test_bootstrap_players_house_2f},
    {"bootstrap_state_size",        test_bootstrap_state_size},
    {"bootstrap_snapshot_roundtrip", test_bootstrap_snapshot_roundtrip},
    {"bootstrap_invalid_id",        test_bootstrap_invalid_id},
};

#endif /* TEST_BOOTSTRAP_H */
