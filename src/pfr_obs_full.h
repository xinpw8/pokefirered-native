#ifndef PFR_OBS_FULL_H
#define PFR_OBS_FULL_H

#include <stdint.h>

/*
 * Comprehensive observation for RL training.
 * Matches pokemonred_puffer's obs richness but in pure C for max SPS.
 *
 * Layout (all little-endian, packed):
 *   screen:     72x80 grayscale pixels (downscaled from 240x160)
 *   scalars:    player state, badges, money, weather, step counter
 *   party:      6 pokemon with full stats, moves, status
 *   bag_items:  top 20 general items (id + qty)
 *   key_items:  30 key item slots (id only)
 *   events:     full 288-byte event flag array (2304 flags)
 *   pokedex:    seen + owned bitmaps (52 + 52 bytes)
 *   npcs:       16 object events with position + type
 *   tiles:      9x9 metatile behavior grid
 */

#define PFR_SCREEN_W         72
#define PFR_SCREEN_H         80
#define PFR_SCREEN_SIZE      (PFR_SCREEN_W * PFR_SCREEN_H)  /* 5760 */

#define PFR_MAX_PARTY        6
#define PFR_MAX_MOVES        4
#define PFR_MAX_BAG_ITEMS    20
#define PFR_MAX_KEY_ITEMS    30
#define PFR_MAX_NPCS_FULL    16
#define PFR_TILE_RADIUS      4
#define PFR_TILE_GRID_SIZE   ((2*PFR_TILE_RADIUS+1)*(2*PFR_TILE_RADIUS+1))  /* 81 */
#define PFR_NUM_FLAG_BYTES   288
#define PFR_DEX_FLAG_BYTES   52
#define PFR_NUM_BADGES       8

/* Per-party-member observation (30 bytes) */
typedef struct __attribute__((packed)) {
    uint16_t species;
    uint8_t  level;
    uint16_t hp;
    uint16_t max_hp;
    uint16_t attack;
    uint16_t defense;
    uint16_t speed;
    uint16_t sp_attack;
    uint16_t sp_defense;
    uint32_t status;
    uint8_t  type1;
    uint8_t  type2;
    uint16_t moves[PFR_MAX_MOVES];
} PfrPartyMonObs;

/* Per-NPC observation (8 bytes) */
typedef struct __attribute__((packed)) {
    uint8_t  active;
    uint8_t  graphics_id;
    int16_t  x;
    int16_t  y;
    uint8_t  facing_direction;
    uint8_t  movement_type;
} PfrNpcFullObs;

/* Bag item slot (4 bytes) */
typedef struct __attribute__((packed)) {
    uint16_t item_id;
    uint16_t quantity;
} PfrBagItemObs;

/* Player scalar state (20 bytes) */
typedef struct __attribute__((packed)) {
    int16_t  player_x;
    int16_t  player_y;
    uint8_t  map_group;
    uint8_t  map_num;
    uint16_t map_layout_id;
    uint8_t  direction;
    uint8_t  running_state;
    uint8_t  in_battle;
    uint8_t  battle_outcome;
    uint8_t  badges;
    uint8_t  weather;
    uint32_t money;
    uint16_t step_counter;
} PfrScalarFullObs;

/* Full observation struct */
typedef struct __attribute__((packed)) {
    uint8_t          screen[PFR_SCREEN_SIZE];                    /* 5760 */
    PfrScalarFullObs scalars;                                     /* 20   */
    PfrPartyMonObs   party[PFR_MAX_PARTY];                       /* 180  */
    PfrBagItemObs    bag_items[PFR_MAX_BAG_ITEMS];               /* 80   */
    uint16_t         key_items[PFR_MAX_KEY_ITEMS];               /* 60   */
    uint8_t          event_flags[PFR_NUM_FLAG_BYTES];            /* 288  */
    uint8_t          pokedex_seen[PFR_DEX_FLAG_BYTES];           /* 52   */
    uint8_t          pokedex_owned[PFR_DEX_FLAG_BYTES];          /* 52   */
    PfrNpcFullObs    npcs[PFR_MAX_NPCS_FULL];                    /* 128  */
    uint8_t          tile_grid[PFR_TILE_GRID_SIZE];              /* 81   */
} PfrFullObs;

#define PFR_FULL_OBS_SIZE  sizeof(PfrFullObs)
/* Expected: 5760+20+180+80+60+288+52+52+128+81 = 6701 */

/* Reward info extended with event/item tracking */
typedef struct {
    /* Basic (same as PfrRewardInfo) */
    int16_t  player_x;
    int16_t  player_y;
    uint8_t  map_group;
    uint8_t  map_num;
    uint8_t  badges;
    uint8_t  party_count;
    uint16_t party_level_sum;
    uint32_t money;
    uint8_t  in_battle;

    /* Extended */
    uint16_t pokedex_seen_count;
    uint16_t pokedex_owned_count;
    uint8_t  party_hp_sum_pct;      /* avg hp% across party, 0-100 */

    /* Event flag counts for progression tracking */
    uint16_t story_flags_set;       /* count of set flags in 0x230-0x4AF range */
    uint16_t trainer_flags_set;     /* count of defeated trainers (0x500-0x7FF) */
    uint16_t system_flags_set;      /* count of set system flags */

    /* Key items */
    uint8_t  has_hm01;  /* Cut */
    uint8_t  has_hm02;  /* Fly */
    uint8_t  has_hm03;  /* Surf */
    uint8_t  has_hm04;  /* Strength */
    uint8_t  has_hm05;  /* Flash */
    uint8_t  has_ss_ticket;
    uint8_t  has_silph_scope;
    uint8_t  has_poke_flute;
    uint8_t  has_card_key;
    uint8_t  has_lift_key;
    uint8_t  has_gold_teeth;
    uint8_t  has_bicycle;
    uint8_t  has_tea;
    uint8_t  has_secret_key;

    /* Gym leader defeated flags */
    uint8_t  defeated_brock;
    uint8_t  defeated_misty;
    uint8_t  defeated_surge;
    uint8_t  defeated_erika;
    uint8_t  defeated_koga;
    uint8_t  defeated_sabrina;
    uint8_t  defeated_blaine;
    uint8_t  defeated_giovanni;
    uint8_t  defeated_e4_lorelei;
    uint8_t  defeated_e4_bruno;
    uint8_t  defeated_e4_agatha;
    uint8_t  defeated_e4_lance;
    uint8_t  defeated_champion;
    uint8_t  game_clear;
} PfrRewardInfoFull;

#endif /* PFR_OBS_FULL_H */
