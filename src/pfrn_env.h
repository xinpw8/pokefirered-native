/*
 * pfrn_env.h -- PufferLib ocean env for pokefirered-native
 *
 * Follows the NMMO3 pattern: pure C env with c_reset/c_step/c_close/c_render.
 * The env_binding.h from PufferLib 4.0 handles all vectorization via OpenMP.
 *
 * Observation layout (226 bytes, flat unsigned char):
 *   [0..54]    PfrnScalarObs    (55 bytes) -- player pos, map, party, badges, etc.
 *   [55..144]  PfrnNpcObs[15]   (90 bytes) -- nearby NPCs
 *   [145..225] tile_grid[81]    (81 bytes) -- 9x9 metatile behaviors
 *
 * Action space: Discrete(8)
 *   0=noop, 1=up, 2=down, 3=left, 4=right, 5=A, 6=B, 7=start
 */

#ifndef PFRN_ENV_H
#define PFRN_ENV_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#include "pfr_so_instance.h"

/* ---- Observation dimensions ---- */

#define PFRN_TILE_RADIUS     4
#define PFRN_TILE_DIM        (2 * PFRN_TILE_RADIUS + 1)  /* 9 */
#define PFRN_TILE_GRID_SIZE  (PFRN_TILE_DIM * PFRN_TILE_DIM)  /* 81 */

#define PFRN_MAX_PARTY       6
#define PFRN_MAX_NPCS        15
#define PFRN_NPC_FEATURES    6

#define PFRN_NUM_BADGES      8
#define PFRN_NUM_ACTIONS     8

#define PFRN_FRAMES_PER_STEP 4
#define PFRN_MAX_STEPS       24576

/* ---- Observation structs (packed, little-endian) ---- */

typedef struct __attribute__((packed)) {
    /* Player location (4 bytes) */
    int16_t  player_x;
    int16_t  player_y;

    /* Map identity (3 bytes) */
    uint8_t  map_group;
    uint8_t  map_num;
    uint8_t  map_layout_id;

    /* Player state (4 bytes) */
    uint8_t  player_direction;
    uint8_t  player_avatar_flags;
    uint8_t  player_running_state;
    uint8_t  player_transition_state;

    /* Game mode flags (2 bytes) */
    uint8_t  in_battle;
    uint8_t  battle_outcome;

    /* Party summary: 6 mons x 6 bytes = 36 bytes */
    struct __attribute__((packed)) {
        uint16_t species;     /* 0 = empty slot */
        uint8_t  level;
        uint8_t  hp_pct;      /* hp * 255 / maxHP */
        uint8_t  status;
        uint8_t  type1;
    } party[PFRN_MAX_PARTY];

    /* Badges (1 byte packed bitfield) */
    uint8_t  badges;

    /* Money (2 bytes, capped at 65535) */
    uint16_t money;

    /* Weather (1 byte) */
    uint8_t  weather;

    /* Step counter (2 bytes) */
    uint16_t step_counter;

} PfrnScalarObs;
/* Expected: 4 + 3 + 4 + 2 + 36 + 1 + 2 + 1 + 2 = 55 bytes */

typedef struct __attribute__((packed)) {
    int8_t   dx;
    int8_t   dy;
    uint8_t  graphics_id;
    uint8_t  direction;
    uint8_t  active;
    uint8_t  movement_type;
} PfrnNpcObs;
/* 6 bytes per NPC, 15 NPCs = 90 bytes */

/* Total obs = 55 + 90 + 81 = 226 */
#define PFRN_SCALAR_OBS_SIZE  sizeof(PfrnScalarObs)
#define PFRN_NPC_OBS_SIZE     (PFRN_MAX_NPCS * sizeof(PfrnNpcObs))
#define PFRN_OBS_SIZE         (PFRN_SCALAR_OBS_SIZE + PFRN_NPC_OBS_SIZE + PFRN_TILE_GRID_SIZE)

/* ---- Exploration tracking ---- */

#define PFRN_VISIT_HASH_SIZE  4096

typedef struct {
    uint32_t visit_hash[PFRN_VISIT_HASH_SIZE / 32];
    uint32_t visit_count;
    uint32_t prev_visit_count;

    uint8_t  prev_badges;
    uint8_t  prev_party_count;
    uint16_t prev_party_level_sum;
    uint32_t prev_money;

    uint32_t step_count;
    float    episode_return;
} PfrnRewardState;

