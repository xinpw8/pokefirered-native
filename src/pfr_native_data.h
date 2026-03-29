#ifndef PFR_NATIVE_DATA_H
#define PFR_NATIVE_DATA_H

#include <stddef.h>
#include <stdint.h>

#define PFR_NATIVE_MAX_OBJECTS 32
#define PFR_NATIVE_MAX_FLAGS   64
#define PFR_NATIVE_MAX_VARS    32
#define PFR_NATIVE_MAX_PARTY   6
#define PFR_NATIVE_STARTER_NONE 0xFF
typedef uint16_t PfrNativeMapId;

#define PFR_NATIVE_MAP_PLAYERS_HOUSE_1F ((PfrNativeMapId)0)
#define PFR_NATIVE_MAP_PLAYERS_HOUSE_2F ((PfrNativeMapId)1)
#define PFR_NATIVE_MAP_PALLET_TOWN_PLAYERS_HOUSE_1F PFR_NATIVE_MAP_PLAYERS_HOUSE_1F
#define PFR_NATIVE_MAP_PALLET_TOWN_PLAYERS_HOUSE_2F PFR_NATIVE_MAP_PLAYERS_HOUSE_2F
#define PFR_NATIVE_MAP_PALLET_TOWN ((PfrNativeMapId)190)
#define PFR_NATIVE_MAP_COUNT (gPfrNativeMapCount)
#define PFR_NATIVE_MAP_INVALID ((PfrNativeMapId)0xFFFF)

/* ---- Flag/Var system ---- */
/* Inline helpers for the 64-bit flag bitfield */
static inline int pfrn_flag_get(uint64_t flags, uint8_t idx) {
    return (idx < PFR_NATIVE_MAX_FLAGS) ? (int)((flags >> idx) & 1) : 0;
}
static inline uint64_t pfrn_flag_set(uint64_t flags, uint8_t idx) {
    return (idx < PFR_NATIVE_MAX_FLAGS) ? (flags | (1ULL << idx)) : flags;
}
static inline uint64_t pfrn_flag_clear(uint64_t flags, uint8_t idx) {
    return (idx < PFR_NATIVE_MAX_FLAGS) ? (flags & ~(1ULL << idx)) : flags;
}
static inline int pfrn_badge_count(uint64_t flags, uint8_t badge_flag_start) {
    int count = 0;
    for (int i = 0; i < 8; i++)
        count += pfrn_flag_get(flags, badge_flag_start + i);
    return count;
}

/* ---- Script action types ---- */
typedef enum {
    PFRN_ACT_SET_FLAG = 0,
    PFRN_ACT_SET_VAR = 1,
    PFRN_ACT_CLEAR_FLAG = 2,
    PFRN_ACT_DIALOG = 3,
    PFRN_ACT_AUTO_BATTLE = 4,
    PFRN_ACT_WARP = 5,
    PFRN_ACT_GIVE_STARTER = 6,
    PFRN_ACT_POKECENTER_HEAL = 7,
    PFRN_ACT_OPEN_SHOP = 8,
    PFRN_ACT_OPEN_PC = 9,
} PfrNativeActionType;

/* ---- Guard condition types ---- */
typedef enum {
    PFRN_GUARD_NONE = 0,
    PFRN_GUARD_FLAG_SET = 1,
    PFRN_GUARD_FLAG_UNSET = 2,
    PFRN_GUARD_VAR_EQ = 3,
    PFRN_GUARD_BADGE_GE = 4,
} PfrNativeGuardType;

/* ---- Script action (single state mutation) ---- */
typedef struct {
    uint8_t type;     /* PfrNativeActionType */
    uint8_t param;    /* flag_id, var_id, dialog_id */
    uint8_t value;    /* var value, starter_id, or unused */
    uint8_t extra;    /* for WARP: map_id low byte; for AUTO_BATTLE: flag to set on win */
} PfrNativeScriptAction;

/* ---- Script definition (data-driven) ---- */
typedef struct {
    const PfrNativeScriptAction *actions;
    uint8_t action_count;
    uint8_t guard_type;   /* PfrNativeGuardType */
    uint8_t guard_param;  /* flag_id or var_id or badge threshold */
    uint8_t guard_value;  /* for VAR_EQ */
} PfrNativeScript;

/* ---- Coord event (trigger on step) ---- */
typedef struct {
    int16_t x, y;
    uint8_t var_id;       /* guard var index (0xFF = no guard) */
    uint8_t var_value;    /* fire only when var == this */
    uint16_t script_id;    /* script to execute */
    uint8_t reserved;
} PfrNativeCoordEvent;

