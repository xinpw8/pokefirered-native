/*
 * pfr_env.c — PufferLib RL environment for pokefirered-native
 *
 * This file implements the RL env step loop:
 *   1. Map action integer → GBA button bitmask
 *   2. Inject into REG_KEYINPUT
 *   3. Run N frames of the game
 *   4. Extract observations from live game globals
 *   5. Compute shaped reward
 *   6. Check terminal conditions
 */

#include "pfr_env.h"

#include <string.h>
#include <stdio.h>

/* Upstream game headers */
#include "global.h"
#include "game_ctx.h"
#include "gba/gba.h"
#include "main.h"
#include "pokemon.h"
#include "event_data.h"
#include "constants/pokemon.h"
#include "constants/flags.h"
#include "global.fieldmap.h"
#include "fieldmap.h"
#include "battle.h"

/* Host layer */
#include "host_frame_step.h"
#include "host_runtime_stubs.h"
#include "pfr_game_api.h"

/* ── Action mapping ──
 * GBA REG_KEYINPUT: 0 = pressed, 1 = released (active-low)
 * We set KEYS_MASK (all released) then clear the bits for pressed buttons.
 */

static const uint16_t sActionToButtons[PFR_NUM_ACTIONS] = {
    [0] = 0,                                         /* none: no buttons */
    [1] = DPAD_UP,
    [2] = DPAD_DOWN,
    [3] = DPAD_LEFT,
    [4] = DPAD_RIGHT,
    [5] = A_BUTTON,
    [6] = B_BUTTON,
    [7] = START_BUTTON,
    [8] = SELECT_BUTTON,
    [9] = L_BUTTON | R_BUTTON,                       /* L+R for soft-reset / menu */
};

/* ── Exploration hash ── */

static uint32_t pfr_tile_hash(uint8_t map_group, uint8_t map_num, int16_t x, int16_t y)
{
    uint32_t h = (uint32_t)map_group * 31 + (uint32_t)map_num;
    h = h * 2654435761u + (uint32_t)(uint16_t)x;
    h = h * 2654435761u + (uint32_t)(uint16_t)y;
    return h % PFR_VISIT_HASH_SIZE;
}

static bool pfr_visit_check_and_set(PfrRewardState *rs, uint8_t mg, uint8_t mn, int16_t x, int16_t y)
{
    uint32_t idx = pfr_tile_hash(mg, mn, x, y);
    uint32_t word = idx / 32;
    uint32_t bit = 1u << (idx % 32);
    if (rs->visit_hash[word] & bit)
        return false; /* already visited */
    rs->visit_hash[word] |= bit;
    rs->visit_count++;
    return true; /* new tile */
}

/* ── Observation extraction ── */

static int8_t clamp_s8(int16_t v)
{
    if (v > 127) return 127;
    if (v < -128) return -128;
    return (int8_t)v;
}

