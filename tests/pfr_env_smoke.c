/*
 * pfr_env_smoke.c -- Ground truth verification harness for the PFRN RL
 * environment.
 *
 * Build pattern: same CMake object library as tests/smoke.c.
 * Links against the full pokefirered-native game + pfr_env layer.
 *
 * Runs 8 tests that verify game-specific constants, movement semantics,
 * observation sizes, and reward shaping against known correct values.
 */

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

/* Upstream game headers */
#include "global.h"
#include "gba/gba.h"
#include "main.h"
#include "pokemon.h"
#include "event_data.h"
#include "constants/pokemon.h"
#include "constants/flags.h"
#include "constants/maps.h"
#include "global.fieldmap.h"
#include "fieldmap.h"
#include "battle.h"

/* Host layer headers */
#include "host_memory.h"
#include "host_crt0.h"
#include "host_sound_init.h"
#include "host_flash.h"
#include "host_frame_step.h"
#include "host_savestate.h"
#include "host_agbmain.h"

/* Game subsystem headers needed for boot */
#include "load_save.h"
#include "malloc.h"
#include "quest_log.h"
#include "bg.h"
#include "dma3.h"
#include "save.h"
#include "sound.h"
#include "help_system.h"
#include "new_menu_helpers.h"
#include "save_failed_screen.h"
#include "pokemon_storage_system_internal.h"

/* RL env headers */
#include "pfr_env.h"
#include "pfr_game_api.h"

/* ---- Extern declarations (same as smoke.c) ---- */

extern u16 gKeyRepeatContinueDelay;

void HostPatchBattleScriptPointers(void);
void HostPatchEventScriptPointers(void);
void HostPatchFieldEffectScriptPointers(void);
void HostPatchBattleAIScriptPointers(void);
void HostPatchBattleAnimScriptPointers(void);
void HostScriptPtrTabReset(void);
void EnableVCountIntrAtLine150(void);

/* ---- Stubs ----
 * HostLogSaveStatus and HostDisplayGetFrameCount are already
 * defined in pfr_game_api.c — do NOT redefine them here. */

/* ---- Verified game constants ---- */

/* Tile coordinate semantics:
 *   gSaveBlock1Ptr->pos.x/y are in TILE units (not pixels).
 *   North step: y -= 1
 *   South step: y += 1
 *   West  step: x -= 1
 *   East  step: x += 1
 */

/* New game starts in Player's House 2F: map_group=4, map_num=1 */
#define EXPECTED_START_MAP_GROUP  4
#define EXPECTED_START_MAP_NUM    1

/* MAP_OFFSET: pos + 7 for MapGridGetMetatileBehaviorAt */
#define MAP_OFFSET  7

/* Walking speed: 8 frames/tile (2px/frame).
 * Running speed: 4 frames/tile (4px/frame).
 * PFR_FRAMES_PER_STEP = 4 (defined in pfr_env.h). */

/* PFR_NUM_ACTIONS = 10:
 *   0=none, 1=up, 2=down, 3=left, 4=right,
 *   5=A, 6=B, 7=start, 8=select, 9=L+R */

/* Observation size: 226 bytes = 55 (scalar) + 90 (NPC) + 81 (tile grid) */
#define EXPECTED_OBS_SIZE         226
#define EXPECTED_SCALAR_OBS_SIZE   55
#define EXPECTED_NPC_OBS_EACH       6

/* Reward constants (from pfr_env.c pfr_compute_reward):
 *   exploration +0.02/new_tile
 *   badges      +10.0/badge
 *   levels      +0.1/level
 *   party       +1.0/new_member
 *   NO time penalty, NO same-tile penalty */
#define REWARD_EXPLORATION   0.02f

/* ---- Test infrastructure ---- */

static int g_tests_run    = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define TEST_ASSERT(cond, msg)                                          \
    do {                                                                \
        if (!(cond)) {                                                  \
            fprintf(stderr, "  FAIL: %s (line %d): %s\n",              \
                    __func__, __LINE__, msg);                           \
            return 0;                                                   \
        }                                                               \
    } while (0)

#define TEST_ASSERT_EQ(a, b, msg)                                       \
    do {                                                                \
        if ((a) != (b)) {                                               \
            fprintf(stderr, "  FAIL: %s (line %d): %s "                \
                    "(expected %d, got %d)\n",                          \
                    __func__, __LINE__, msg, (int)(b), (int)(a));       \
            return 0;                                                   \
        }                                                               \
    } while (0)