/* ---- Log struct (PufferLib: ALL floats) ---- */

typedef struct PfrnLog PfrnLog;
struct PfrnLog {
    float episode_return;
    float episode_length;
    float badges;
    float exploration;
    float party_level_sum;
    float n;     /* episode count -- used by env_binding.h vec_log */
};

/* Reward info extracted from game (matches pfr_game_api.h PfrRewardInfo) */
typedef struct {
    int16_t  player_x;
    int16_t  player_y;
    uint8_t  map_group;
    uint8_t  map_num;
    uint8_t  badges;
    uint8_t  party_count;
    uint16_t party_level_sum;
    uint32_t money;
    uint8_t  in_battle;
} PfrnRewardInfo;

/* ---- Main env struct (PufferLib ocean pattern) ---- */

typedef struct PfrnEnv PfrnEnv;
struct PfrnEnv {
    PfrnLog log;                       /* MUST be first (env_binding.h vec_log) */

    unsigned char *observations;       /* PufferLib-managed numpy buffer */
    float         *actions;            /* PufferLib-managed numpy buffer (float32) */
    float         *rewards;            /* PufferLib-managed numpy buffer */
    unsigned char *terminals;          /* PufferLib-managed numpy buffer (bool) */

    PfrnRewardState reward_state;
    PfrInstance    *instance;          /* game instance (SO-copy) */

    uint32_t max_steps;
    uint32_t frames_per_step;
    char     savestate_path[512];
};

/* ---- GBA button defs (active-high for pfr_game_step_frames) ---- */

#define PFRN_BTN_A       (1 << 0)
#define PFRN_BTN_B       (1 << 1)
#define PFRN_BTN_SELECT  (1 << 2)
#define PFRN_BTN_START   (1 << 3)
#define PFRN_BTN_RIGHT   (1 << 4)
#define PFRN_BTN_LEFT    (1 << 5)
#define PFRN_BTN_UP      (1 << 6)
#define PFRN_BTN_DOWN    (1 << 7)
#define PFRN_BTN_R       (1 << 8)
#define PFRN_BTN_L       (1 << 9)

/* ---- Action -> button mapping ---- */

static const uint16_t sPfrnActionToButtons[PFRN_NUM_ACTIONS] = {
    [0] = 0,                                /* noop */
    [1] = PFRN_BTN_UP,
    [2] = PFRN_BTN_DOWN,
    [3] = PFRN_BTN_LEFT,
    [4] = PFRN_BTN_RIGHT,
    [5] = PFRN_BTN_A,
    [6] = PFRN_BTN_B,
    [7] = PFRN_BTN_START,
};

/* ---- Exploration hash ---- */

static uint32_t pfrn_tile_hash(uint8_t mg, uint8_t mn, int16_t x, int16_t y) {
    uint32_t h = (uint32_t)mg * 31 + (uint32_t)mn;
    h = h * 2654435761u + (uint32_t)(uint16_t)x;
    h = h * 2654435761u + (uint32_t)(uint16_t)y;
    return h % PFRN_VISIT_HASH_SIZE;
}

static bool pfrn_visit_check_and_set(PfrnRewardState *rs, uint8_t mg, uint8_t mn,
                                      int16_t x, int16_t y) {
    uint32_t idx = pfrn_tile_hash(mg, mn, x, y);
    uint32_t word = idx / 32;
    uint32_t bit = 1u << (idx % 32);
    if (rs->visit_hash[word] & bit)
        return false;
    rs->visit_hash[word] |= bit;
    rs->visit_count++;
    return true;
}

/* ---- Reward computation ---- */

static float pfrn_compute_reward(PfrnEnv *env, const PfrnRewardInfo *info) {
    PfrnRewardState *rs = &env->reward_state;
    float reward = 0.0f;

    /* 1. Exploration: new tiles visited */
    pfrn_visit_check_and_set(rs, info->map_group, info->map_num,
                              info->player_x, info->player_y);
    uint32_t new_visits = rs->visit_count - rs->prev_visit_count;
    reward += new_visits * 0.02f;
    rs->prev_visit_count = rs->visit_count;

    /* 2. Badge progression */
    uint8_t new_badges = info->badges & ~rs->prev_badges;
    if (new_badges) {
        int count = __builtin_popcount(new_badges);
        reward += count * 10.0f;
        rs->prev_badges = info->badges;
    }

    /* 3. Party level gains */
    if (info->party_level_sum > rs->prev_party_level_sum) {
        reward += (info->party_level_sum - rs->prev_party_level_sum) * 0.1f;
        rs->prev_party_level_sum = info->party_level_sum;
    }

    /* 4. New party member */
    if (info->party_count > rs->prev_party_count) {
        reward += (info->party_count - rs->prev_party_count) * 1.0f;
        rs->prev_party_count = info->party_count;
    }

    return reward;
}