static void pfr_extract_observations(PfrEnv *env)
{
    unsigned char *buf = env->observations;
    PfrScalarObs *scalar = (PfrScalarObs *)buf;
    PfrNpcObs *npcs = (PfrNpcObs *)(buf + PFR_SCALAR_OBS_SIZE);
    unsigned char *tiles = buf + PFR_SCALAR_OBS_SIZE + PFR_NPC_OBS_SIZE;

    memset(buf, 0, PFR_OBS_SIZE);

    /* ── Scalars ── */
    scalar->player_x = gSaveBlock1Ptr->pos.x;
    scalar->player_y = gSaveBlock1Ptr->pos.y;

    scalar->map_group = gSaveBlock1Ptr->location.mapGroup;
    scalar->map_num = gSaveBlock1Ptr->location.mapNum;
    scalar->map_layout_id = (uint8_t)(gSaveBlock1Ptr->location.mapGroup); /* mapLayoutId not in WarpData, use mapGroup as proxy */

    /* Player object event (index 0 is always the player) */
    struct ObjectEvent *playerObj = &gObjectEvents[gPlayerAvatar.objectEventId];
    scalar->player_direction = playerObj->facingDirection & 0x0F;
    scalar->player_avatar_flags = gPlayerAvatar.flags;
    scalar->player_running_state = gPlayerAvatar.runningState;
    scalar->player_transition_state = gPlayerAvatar.tileTransitionState;

    scalar->in_battle = gMain.inBattle;
    scalar->battle_outcome = gBattleOutcome;

    /* Party */
    uint8_t partyCount = gSaveBlock1Ptr->playerPartyCount;
    for (int i = 0; i < PFR_MAX_PARTY; i++)
    {
        if (i < partyCount)
        {
            struct Pokemon *mon = &gPlayerParty[i];
            scalar->party[i].species = (uint16_t)GetMonData(mon, MON_DATA_SPECIES);
            scalar->party[i].level = (uint8_t)GetMonData(mon, MON_DATA_LEVEL);
            uint16_t hp = (uint16_t)GetMonData(mon, MON_DATA_HP);
            uint16_t maxHP = (uint16_t)GetMonData(mon, MON_DATA_MAX_HP);
            scalar->party[i].hp_pct = (maxHP > 0) ? (uint8_t)(hp * 255u / maxHP) : 0;
            scalar->party[i].status = (uint8_t)(mon->status & 0xFF);
            /* Type info requires species lookup; use species ID for now */
            scalar->party[i].type1 = 0; /* TODO: lookup from species table */
        }
    }

    /* Badges */
    uint8_t badges = 0;
    for (int i = 0; i < PFR_NUM_BADGES; i++)
    {
        if (FlagGet(FLAG_BADGE01_GET + i))
            badges |= (1u << i);
    }
    scalar->badges = badges;

    /* Money */
    uint32_t money = gSaveBlock1Ptr->money;
    scalar->money = (money > 65535) ? 65535 : (uint16_t)money;

    /* Weather */
    scalar->weather = gSaveBlock1Ptr->weather;

    /* Step counter */
    scalar->step_counter = (uint16_t)(env->reward_state.step_count & 0xFFFF);

    /* ── NPCs ── */
    int16_t px = gSaveBlock1Ptr->pos.x;
    int16_t py = gSaveBlock1Ptr->pos.y;
    int npc_idx = 0;

    for (int i = 0; i < 16 && npc_idx < PFR_MAX_NPCS; i++)
    {
        struct ObjectEvent *obj = &gObjectEvents[i];
        if (!obj->active || obj->isPlayer)
            continue;

        npcs[npc_idx].dx = clamp_s8(obj->currentCoords.x - px);
        npcs[npc_idx].dy = clamp_s8(obj->currentCoords.y - py);
        npcs[npc_idx].graphics_id = obj->graphicsId;
        npcs[npc_idx].direction = obj->facingDirection & 0x0F;
        npcs[npc_idx].active = 1;
        npcs[npc_idx].movement_type = obj->movementType;
        npc_idx++;
    }

    /* ── Tile grid (9x9 metatile behaviors) ── */
    /* MapGridGetMetatileBehaviorAt uses absolute map coords.
     * Player position in map coords = pos + 7 (MAP_OFFSET) */
    int16_t map_x = px + 7;
    int16_t map_y = py + 7;

    for (int dy = -PFR_TILE_RADIUS; dy <= PFR_TILE_RADIUS; dy++)
    {
        for (int dx = -PFR_TILE_RADIUS; dx <= PFR_TILE_RADIUS; dx++)
        {
            int idx = (dy + PFR_TILE_RADIUS) * PFR_TILE_DIM + (dx + PFR_TILE_RADIUS);
            uint32_t behavior = MapGridGetMetatileBehaviorAt(map_x + dx, map_y + dy);
            tiles[idx] = (unsigned char)(behavior & 0xFF);
        }
    }
}

/* ── Reward computation ── */

