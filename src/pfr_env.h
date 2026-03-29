/*
 * pfr_env.h — PufferLib RL environment for pokefirered-native
 *
 * Design: zero-copy observation extraction from live game state.
 * The C env owns the game loop. Each c_step() injects an action
 * into REG_KEYINPUT, runs N frames, then extracts structured
 * observations from the live game globals.
 *
 * Observation layout is designed for proper neural network consumption:
 * - Multi-byte values reconstructed into single fields
 * - Bitmasks unpacked into individual bits
 * - Spatial tile grid preserved as 2D for Conv2d
 * - Categorical IDs left as integers for embedding layers
 */

#ifndef PFR_ENV_H
#define PFR_ENV_H

#include <stdint.h>
#include <stdbool.h>

/* ── Observation dimensions ── */

#define PFR_TILE_RADIUS    4           /* 4 tiles in each direction from player */
#define PFR_TILE_DIM       (2 * PFR_TILE_RADIUS + 1)  /* 9x9 */
#define PFR_TILE_CHANNELS  8           /* bits per metatile behavior byte */
#define PFR_TILE_GRID_SIZE (PFR_TILE_DIM * PFR_TILE_DIM)  /* 81 */

#define PFR_MAX_PARTY      6
#define PFR_MAX_NPCS       15          /* visible NPCs (OBJECT_EVENTS_COUNT - 1 player) */
#define PFR_NPC_FEATURES   6           /* per-NPC: dx, dy, graphics_id, direction, active, movement_type */

#define PFR_NUM_BADGES     8
#define PFR_NUM_ACTIONS    10          /* none, up, down, left, right, a, b, start, select, l+r (menu) */

#define PFR_FRAMES_PER_STEP 4          /* action repeat / frame skip */
#define PFR_MAX_STEPS       24576      /* episode truncation (~24K steps) */

/* ── Observation struct ──
 * Flat uint8 buffer that PufferLib writes into.
 * The Python policy network parses this into typed tensors.
 *
 * Total size: keep it small for SPS. We pack efficiently.
 */

/* Scalar features block (flat, in order) */
typedef struct __attribute__((packed)) {
    /* Player location (4 bytes) */
    int16_t player_x;                  /* gSaveBlock1Ptr->pos.x */
    int16_t player_y;                  /* gSaveBlock1Ptr->pos.y */

    /* Map identity (3 bytes) */
    uint8_t map_group;                 /* gSaveBlock1Ptr->location.mapGroup */
    uint8_t map_num;                   /* gSaveBlock1Ptr->location.mapNum */
    uint8_t map_layout_id;             /* low byte of mapLayoutId */

    /* Player state (4 bytes) */
    uint8_t player_direction;          /* facing direction 0-3 */
    uint8_t player_avatar_flags;       /* on_foot/bike/surf/etc bitmask */
    uint8_t player_running_state;      /* NOT_MOVING/TURN/MOVING */
    uint8_t player_transition_state;   /* tile transition state */

    /* Game mode flags (2 bytes) */
    uint8_t in_battle;                 /* gMain.inBattle */
    uint8_t battle_outcome;            /* gBattleOutcome (0 if not post-battle) */

    /* Party summary (PFR_MAX_PARTY * 6 = 36 bytes) */
    struct __attribute__((packed)) {
        uint16_t species;              /* 0 = empty slot */
        uint8_t  level;
        uint8_t  hp_pct;               /* hp * 255 / maxHP, 0-255 */
        uint8_t  status;               /* status condition flags (low byte) */
        uint8_t  type1;                /* pokemon type1 for battle context */
    } party[PFR_MAX_PARTY];

    /* Badges (1 byte packed) */
    uint8_t badges;                    /* bit 0 = badge 1, etc. */

    /* Money (2 bytes, capped) */
    uint16_t money;                    /* min(money, 65535) */

    /* Weather (1 byte) */
    uint8_t weather;

    /* Step counter for exploration reward (2 bytes) */
    uint16_t step_counter;

} PfrScalarObs;
/* Expected size: 4 + 3 + 4 + 2 + 36 + 1 + 2 + 1 + 2 = 55 bytes */