#define TEST_ASSERT_FLOAT(a, b, msg)                                    \
    do {                                                                \
        if (fabsf((float)(a) - (float)(b)) > 1e-6f) {                  \
            fprintf(stderr, "  FAIL: %s (line %d): %s "                \
                    "(expected %.8f, got %.8f)\n",                      \
                    __func__, __LINE__, msg,                            \
                    (double)(b), (double)(a));                           \
            return 0;                                                   \
        }                                                               \
    } while (0)

#define RUN_TEST(fn)                                                    \
    do {                                                                \
        g_tests_run++;                                                  \
        fprintf(stdout, "Running %s ...\n", #fn);                       \
        if (fn()) {                                                     \
            g_tests_passed++;                                           \
            fprintf(stdout, "  PASS: %s\n", #fn);                       \
        } else {                                                        \
            g_tests_failed++;                                           \
            fprintf(stdout, "  FAIL: %s\n", #fn);                       \
        }                                                               \
    } while (0)

/* ---- Helper: set up a PfrEnv on the stack with local buffers ---- */

static void setup_env(PfrEnv *env, unsigned char *obs_buf,
                      int *action_buf, float *reward_buf,
                      unsigned char *terminal_buf, uint32_t frames_per_step)
{
    memset(obs_buf, 0, PFR_OBS_SIZE);

    /* Use the proper init function */
    pfr_env_init(env, obs_buf, action_buf, reward_buf, terminal_buf,
                 PFR_MAX_STEPS,   /* max_steps */
                 frames_per_step, /* frames_per_step */
                 NULL);           /* savestate_path: use hot state */
}

/* ---- Helper: step the env once with a given action ---- */

static void do_step(PfrEnv *env, int action)
{
    env->actions[0] = action;
    pfr_env_step(env);
}

/* ---- Helper: step game frames directly with a button mask ---- */

static void step_frames_with_buttons(uint16_t buttons, int n_frames)
{
    /* Use pfr_game_step_frames which provides HeadlessFrameLogic
     * (inlines ReadKeys + calls gMain.callback1/callback2).
     * Do NOT use HostFrameStepRunFast(NULL, NULL) — NULL logicFn
     * skips all game logic and key processing. */
    pfr_game_step_frames(buttons, n_frames);
}

/* ================================================================== */
/* TEST 1: Observation struct sizes                                   */
/* ================================================================== */

static int test_obs_size(void)
{
    TEST_ASSERT_EQ((int)sizeof(PfrScalarObs), EXPECTED_SCALAR_OBS_SIZE,
                   "sizeof(PfrScalarObs) must be 55");
    TEST_ASSERT_EQ((int)PFR_OBS_SIZE, EXPECTED_OBS_SIZE,
                   "PFR_OBS_SIZE must be 226");
    TEST_ASSERT_EQ((int)sizeof(PfrNpcObs), EXPECTED_NPC_OBS_EACH,
                   "sizeof(PfrNpcObs) must be 6");
    TEST_ASSERT_EQ(PFR_NUM_ACTIONS, 10,
                   "PFR_NUM_ACTIONS must be 10");
    TEST_ASSERT_EQ(PFR_FRAMES_PER_STEP, 4,
                   "PFR_FRAMES_PER_STEP must be 4");
    TEST_ASSERT_EQ(PFR_TILE_GRID_SIZE, 81,
                   "PFR_TILE_GRID_SIZE must be 81");
    return 1;
}

/* ================================================================== */
/* TEST 2: Initial state (Pallet Town, party count, starter pokemon)  */
/* ================================================================== */

static int test_initial_state(void)
{
    unsigned char obs_buf[PFR_OBS_SIZE];
    int           action_buf = 0;
    float         reward_buf = 0.0f;
    unsigned char terminal_buf = 0;
    PfrEnv env;

    setup_env(&env, obs_buf, &action_buf, &reward_buf,
              &terminal_buf, PFR_FRAMES_PER_STEP);

    /* Reset using the hot savestate left by pfr_game_boot() */
    pfr_env_reset(&env);

    /* Parse the scalar observation block */
    PfrScalarObs *scalar = (PfrScalarObs *)obs_buf;

    TEST_ASSERT_EQ(scalar->map_group, EXPECTED_START_MAP_GROUP,
                   "map_group must be 4 (Player's House 2F)");
    TEST_ASSERT_EQ(scalar->map_num, EXPECTED_START_MAP_NUM,
                   "map_num must be 1 (Player's House 2F)");

    /* New game starts with 0 pokemon — starter is obtained from Oak's lab */
    uint8_t party_count = gSaveBlock1Ptr->playerPartyCount;
    TEST_ASSERT_EQ(party_count, 0,
                   "party_count must be 0 (new game, before getting starter)");

    return 1;
}

/* ================================================================== */
/* TEST 3: Movement north (UP) -- y decreases by 1                    */
/* ================================================================== */

static int test_movement_north(void)
{
    /* Restore to overworld */
    pfr_game_restore_hot();

    int16_t x0 = gSaveBlock1Ptr->pos.x;
    int16_t y0 = gSaveBlock1Ptr->pos.y;
    fprintf(stderr, "  [north] start pos: (%d, %d)\n", x0, y0);

    /* First walk south to make room for a northward step.
     * Use extra frames to ensure completion. */
    step_frames_with_buttons(DPAD_DOWN, 16);
    fprintf(stderr, "  [north] after south walk: (%d, %d)\n",
            gSaveBlock1Ptr->pos.x, gSaveBlock1Ptr->pos.y);

    int16_t y_before = gSaveBlock1Ptr->pos.y;

    /* Turn north (4 frames) then walk north (8 frames) */
    step_frames_with_buttons(DPAD_UP, 4);
    step_frames_with_buttons(DPAD_UP, 8);

    int16_t y_after = gSaveBlock1Ptr->pos.y;
    fprintf(stderr, "  [north] after north walk: (%d, %d), delta=%d\n",
            gSaveBlock1Ptr->pos.x, y_after, y_after - y_before);

    int16_t delta = y_after - y_before;

    TEST_ASSERT_EQ(delta, -1,
                   "1 step north: y must decrease by exactly 1 tile");
    return 1;
}

/* ================================================================== */
/* TEST 4: Movement south (DOWN) -- y increases by 1                  */
/* ================================================================== */

static int test_movement_south(void)
{
    pfr_game_restore_hot();

    int16_t y_before = gSaveBlock1Ptr->pos.y;

    /* Turn south + walk one tile */
    step_frames_with_buttons(DPAD_DOWN, 4);
    step_frames_with_buttons(DPAD_DOWN, 8);

    int16_t y_after = gSaveBlock1Ptr->pos.y;
    int16_t delta = y_after - y_before;

    TEST_ASSERT_EQ(delta, 1,
                   "1 step south: y must increase by exactly 1 tile");
    return 1;
}

/* ================================================================== */
/* TEST 5: Movement east (RIGHT) -- x increases by 1                  */
/* ================================================================== */

static int test_movement_east(void)
{
    pfr_game_restore_hot();

    int16_t x_before = gSaveBlock1Ptr->pos.x;

    /* Turn east + walk one tile */
    step_frames_with_buttons(DPAD_RIGHT, 4);
    step_frames_with_buttons(DPAD_RIGHT, 8);

    int16_t x_after = gSaveBlock1Ptr->pos.x;
    int16_t delta = x_after - x_before;

    TEST_ASSERT_EQ(delta, 1,
                   "1 step east: x must increase by exactly 1 tile");
    return 1;
}

/* ================================================================== */
/* TEST 6: Movement west (LEFT) -- x decreases by 1                  */
/* ================================================================== */

static int test_movement_west(void)
{
    pfr_game_restore_hot();

    int16_t x_before = gSaveBlock1Ptr->pos.x;

    /* Turn west + walk one tile */
    step_frames_with_buttons(DPAD_LEFT, 4);
    step_frames_with_buttons(DPAD_LEFT, 8);

    int16_t x_after = gSaveBlock1Ptr->pos.x;
    int16_t delta = x_after - x_before;

    TEST_ASSERT_EQ(delta, -1,
                   "1 step west: x must decrease by exactly 1 tile");
    return 1;
}

/* ================================================================== */
/* TEST 7: Reward -- exploration (+0.02/new tile), zero on revisit    */
/* ================================================================== */

static int test_reward_exploration(void)
{
    unsigned char obs_buf[PFR_OBS_SIZE];
    int           action_buf = 0;
    float         reward_buf = 0.0f;
    unsigned char terminal_buf = 0;
    PfrEnv env;

    /* Use 12 frames_per_step: 4 for turn + 8 for full tile walk */
    setup_env(&env, obs_buf, &action_buf, &reward_buf,
              &terminal_buf, 12);

    pfr_env_reset(&env);

    /* Walk south via multiple env steps until position changes.
     * Each step is 12 frames with DPAD_DOWN (action=2). */
    int16_t start_y = gSaveBlock1Ptr->pos.y;
    float total_reward = 0.0f;
    int steps_taken = 0;

    for (int i = 0; i < 5; i++) {
        do_step(&env, 2);  /* action 2 = DPAD_DOWN */
        total_reward += env.rewards[0];
        steps_taken++;
        fprintf(stderr, "  [explore] step %d: y=%d reward=%.4f\n",
                i, gSaveBlock1Ptr->pos.y, env.rewards[0]);
        if (gSaveBlock1Ptr->pos.y != start_y)
            break;
    }

    TEST_ASSERT(gSaveBlock1Ptr->pos.y != start_y,
                "player must have moved to a new tile");
    TEST_ASSERT(total_reward > 0.01f,
                "exploration reward must include +0.02 for new tile");

    /* Now walk back UP until we return to start_y (revisited tile) */
    float revisit_reward = 0.0f;
    for (int i = 0; i < 5; i++) {
        do_step(&env, 1);  /* action 1 = DPAD_UP */
        fprintf(stderr, "  [explore] back step %d: y=%d reward=%.4f\n",
                i, gSaveBlock1Ptr->pos.y, env.rewards[0]);
        if (gSaveBlock1Ptr->pos.y == start_y) {
            revisit_reward = env.rewards[0];
            break;
        }
    }

    TEST_ASSERT_FLOAT(revisit_reward, 0.0f,
                      "revisited tile reward must be 0.0 (no penalty)");

    return 1;
}

/* ================================================================== */
/* TEST 8: Reward -- no-op steps yield zero reward                    */
/* ================================================================== */

static int test_reward_noop_zero(void)
{
    unsigned char obs_buf[PFR_OBS_SIZE];
    int           action_buf = 0;
    float         reward_buf = 0.0f;
    unsigned char terminal_buf = 0;
    PfrEnv env;

    setup_env(&env, obs_buf, &action_buf, &reward_buf,
              &terminal_buf, PFR_FRAMES_PER_STEP);

    pfr_env_reset(&env);

    /* Reset marks starting tile as visited via pfr_snapshot_baseline.
     * Subsequent no-op steps on the same tile should yield zero reward
     * (no exploration bonus, no time penalty). */

    for (int i = 0; i < 5; i++)
    {
        do_step(&env, 0);
        float reward = env.rewards[0];

        TEST_ASSERT_FLOAT(reward, 0.0f,
                          "no-op step reward must be exactly 0.0 "
                          "(no time penalty, no exploration)");
    }

    return 1;
}

/* ================================================================== */
/* main: boot game, load state, run all tests                         */
/* ================================================================== */

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    fprintf(stdout, "=== PFRN Environment Smoke Tests ===\n\n");

    /* ---- Boot the game ----
     * pfr_game_boot() handles the full initialization sequence:
     *   - HostMemoryInit, HostCrt0Init, HostSoundInit, HostFlashInit
     *   - HostAgbMainInit (runs AgbMain equivalent)
     *   - Runs enough frames to get through intro/title/Oak's speech
     *     to the overworld
     *   - Captures a hot savestate at the overworld for fast reset
     *
     * If pfr_game_boot() is not available (link error), the test
     * runner must be linked against the full game + pfr_game_api. */
    /* pfr_game_boot() initializes hardware, runs through stubs
     * (intro/title/oak/newgame), and captures a hot savestate at
     * the overworld. No splash screens, no "press START". */
    fprintf(stdout, "Booting game...\n");
    pfr_game_boot();
    fprintf(stdout, "Game booted. Hot savestate captured at overworld.\n\n");

    /* ---- Run all tests ---- */

    /* Test 1: compile-time / sizeof checks (no game state needed) */
    RUN_TEST(test_obs_size);

    /* Test 2: initial state verification */
    RUN_TEST(test_initial_state);

    /* Tests 3-6: movement in each cardinal direction */
    RUN_TEST(test_movement_north);
    RUN_TEST(test_movement_south);
    RUN_TEST(test_movement_east);
    RUN_TEST(test_movement_west);

    /* Tests 7-8: reward verification */
    RUN_TEST(test_reward_exploration);
    RUN_TEST(test_reward_noop_zero);

    /* ---- Summary ---- */
    fprintf(stdout, "\n=== Results: %d/%d passed",
            g_tests_passed, g_tests_run);
    if (g_tests_failed > 0)
        fprintf(stdout, " (%d FAILED)", g_tests_failed);
    fprintf(stdout, " ===\n");

    return g_tests_failed > 0 ? 1 : 0;
}