static float pfr_compute_reward(PfrEnv *env)
{
    PfrRewardState *rs = &env->reward_state;
    float reward = 0.0f;

    /* 1. Exploration: new tiles visited */
    uint8_t mg = gSaveBlock1Ptr->location.mapGroup;
    uint8_t mn = gSaveBlock1Ptr->location.mapNum;
    int16_t px = gSaveBlock1Ptr->pos.x;
    int16_t py = gSaveBlock1Ptr->pos.y;

    pfr_visit_check_and_set(rs, mg, mn, px, py);
    uint32_t new_visits = rs->visit_count - rs->prev_visit_count;
    reward += new_visits * 0.02f;
    rs->prev_visit_count = rs->visit_count;

    /* 2. Badge progression (big reward) */
    uint8_t badges = 0;
    for (int i = 0; i < PFR_NUM_BADGES; i++)
    {
        if (FlagGet(FLAG_BADGE01_GET + i))
            badges |= (1u << i);
    }
    uint8_t new_badges = badges & ~rs->prev_badges;
    if (new_badges)
    {
        int count = __builtin_popcount(new_badges);
        reward += count * 10.0f;
        rs->prev_badges = badges;
    }

    /* 3. Party level gains */
    uint16_t level_sum = 0;
    uint8_t party_count = gSaveBlock1Ptr->playerPartyCount;
    for (int i = 0; i < party_count && i < PFR_MAX_PARTY; i++)
    {
        level_sum += (uint16_t)GetMonData(&gPlayerParty[i], MON_DATA_LEVEL);
    }
    if (level_sum > rs->prev_party_level_sum)
    {
        reward += (level_sum - rs->prev_party_level_sum) * 0.1f;
        rs->prev_party_level_sum = level_sum;
    }

    /* 4. New party member */
    if (party_count > rs->prev_party_count)
    {
        reward += (party_count - rs->prev_party_count) * 1.0f;
        rs->prev_party_count = party_count;
    }

    return reward;
}

/* ── Game frame stepping ── */

static void pfr_run_frames(PfrEnv *env, int n, uint16_t buttons)
{
    (void)env;
    /* Use pfr_game_step_frames which provides HeadlessFrameLogic
     * (inlines ReadKeys + calls gMain.callback1/callback2).
     * Do NOT use HostFrameStepRunFast(NULL, NULL) — NULL logicFn
     * skips all game logic and key processing. */
    pfr_game_step_frames(buttons, n);
}

/* ── Public API ── */

void pfr_env_init(PfrEnv *env,
                  unsigned char *observations,
                  int *actions,
                  float *rewards,
                  unsigned char *terminals,
                  uint32_t max_steps,
                  uint32_t frames_per_step,
                  const char *savestate_path)
{
    /* Zero everything first — clean slate */
    memset(env, 0, sizeof(PfrEnv));

    /* Wire up PufferLib buffer pointers */
    env->observations = observations;
    env->actions      = actions;
    env->rewards      = rewards;
    env->terminals    = terminals;

    /* Config with defaults */
    env->max_steps       = max_steps       ? max_steps       : PFR_MAX_STEPS;
    env->frames_per_step = frames_per_step ? frames_per_step : PFR_FRAMES_PER_STEP;
    env->savestate_loaded = false;

    /* Savestate path */
    if (savestate_path && savestate_path[0] != 0) {
        strncpy(env->savestate_path, savestate_path,
                sizeof(env->savestate_path) - 1);
        env->savestate_path[sizeof(env->savestate_path) - 1] = 0;
    } else {
        env->savestate_path[0] = 0;
    }

    /* Zero the log (PufferLib accumulator) */
    memset(&env->log, 0, sizeof(PfrLog));

    /* Zero reward state (visit hash, counters, prev-trackers) */
    memset(&env->reward_state, 0, sizeof(PfrRewardState));
}

