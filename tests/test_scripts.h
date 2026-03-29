/* test_scripts.h — Tests for data-driven script execution system */
#ifndef TEST_SCRIPTS_H
#define TEST_SCRIPTS_H

#include <string.h>

static PfrNativeCore *bootstrap_core(void) {
    static PfrNativeCore c;
    c_init(&c);
    c_reset(&c, PFR_NATIVE_BOOTSTRAP_PALLET_TOWN, NULL);
    return &c;
}

/* Local reimplementation of check_guard (the engine's is static) */
static int test_check_guard(const PfrNativeState *state, const PfrNativeScript *script) {
    switch (script->guard_type) {
    case PFRN_GUARD_NONE:       return 1;
    case PFRN_GUARD_FLAG_SET:   return pfrn_flag_get(state->flags, script->guard_param) != 0;
    case PFRN_GUARD_FLAG_UNSET: return pfrn_flag_get(state->flags, script->guard_param) == 0;
    case PFRN_GUARD_VAR_EQ:     return script->guard_param < PFR_NATIVE_MAX_VARS
                                       && state->vars[script->guard_param] == script->guard_value;
    case PFRN_GUARD_BADGE_GE:   return pfrn_badge_count(state->flags, PFRN_BADGE_FLAG_START)
                                       >= (int)script->guard_value;
    default: return 1;
    }
}

/* Local reimplementation of execute_action (the engine's is static) */
static void test_execute_action(PfrNativeState *state, const PfrNativeScriptAction *act) {
    switch (act->type) {
    case PFRN_ACT_SET_FLAG:
        state->flags = pfrn_flag_set(state->flags, act->param);
        break;
    case PFRN_ACT_SET_VAR:
        if (act->param < PFR_NATIVE_MAX_VARS)
            state->vars[act->param] = act->value;
        break;
    case PFRN_ACT_CLEAR_FLAG:
        state->flags = pfrn_flag_clear(state->flags, act->param);
        break;
    case PFRN_ACT_AUTO_BATTLE:
        if (act->extra != PFRN_FLAG_NONE)
            state->flags = pfrn_flag_set(state->flags, act->extra);
        if (state->party_count < PFR_NATIVE_MAX_PARTY)
            state->party_count++;
        break;
    default:
        break;
    }
}

/* Set a guard flag, call check_guard on a script with FLAG_SET guard -> true */
static int test_script_guard_positive(void) {
    PfrNativeCore *core = bootstrap_core();
    PfrNativeState *state = &core->state;

    /* Find a script with GUARD_FLAG_SET. The CUT_TREE script (24) has
     * guard_type=FLAG_SET, guard_param=PFRN_FLAG_GOT_HM01. */
    const PfrNativeScript *script = &gPfrNativeScripts[PFR_NATIVE_SCRIPT_CUT_TREE];
    TEST_ASSERT(script->guard_type == PFRN_GUARD_FLAG_SET,
                "CUT_TREE script should have FLAG_SET guard");

    /* Set the guard flag */
    state->flags = pfrn_flag_set(state->flags, script->guard_param);
    TEST_ASSERT(test_check_guard(state, script), "guard should pass when flag is set");
    return 0;
}

/* Don't set guard flag -> check_guard returns false */
static int test_script_guard_negative(void) {
    PfrNativeCore *core = bootstrap_core();
    PfrNativeState *state = &core->state;

    const PfrNativeScript *script = &gPfrNativeScripts[PFR_NATIVE_SCRIPT_CUT_TREE];
    TEST_ASSERT(script->guard_type == PFRN_GUARD_FLAG_SET,
                "CUT_TREE script should have FLAG_SET guard");

    /* Ensure the guard flag is NOT set */
    state->flags = pfrn_flag_clear(state->flags, script->guard_param);
    TEST_ASSERT(!test_check_guard(state, script), "guard should fail when flag is not set");
    return 0;
}