/* NPC observation block */
typedef struct __attribute__((packed)) {
    /* Relative position to player */
    int8_t  dx;                        /* NPC.x - player.x, clamped to [-127,127] */
    int8_t  dy;                        /* NPC.y - player.y, clamped to [-127,127] */
    uint8_t graphics_id;               /* sprite/character type (categorical) */
    uint8_t direction;                 /* facing direction */
    uint8_t active;                    /* 1 if this NPC slot is valid */
    uint8_t movement_type;             /* stationary, wander, etc. */
} PfrNpcObs;
/* 6 bytes per NPC, 15 NPCs = 90 bytes */

/* Full observation buffer layout:
 *   [0..54]    PfrScalarObs     (55 bytes)
 *   [55..144]  PfrNpcObs[15]    (90 bytes)
 *   [145..225] tile_grid[81]    (81 bytes, metatile behaviors 9x9)
 *   Total: 226 bytes
 */
#define PFR_SCALAR_OBS_SIZE   sizeof(PfrScalarObs)
#define PFR_NPC_OBS_SIZE      (PFR_MAX_NPCS * sizeof(PfrNpcObs))
#define PFR_OBS_SIZE          (PFR_SCALAR_OBS_SIZE + PFR_NPC_OBS_SIZE + PFR_TILE_GRID_SIZE)

/* ── Reward tracking state ── */

typedef struct {
    /* Exploration: hash-set of visited (map_group, map_num, x, y) */
    #define PFR_VISIT_HASH_SIZE 4096
    uint32_t visit_hash[PFR_VISIT_HASH_SIZE / 32]; /* bit array */
    uint32_t visit_count;                           /* total unique tiles visited */
    uint32_t prev_visit_count;

    /* Progress tracking */
    uint8_t  prev_badges;
    uint8_t  prev_party_count;
    uint16_t prev_party_level_sum;
    uint32_t prev_money;
    uint16_t prev_pokedex_seen;

    /* Episode bookkeeping */
    uint32_t step_count;
    float    episode_return;
} PfrRewardState;

/* ── Log struct (PufferLib requirement: all floats, ends with n) ── */

typedef struct {
    float episode_return;
    float episode_length;
    float badges;
    float exploration;
    float party_level_sum;
    float n;                 /* MUST be last */
} PfrLog;

/* ── Main env struct (matches PufferLib ocean pattern) ── */

typedef struct {
    PfrLog log;                        /* MUST be first field */
    unsigned char *observations;       /* PufferLib-managed buffer */
    int *actions;                      /* PufferLib-managed buffer */
    float *rewards;                    /* PufferLib-managed buffer */
    unsigned char *terminals;          /* PufferLib-managed buffer */

    /* Internal state */
    PfrRewardState reward_state;
    uint32_t max_steps;                /* episode truncation length */
    uint32_t frames_per_step;          /* action repeat */
    bool     savestate_loaded;         /* whether initial state is ready */
    char     savestate_path[512];      /* path to overworld savestate */
} PfrEnv;

/* ── API ── */

/*
 * pfr_env_init: One-time initialization of env struct.
 * Must be called before pfr_env_reset(). Sets all fields to correct
 * initial values and types. Equivalent to pokemonred_puffer's
 * __init__() + init_mem().
 *
 * obs/actions/rewards/terminals: PufferLib-managed buffer pointers
 * max_steps: episode truncation length (0 = PFR_MAX_STEPS default)
 * frames_per_step: action repeat (0 = PFR_FRAMES_PER_STEP default)
 * savestate_path: path to overworld savestate (NULL = use hot state)
 */
void pfr_env_init(PfrEnv *env,
                  unsigned char *observations,
                  int *actions,
                  float *rewards,
                  unsigned char *terminals,
                  uint32_t max_steps,
                  uint32_t frames_per_step,
                  const char *savestate_path);

void pfr_env_reset(PfrEnv *env);
void pfr_env_step(PfrEnv *env);

/* PufferLib-compatible names */
#define c_reset pfr_env_reset
#define c_step  pfr_env_step
static inline void c_render(PfrEnv *env) { (void)env; }
static inline void c_close(PfrEnv *env) { (void)env; }

#endif /* PFR_ENV_H */