/* Helper: snapshot current game state into prev-trackers for delta reward */
static void pfr_snapshot_baseline(PfrEnv *env)
{
    PfrRewardState *rs = &env->reward_state;

    /* Badges */
    rs->prev_badges = 0;
    for (int i = 0; i < PFR_NUM_BADGES; i++)
    {
        if (FlagGet(FLAG_BADGE01_GET + i))
            rs->prev_badges |= (1u << i);
    }

    /* Party */
    rs->prev_party_count = gSaveBlock1Ptr->playerPartyCount;
    rs->prev_party_level_sum = 0;
    for (int i = 0; i < rs->prev_party_count && i < PFR_MAX_PARTY; i++)
    {
        rs->prev_party_level_sum += (uint16_t)GetMonData(&gPlayerParty[i], MON_DATA_LEVEL);
    }

    /* Money */
    rs->prev_money = gSaveBlock1Ptr->money;

    /* Exploration: mark starting tile as visited */
    uint8_t mg = gSaveBlock1Ptr->location.mapGroup;
    uint8_t mn = gSaveBlock1Ptr->location.mapNum;
    int16_t px = gSaveBlock1Ptr->pos.x;
    int16_t py = gSaveBlock1Ptr->pos.y;
    pfr_visit_check_and_set(rs, mg, mn, px, py);
    rs->prev_visit_count = rs->visit_count;
}

static void pfr_env_restore_episode(PfrEnv *env, bool clear_outputs)
{
    /* 1. Zero per-episode state (visit hash, counters) */
gHostStubSoftResetCalls = 0;
    memset(&env->reward_state, 0, sizeof(PfrRewardState));

    /* 2. Restore game to overworld */
    if (env->savestate_path[0] != 0)
    {
        if (pfr_game_load_state(env->savestate_path) != 0)
        {
            fprintf(stderr, "pfr_env: failed to load savestate: %s\n",
                    env->savestate_path);
            pfr_game_restore_hot();
        }
    }
    else
    {
        pfr_game_restore_hot();
    }

    /* 3. Snapshot game state as baseline for delta reward */
    pfr_snapshot_baseline(env);

    /* 4. Extract initial observation */
    pfr_extract_observations(env);

    if (clear_outputs)
    {
        env->rewards[0] = 0.0f;
        env->terminals[0] = 0;
    }
}

void pfr_env_reset(PfrEnv *env)
{
    pfr_env_restore_episode(env, true);
}

void pfr_env_step(PfrEnv *env)
{
    /* 1. Get action and map to buttons */
    int action = env->actions[0];
    if (action < 0 || action >= PFR_NUM_ACTIONS)
        action = 0;

    uint16_t buttons = sActionToButtons[action];

    /* 2. Run game frames with this input */
    pfr_run_frames(env, env->frames_per_step ? env->frames_per_step : PFR_FRAMES_PER_STEP, buttons);

    /* 3. Extract observations */
    pfr_extract_observations(env);

    /* 4. Compute reward */
    float reward = pfr_compute_reward(env);
    env->rewards[0] = reward;
    env->reward_state.episode_return += reward;
    env->reward_state.step_count++;

    /* 5. Check terminal / truncation */
    bool terminal = false;

    /* Truncate after max_steps */
    uint32_t max_steps = env->max_steps ? env->max_steps : PFR_MAX_STEPS;
    if (env->reward_state.step_count >= max_steps)
        terminal = true;


    /* Game called SoftReset (e.g. after credits). In headless mode SoftReset
     * is a no-op, so we must detect it and end the episode. */
    if (gHostStubSoftResetCalls > 0)
    {
        gHostStubSoftResetCalls = 0;
        terminal = true;
    }
    /* Could also terminal on whiteout, but for now let the agent learn from it */

    env->terminals[0] = terminal ? 1 : 0;

    if (terminal)
    {
        float reward = env->rewards[0];

        /* Log episode stats */
        env->log.episode_return += env->reward_state.episode_return;
        env->log.episode_length += (float)env->reward_state.step_count;
        env->log.badges += (float)__builtin_popcount(env->reward_state.prev_badges);
        env->log.exploration += (float)env->reward_state.visit_count;
        env->log.party_level_sum += (float)env->reward_state.prev_party_level_sum;
        env->log.n += 1.0f;

        /* Auto-reset while preserving the terminal signal for this step. */
        pfr_env_restore_episode(env, false);
        env->rewards[0] = reward;
        env->terminals[0] = 1;
    }
}
