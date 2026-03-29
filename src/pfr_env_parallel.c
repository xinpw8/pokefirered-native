/*
 * pfr_env_parallel.c — Parallel RL environment using SO-copy instances
 *
 * No game headers. All game interaction through PfrInstance function pointers.
 * This file can be compiled without ANY pokefirered include paths.
 */

#include "pfr_env_parallel.h"
#include <string.h>
#include <stdio.h>

/* Action → GBA button mapping (same values as pfr_env.c) */
static const uint16_t sActionToButtons[PFR_NUM_ACTIONS] = {
    [0] = 0,
    [1] = PFR_BTN_UP,
    [2] = PFR_BTN_DOWN,
    [3] = PFR_BTN_LEFT,
    [4] = PFR_BTN_RIGHT,
    [5] = PFR_BTN_A,
    [6] = PFR_BTN_B,
    [7] = PFR_BTN_START,
    [8] = PFR_BTN_SELECT,
    [9] = PFR_BTN_L | PFR_BTN_R,
};

/* ── Exploration hash (same as pfr_env.c) ── */

static uint32_t pfr_tile_hash(uint8_t map_group, uint8_t map_num, int16_t x, int16_t y)
{
    uint32_t h = (uint32_t)map_group * 31 + (uint32_t)map_num;
    h = h * 2654435761u + (uint32_t)(uint16_t)x;
    h = h * 2654435761u + (uint32_t)(uint16_t)y;
    return h % PFR_VISIT_HASH_SIZE;
}

static bool pfr_visit_check_and_set(PfrParRewardState *rs, uint8_t mg, uint8_t mn, int16_t x, int16_t y)
{
    uint32_t idx = pfr_tile_hash(mg, mn, x, y);
    uint32_t word = idx / 32;
    uint32_t bit = 1u << (idx % 32);
    if (rs->visit_hash[word] & bit)
        return false;
    rs->visit_hash[word] |= bit;
    rs->visit_count++;
    return true;
}

/* ── Reward computation ── */

static float pfr_par_compute_reward(PfrParEnv *env, const PfrParRewardInfo *info)
{
    PfrParRewardState *rs = &env->reward_state;
    float reward = 0.0f;

    /* 1. Exploration */
    pfr_visit_check_and_set(rs, info->map_group, info->map_num, info->player_x, info->player_y);
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

/* ── Public API ── */

void pfr_par_env_init(PfrParEnv *env,
                      unsigned char *observations,
                      int *actions,
                      float *rewards,
                      unsigned char *terminals,
                      PfrInstance *instance,
                      uint32_t max_steps,
                      uint32_t frames_per_step,
                      const char *savestate_path)
{
    /* Zero everything */
    memset(env, 0, sizeof(PfrParEnv));

    /* Wire up PufferLib buffer pointers */
    env->observations = observations;
    env->actions      = actions;
    env->rewards      = rewards;
    env->terminals    = terminals;
    env->instance     = instance;

    /* Config with defaults */
    env->max_steps       = max_steps       ? max_steps       : PFR_MAX_STEPS;
    env->frames_per_step = frames_per_step ? frames_per_step : PFR_FRAMES_PER_STEP;

    /* Savestate path */
    if (savestate_path && savestate_path[0] != '\0') {
        strncpy(env->savestate_path, savestate_path,
                sizeof(env->savestate_path) - 1);
        env->savestate_path[sizeof(env->savestate_path) - 1] = '\0';
    } else {
        env->savestate_path[0] = '\0';
    }

    /* Zero log and reward state */
    memset(&env->log, 0, sizeof(PfrParLog));
    memset(&env->reward_state, 0, sizeof(PfrParRewardState));
}

void pfr_par_env_reset(PfrParEnv *env)
{
    PfrInstance *inst = env->instance;

    /* 1. Zero per-episode state */
    memset(&env->reward_state, 0, sizeof(PfrParRewardState));

    /* 2. Restore game to overworld */
    if (env->savestate_path[0] != '\0') {
        if (inst->load_state(env->savestate_path) != 0) {
            fprintf(stderr, "pfr_par_env: failed to load savestate: %s\n",
                    env->savestate_path);
        }
    } else {
        inst->restore_hot();
    }

    /* 3. Snapshot game state as baseline for delta reward */
    PfrParRewardInfo info;
    inst->get_reward_info(&info);

    PfrParRewardState *rs = &env->reward_state;
    rs->prev_badges = info.badges;
    rs->prev_party_count = info.party_count;
    rs->prev_party_level_sum = info.party_level_sum;
    rs->prev_money = info.money;

    /* Mark starting tile as visited */
    pfr_visit_check_and_set(rs, info.map_group, info.map_num,
                            info.player_x, info.player_y);
    rs->prev_visit_count = rs->visit_count;

    /* 4. Extract initial observation */
    inst->extract_obs(env->observations);

    /* 5. Zero initial reward/terminal */
    env->rewards[0] = 0.0f;
    env->terminals[0] = 0;
}

void pfr_par_env_step(PfrParEnv *env)
{
    PfrInstance *inst = env->instance;

    /* 1. Map action to buttons */
    int action = env->actions[0];
    if (action < 0 || action >= PFR_NUM_ACTIONS)
        action = 0;
    uint16_t buttons = sActionToButtons[action];

    /* 2. Step game frames */
    int n = env->frames_per_step ? env->frames_per_step : PFR_FRAMES_PER_STEP;
    inst->step_frames(buttons, n);

    /* 3. Extract observations */
    inst->extract_obs(env->observations);

    /* 4. Get reward info and compute reward */
    PfrParRewardInfo info;
    inst->get_reward_info(&info);
    float reward = pfr_par_compute_reward(env, &info);
    env->rewards[0] = reward;
    env->reward_state.episode_return += reward;
    env->reward_state.step_count++;

    /* 5. Terminal check */
    bool terminal = false;
    uint32_t max_steps = env->max_steps ? env->max_steps : PFR_MAX_STEPS;
    if (env->reward_state.step_count >= max_steps)
        terminal = true;

    env->terminals[0] = terminal ? 1 : 0;

    if (terminal) {
        env->log.episode_return += env->reward_state.episode_return;
        env->log.episode_length += (float)env->reward_state.step_count;
        env->log.badges += (float)__builtin_popcount(env->reward_state.prev_badges);
        env->log.exploration += (float)env->reward_state.visit_count;
        env->log.party_level_sum += (float)env->reward_state.prev_party_level_sum;
        env->log.n += 1.0f;

        pfr_par_env_reset(env);
    }
}