/* ---- pfrn_init: set config, don't touch game yet ---- */

static void pfrn_init(PfrnEnv *env) {
    /* Zero log and reward state (already zeroed by calloc in env_binding.h
     * but be defensive) */
    memset(&env->log, 0, sizeof(PfrnLog));
    memset(&env->reward_state, 0, sizeof(PfrnRewardState));
}

/* ---- c_reset: restore savestate, snapshot baseline ---- */

static void pfrn_restore_episode(PfrnEnv *env, bool clear_outputs) {
    PfrInstance *inst = env->instance;

    // fprintf(stderr, "[PFRN-DEBUG] restore_episode start (instance=%d, clear=%d)\n",
    //             inst->instance_id, clear_outputs);

    /* 1. Zero per-episode state */
    memset(&env->reward_state, 0, sizeof(PfrnRewardState));

    /* 2. Restore game to overworld */
    // fprintf(stderr, "[PFRN-DEBUG] restoring hot state...\n");
    if (env->savestate_path[0] != '\0') {
        if (inst->load_state(env->savestate_path) != 0) {
            fprintf(stderr, "pfrn_env: failed to load savestate: %s\n",
                    env->savestate_path);
            inst->restore_hot();
        }
    } else {
        inst->restore_hot();
    }
    // fprintf(stderr, "[PFRN-DEBUG] hot restore done, getting reward info...\n");

    /* 3. Snapshot baseline for delta reward */
    PfrnRewardInfo info;
    inst->get_reward_info(&info);
    // fprintf(stderr, "[PFRN-DEBUG] reward info done\n");

    /* --- MONITOR: validate game state after restore --- */
    if (info.money > 999999) {
        // fprintf(stderr, "[PFRN-MONITOR] WARNING: corrupt money=%u after restore "
    //                 "(instance=%d)\n", info.money, env->instance->instance_id);
    }
    if (info.party_count == 0) {
        // fprintf(stderr, "[PFRN-MONITOR] WARNING: empty party (count=0) after restore "
    //                 "(instance=%d, pos=%d,%d map=%d.%d)\n",
    //                 env->instance->instance_id,
    //                 info.player_x, info.player_y,
    //                 info.map_group, info.map_num);
    }
    if (info.party_count > 6) {
        fprintf(stderr, "[PFRN-MONITOR] ERROR: corrupt party_count=%u after restore "
                "(instance=%d)\n", info.party_count, env->instance->instance_id);
    }
    if (info.party_level_sum > 600) {
        // fprintf(stderr, "[PFRN-MONITOR] WARNING: suspicious party_level_sum=%u "
    //                 "(instance=%d)\n", info.party_level_sum, env->instance->instance_id);
    }

    PfrnRewardState *rs = &env->reward_state;
    rs->prev_badges = info.badges;
    rs->prev_party_count = info.party_count;
    rs->prev_party_level_sum = info.party_level_sum;
    rs->prev_money = info.money;

    /* Mark starting tile visited */
    pfrn_visit_check_and_set(rs, info.map_group, info.map_num,
                              info.player_x, info.player_y);
    rs->prev_visit_count = rs->visit_count;

    /* 4. Extract initial observation */
    // fprintf(stderr, "[PFRN-DEBUG] extracting obs (buf=%p)...\n", (void*)env->observations);
    inst->extract_obs(env->observations);
    // fprintf(stderr, "[PFRN-DEBUG] obs extracted OK\n");

    /* --- MONITOR: validate observation buffer --- */
    {
        /* Check if screen/obs is all zeros (suggests rendering not working) */
        int all_zero = 1;
        for (int _i = 0; _i < PFRN_OBS_SIZE && all_zero; _i++) {
            if (env->observations[_i] != 0) all_zero = 0;
        }
        if (all_zero) {
            // fprintf(stderr, "[PFRN-MONITOR] WARNING: observation buffer is ALL ZEROS "
    //                     "after reset (instance=%d)\n", env->instance->instance_id);
        }
    }

    // fprintf(stderr, "[PFRN-DEBUG] about to write rewards/terminals (clear=%d, rewards=%p, terminals=%p)\n",
    //             clear_outputs, (void*)env->rewards, (void*)env->terminals);
    if (clear_outputs) {
        env->rewards[0] = 0.0f;
        env->terminals[0] = 0;
    }
    // fprintf(stderr, "[PFRN-DEBUG] restore_episode COMPLETE\n");
}