/* ---- Map-enter action (runs on map load) ---- */
typedef struct {
    uint8_t action_type;   /* 0=set_flag, 1=set_var, 2=clear_flag */
    uint8_t cond_type;     /* PfrNativeGuardType */
    uint8_t cond_param;    /* flag_id or var_id or badge threshold */
    uint8_t cond_value;    /* for var_eq */
    uint8_t action_param;  /* flag_id or var_id */
    uint8_t action_value;  /* for set_var */
} PfrNativeMapEnterAction;

/* ---- Metatile override (runtime tile collision change) ---- */
/* When the guard condition is met, the tile at (x, y) has its collision
 * replaced with new_collision. Used for: Cinnabar quiz doors, Victory Road
 * barriers, Vermilion Gym electric beams. Checked by tile_at_effective(). */
typedef struct {
    uint16_t x;            /* tile x coordinate */
    uint16_t y;            /* tile y coordinate */
    uint8_t new_collision; /* 0=passable, 1=blocked */
    uint8_t guard_type;    /* PfrNativeGuardType */
    uint8_t guard_param;   /* flag_id or var_id or badge threshold */
    uint8_t guard_value;   /* for VAR_EQ comparison */
} PfrNativeMetatileOverride;

/* Generated enums (flag/var indices, script IDs, dialog IDs).
 * This file is produced by tools/gen_pfr_native_data.py. */
#include "pfr_native_generated.h"

typedef struct {
    uint16_t raw_block;
    uint16_t metatile_id;
    uint16_t behavior;
    uint8_t collision;
    uint8_t elevation;
} PfrNativeTile;

typedef struct {
    int16_t x;
    int16_t y;
    PfrNativeMapId dest_map;
    uint8_t dest_warp_id;
    uint8_t supported;
} PfrNativeWarp;

typedef enum {
    PFR_NATIVE_CONN_NORTH = 0,
    PFR_NATIVE_CONN_SOUTH = 1,
    PFR_NATIVE_CONN_WEST = 2,
    PFR_NATIVE_CONN_EAST = 3,
    PFR_NATIVE_CONN_DIVE = 4,
    PFR_NATIVE_CONN_EMERGE = 5,
    PFR_NATIVE_CONN_UNKNOWN = 0xFF,
} PfrNativeConnectionDirection;

typedef struct {
    uint8_t direction;
    int16_t offset;
    PfrNativeMapId dest_map;
} PfrNativeConnection;

typedef struct {
    int16_t x;
    int16_t y;
    uint8_t facing_mask;
    uint16_t script_id;
} PfrNativeBgEvent;

/* Boulder graphics ID — objects with this ID are pushable via Strength */
#define PFR_NATIVE_GFX_PUSHABLE_BOULDER 0x61

typedef struct {
    uint8_t local_id;
    uint16_t graphics_id;
    int16_t x;
    int16_t y;
    uint8_t facing;
    uint8_t movement_type;
    uint16_t script_id;
    uint8_t hide_flag;     /* flag index: NPC hidden when flag is set (0xFF = always visible) */
} PfrNativeObjectEvent;

typedef struct {
    const char *name;
    const char *id_symbol;
    PfrNativeMapId map_id;
    uint8_t map_group;
    uint8_t map_num;
    uint16_t width;
    uint16_t height;
    const PfrNativeTile *tiles;
    size_t tile_count;
    const PfrNativeWarp *warps;
    size_t warp_count;
    const PfrNativeConnection *connections;
    size_t connection_count;
    const PfrNativeBgEvent *bg_events;
    size_t bg_event_count;
    const PfrNativeObjectEvent *object_events;
    size_t object_event_count;
    const PfrNativeCoordEvent *coord_events;
    size_t coord_event_count;
    const PfrNativeMapEnterAction *enter_actions;
    size_t enter_action_count;
    const PfrNativeMetatileOverride *metatile_overrides;
    size_t metatile_override_count;
    uint16_t primary_tileset_id;
    uint16_t secondary_tileset_id;
} PfrNativeMap;

typedef struct {
    const char *const *pages;
    uint8_t page_count;
} PfrNativeDialog;

extern const size_t gPfrNativeMapCount;
extern const PfrNativeMap gPfrNativeMaps[];
extern const PfrNativeDialog gPfrNativeDialogs[];
extern const PfrNativeScript gPfrNativeScripts[];
extern const size_t gPfrNativeScriptCount;

#include "pfr_native_tileset_data.h"

#endif
