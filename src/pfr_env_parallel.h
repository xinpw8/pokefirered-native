/*
 * pfr_env_parallel.h — Parallel RL environment using SO-copy instances
 *
 * Unlike pfr_env.h, this header has ZERO dependencies on game internals.
 * All game interaction happens through PfrInstance function pointers.
 */

#ifndef PFR_ENV_PARALLEL_H
#define PFR_ENV_PARALLEL_H

#include <stdint.h>
#include <stdbool.h>
#include "pfr_so_instance.h"

/* ── Same observation dimensions as pfr_env.h ── */
#define PFR_TILE_RADIUS    4
#define PFR_TILE_DIM       (2 * PFR_TILE_RADIUS + 1)
#define PFR_TILE_GRID_SIZE (PFR_TILE_DIM * PFR_TILE_DIM)
#define PFR_MAX_PARTY      6
#define PFR_MAX_NPCS       15
#define PFR_NPC_FEATURES   6
#define PFR_NUM_BADGES     8
#define PFR_NUM_ACTIONS    10
#define PFR_FRAMES_PER_STEP 4
#define PFR_MAX_STEPS       24576
#define PFR_VISIT_HASH_SIZE 4096

/* Obs size: 55 + 90 + 81 = 226 */
#define PFR_SCALAR_OBS_SIZE  55
#define PFR_NPC_OBS_SIZE     90
#define PFR_OBS_SIZE         226

/* ── Action → button mapping (must match pfr_game_api.h key defs) ── */
/* GBA button bits (active-high for pfr_game_step_frames) */
#define PFR_BTN_A       (1 << 0)
#define PFR_BTN_B       (1 << 1)
#define PFR_BTN_SELECT  (1 << 2)
#define PFR_BTN_START   (1 << 3)
#define PFR_BTN_RIGHT   (1 << 4)
#define PFR_BTN_LEFT    (1 << 5)
#define PFR_BTN_UP      (1 << 6)
#define PFR_BTN_DOWN    (1 << 7)
#define PFR_BTN_R       (1 << 8)
#define PFR_BTN_L       (1 << 9)

/* ── Reward info (matches PfrRewardInfo in pfr_game_api.h) ── */
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
} PfrParRewardInfo;

/* ── Reward tracking state ── */
typedef struct {
    uint32_t visit_hash[PFR_VISIT_HASH_SIZE / 32];
    uint32_t visit_count;
    uint32_t prev_visit_count;
    uint8_t  prev_badges;
    uint8_t  prev_party_count;
    uint16_t prev_party_level_sum;
    uint32_t prev_money;
    uint32_t step_count;
    float    episode_return;
} PfrParRewardState;

/* ── Log struct (PufferLib: all floats, ends with n) ── */
typedef struct {
    float episode_return;
    float episode_length;
    float badges;
    float exploration;
    float party_level_sum;
    float n;
} PfrParLog;

/* ── Parallel env struct ── */
typedef struct {
    PfrParLog log;                     /* MUST be first (PufferLib) */
    unsigned char *observations;       /* PufferLib-managed */
    int *actions;                      /* PufferLib-managed */
    float *rewards;                    /* PufferLib-managed */
    unsigned char *terminals;          /* PufferLib-managed */

    PfrParRewardState reward_state;
    PfrInstance *instance;             /* Pointer to this env's game instance */
    uint32_t max_steps;
    uint32_t frames_per_step;
    char     savestate_path[512];
} PfrParEnv;

/* ── API ── */

/*
 * pfr_par_env_init: One-time initialization.
 * Must be called before pfr_par_env_reset().
 */
void pfr_par_env_init(PfrParEnv *env,
                      unsigned char *observations,
                      int *actions,
                      float *rewards,
                      unsigned char *terminals,
                      PfrInstance *instance,
                      uint32_t max_steps,
                      uint32_t frames_per_step,
                      const char *savestate_path);

void pfr_par_env_reset(PfrParEnv *env);
void pfr_par_env_step(PfrParEnv *env);

/* PufferLib-compatible names */
#define c_reset pfr_par_env_reset
#define c_step  pfr_par_env_step
static inline void c_render(PfrParEnv *env) { (void)env; }
static inline void c_close(PfrParEnv *env) { (void)env; }

#endif /* PFR_ENV_PARALLEL_H */