static void c_reset(PfrnEnv *env) {
    pfrn_restore_episode(env, true);
}

/* ---- c_step: inject action, run frames, extract obs, compute reward ---- */

static void c_step(PfrnEnv *env) {
    PfrInstance *inst = env->instance;

    /* 1. Map action to GBA buttons */
    int action = (int)env->actions[0];
    if (action < 0 || action >= PFRN_NUM_ACTIONS)
        action = 0;
    uint16_t buttons = sPfrnActionToButtons[action];

    /* 2. Step game frames */
    int n = env->frames_per_step ? env->frames_per_step : PFRN_FRAMES_PER_STEP;
    if (0 && inst->step_frames_fast) {
        inst->step_frames_fast(buttons, n);
    } else {
        inst->step_frames(buttons, n);
    }

    /* 3. Extract observations */
    inst->extract_obs(env->observations);

    /* 4. Compute reward */
    PfrnRewardInfo info;
    inst->get_reward_info(&info);

    /* --- MONITOR: validate reward info before computing reward --- */
    if (info.money > 999999) {
        fprintf(stderr, "[PFRN-MONITOR] CORRUPT money=%u at step=%u (instance=%d)\n",
                info.money, env->reward_state.step_count, env->instance->instance_id);
        info.money = 0;  /* clamp to prevent NaN propagation */
    }
    if (info.party_count > 6) {
        fprintf(stderr, "[PFRN-MONITOR] CORRUPT party_count=%u at step=%u (instance=%d)\n",
                info.party_count, env->reward_state.step_count, env->instance->instance_id);
        info.party_count = 0;
    }
    if (info.party_level_sum > 600) {
        fprintf(stderr, "[PFRN-MONITOR] CORRUPT party_level_sum=%u at step=%u (instance=%d)\n",
                info.party_level_sum, env->reward_state.step_count, env->instance->instance_id);
        info.party_level_sum = 0;
    }

    float reward = pfrn_compute_reward(env, &info);

    /* --- MONITOR: check reward is finite --- */
    if (isnan(reward) || isinf(reward)) {
        fprintf(stderr, "[PFRN-MONITOR] NaN/Inf reward=%f at step=%u (instance=%d) "
                "money=%u party=%u levels=%u badges=%u\n",
                reward, env->reward_state.step_count, env->instance->instance_id,
                info.money, info.party_count, info.party_level_sum, info.badges);
        reward = 0.0f;  /* clamp to prevent NaN propagation */
    }
    if (reward > 100.0f || reward < -100.0f) {
        fprintf(stderr, "[PFRN-MONITOR] EXTREME reward=%f at step=%u (instance=%d)\n",
                reward, env->reward_state.step_count, env->instance->instance_id);
    }

    env->rewards[0] = reward;
    env->reward_state.episode_return += reward;
    env->reward_state.step_count++;

    /* 5. Terminal check */
    bool terminal = false;
    uint32_t max_steps = env->max_steps ? env->max_steps : PFRN_MAX_STEPS;
    if (env->reward_state.step_count >= max_steps)
        terminal = true;

    env->terminals[0] = terminal ? 1 : 0;

    if (terminal) {
        float reward = env->rewards[0];

        /* Log episode stats for vec_log aggregation */
        env->log.episode_return += env->reward_state.episode_return;
        env->log.episode_length += (float)env->reward_state.step_count;
        env->log.badges += (float)__builtin_popcount(env->reward_state.prev_badges);
        env->log.exploration += (float)env->reward_state.visit_count;
        env->log.party_level_sum += (float)env->reward_state.prev_party_level_sum;
        env->log.n += 1.0f;

        /* Auto-reset while preserving the terminal signal for this step. */
        pfrn_restore_episode(env, false);
        env->rewards[0] = reward;
        env->terminals[0] = 1;
    }
}

/* ---- c_render / c_close: no-ops for now ---- */

static void c_render(PfrnEnv *env) {
    (void)env;
}

static void c_close(PfrnEnv *env) {
    (void)env;
}

#endif /* PFRN_ENV_H */