/* Set 7 badge flags, check BADGE_GE 7 -> true. With 6 -> false. */
static int test_script_badge_guard(void) {
    PfrNativeCore *core = bootstrap_core();
    PfrNativeState *state = &core->state;

    /* Build a synthetic script with BADGE_GE guard for testing */
    PfrNativeScript synth;
    memset(&synth, 0, sizeof(synth));
    synth.guard_type = PFRN_GUARD_BADGE_GE;
    synth.guard_value = 7;

    /* Clear all badge flags first */
    for (int i = 0; i < 8; i++)
        state->flags = pfrn_flag_clear(state->flags, PFRN_FLAG_BADGE01_GET + i);

    /* Set 7 badge flags (BADGE01 through BADGE07) */
    for (int i = 0; i < 7; i++)
        state->flags = pfrn_flag_set(state->flags, PFRN_FLAG_BADGE01_GET + i);

    TEST_ASSERT(pfrn_badge_count(state->flags, PFRN_BADGE_FLAG_START) == 7,
                "should have 7 badges");
    TEST_ASSERT(test_check_guard(state, &synth), "BADGE_GE 7 should pass with 7 badges");

    /* Remove one badge -> 6 badges, guard should fail */
    state->flags = pfrn_flag_clear(state->flags, PFRN_FLAG_BADGE07_GET);
    TEST_ASSERT(pfrn_badge_count(state->flags, PFRN_BADGE_FLAG_START) == 6,
                "should have 6 badges");
    TEST_ASSERT(!test_check_guard(state, &synth), "BADGE_GE 7 should fail with 6 badges");

    return 0;
}

/* Call execute_action with SET_FLAG, verify flag is set */
static int test_script_execute_set_flag(void) {
    PfrNativeCore *core = bootstrap_core();
    PfrNativeState *state = &core->state;

    /* Clear the flag first */
    state->flags = pfrn_flag_clear(state->flags, PFRN_FLAG_GOT_HM01);
    TEST_ASSERT(!pfrn_flag_get(state->flags, PFRN_FLAG_GOT_HM01),
                "flag should be clear before test");

    PfrNativeScriptAction act;
    memset(&act, 0, sizeof(act));
    act.type = PFRN_ACT_SET_FLAG;
    act.param = PFRN_FLAG_GOT_HM01;

    test_execute_action(state, &act);
    TEST_ASSERT(pfrn_flag_get(state->flags, PFRN_FLAG_GOT_HM01),
                "flag should be set after SET_FLAG action");
    return 0;
}

/* Call execute_action with SET_VAR, verify var is set */
static int test_script_execute_set_var(void) {
    PfrNativeCore *core = bootstrap_core();
    PfrNativeState *state = &core->state;

    PfrNativeScriptAction act;
    memset(&act, 0, sizeof(act));
    act.type = PFRN_ACT_SET_VAR;
    act.param = PFRN_VAR_MAP_SCENE_PEWTER_CITY;
    act.value = 42;

    test_execute_action(state, &act);
    TEST_ASSERT_EQ(state->vars[PFRN_VAR_MAP_SCENE_PEWTER_CITY], 42,
                   "var should be set to 42 after SET_VAR action");
    return 0;
}

/* Call execute_action with AUTO_BATTLE, verify party_count increments and victory flag set */
static int test_script_auto_battle(void) {
    PfrNativeCore *core = bootstrap_core();
    PfrNativeState *state = &core->state;

    uint8_t initial_party = state->party_count;

    /* Clear the victory flag */
    state->flags = pfrn_flag_clear(state->flags, PFRN_FLAG_DEFEATED_BROCK);
    TEST_ASSERT(!pfrn_flag_get(state->flags, PFRN_FLAG_DEFEATED_BROCK),
                "victory flag should be clear before test");

    PfrNativeScriptAction act;
    memset(&act, 0, sizeof(act));
    act.type = PFRN_ACT_AUTO_BATTLE;
    act.param = PFR_NATIVE_DIALOG_NONE;  /* no dialog */
    act.extra = PFRN_FLAG_DEFEATED_BROCK; /* victory flag */

    test_execute_action(state, &act);
    TEST_ASSERT_EQ(state->party_count, initial_party + 1,
                   "party_count should increment after AUTO_BATTLE");
    TEST_ASSERT(pfrn_flag_get(state->flags, PFRN_FLAG_DEFEATED_BROCK),
                "victory flag should be set after AUTO_BATTLE");
    return 0;
}

/* Verify gPfrNativeScriptCount == PFR_NATIVE_SCRIPT_COUNT */
static int test_script_count(void) {
    TEST_ASSERT_EQ((int)gPfrNativeScriptCount, PFR_NATIVE_SCRIPT_COUNT,
                   "gPfrNativeScriptCount should match PFR_NATIVE_SCRIPT_COUNT (95)");
    return 0;
}

static const TestEntry scripts_tests[] = {
    { "script_guard_positive",    test_script_guard_positive },
    { "script_guard_negative",    test_script_guard_negative },
    { "script_badge_guard",       test_script_badge_guard },
    { "script_execute_set_flag",  test_script_execute_set_flag },
    { "script_execute_set_var",   test_script_execute_set_var },
    { "script_auto_battle",       test_script_auto_battle },
    { "script_count",             test_script_count },
    { NULL, NULL }
};

#endif /* TEST_SCRIPTS_H */
