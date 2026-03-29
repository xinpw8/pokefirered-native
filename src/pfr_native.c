#include "pfr_native.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MB_IMPASSABLE_EAST 0x30
#define MB_IMPASSABLE_WEST 0x31
#define MB_IMPASSABLE_NORTH 0x32
#define MB_IMPASSABLE_SOUTH 0x33
#define MB_IMPASSABLE_NORTHEAST 0x34
#define MB_IMPASSABLE_NORTHWEST 0x35
#define MB_IMPASSABLE_SOUTHEAST 0x36
#define MB_IMPASSABLE_SOUTHWEST 0x37
#define MB_CAVE_DOOR 0x60
#define MB_LADDER 0x61
#define MB_EAST_ARROW_WARP 0x62
#define MB_WEST_ARROW_WARP 0x63
#define MB_NORTH_ARROW_WARP 0x64
#define MB_SOUTH_ARROW_WARP 0x65
#define MB_JUMP_EAST 0x38
#define MB_JUMP_WEST 0x39
#define MB_JUMP_NORTH 0x3A
#define MB_JUMP_SOUTH 0x3B
#define MB_FALL_WARP 0x66
#define MB_REGULAR_WARP 0x67
#define MB_LAVARIDGE_1F_WARP 0x68
#define MB_WARP_DOOR 0x69
#define MB_UP_ESCALATOR 0x6A
#define MB_DOWN_ESCALATOR 0x6B
#define MB_UP_RIGHT_STAIR_WARP 0x6C
#define MB_UP_LEFT_STAIR_WARP 0x6D
#define MB_DOWN_RIGHT_STAIR_WARP 0x6E
#define MB_DOWN_LEFT_STAIR_WARP 0x6F
#define MB_UNION_ROOM_WARP 0x71
/* Water tile behaviors (for Surf) */
#define MB_POND_WATER 0x10
#define MB_FAST_WATER 0x11
#define MB_DEEP_WATER 0x12
#define MB_OCEAN_WATER 0x15

/* Spin tile behaviors (Rocket HQ, Viridian Gym) */
#define MB_SPIN_RIGHT 0x54
#define MB_SPIN_LEFT  0x55
#define MB_SPIN_UP    0x56
#define MB_SPIN_DOWN  0x57
#define MB_STOP_SPINNING 0x58

/* Slide tile behaviors (ice puzzles) */
#define MB_SLIDE_EAST  0x44
#define MB_SLIDE_WEST  0x45
#define MB_SLIDE_NORTH 0x46
#define MB_SLIDE_SOUTH 0x47

#define MB_BOOKSHELF 0x81
#define MB_PC 0x83
#define MB_SIGNPOST 0x84
#define MB_TELEVISION 0x86
#define MB_KITCHEN 0x8A
#define MB_DRESSER 0x8B
#define MB_WINDOW 0x9D

/* Maximum iterations for spin/slide loops to prevent infinite loops */
#define MAX_SLIDE_STEPS 64

#define DIALOG_BUFFER_SIZE 256
#define TILE_PIXELS 16

#define PFR_SAVE_MAGIC 0x50465253  /* "PFRS" */
#define PFR_SAVE_VERSION 1

/* Save state to binary file. Returns 0 on success. */
static int pfr_save_to_file(const PfrNativeState *state, const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    uint32_t magic = PFR_SAVE_MAGIC;
    uint32_t version = PFR_SAVE_VERSION;
    uint32_t size = (uint32_t)sizeof(PfrNativeState);
    fwrite(&magic, 4, 1, f);
    fwrite(&version, 4, 1, f);
    fwrite(&size, 4, 1, f);
    fwrite(state, sizeof(PfrNativeState), 1, f);
    fclose(f);
    return 0;
}

/* Load state from binary file. Returns 0 on success. */
static int __attribute__((unused)) pfr_load_from_file(PfrNativeState *state, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    uint32_t magic, version, size;
    if (fread(&magic, 4, 1, f) != 1 || magic != PFR_SAVE_MAGIC) { fclose(f); return -1; }
    if (fread(&version, 4, 1, f) != 1 || version != PFR_SAVE_VERSION) { fclose(f); return -1; }
    if (fread(&size, 4, 1, f) != 1 || size != (uint32_t)sizeof(PfrNativeState)) { fclose(f); return -1; }
    if (fread(state, sizeof(PfrNativeState), 1, f) != 1) { fclose(f); return -1; }
    fclose(f);
    return 0;
}

/* Get save file path (in home dir) */
static const char *pfr_save_path(void)
{
    static char path[512];
    if (path[0] == '\0') {
        const char *home = getenv("HOME");
        if (home)
            snprintf(path, sizeof(path), "%s/.pfr_native_save.bin", home);
        else
            snprintf(path, sizeof(path), "/tmp/.pfr_native_save.bin");
    }
    return path;
}

const PfrNativeMap *pfr_native_get_map(PfrNativeMapId map_id)
{
    if (map_id >= PFR_NATIVE_MAP_COUNT)
        return NULL;
    return &gPfrNativeMaps[map_id];
}

PfrNativeMapId pfr_native_find_map_by_name(const char *name)
{
    size_t i;

    if (name == NULL || name[0] == '\0')
        return PFR_NATIVE_MAP_INVALID;

    for (i = 0; i < PFR_NATIVE_MAP_COUNT; i++)
    {
        if (strcmp(gPfrNativeMaps[i].name, name) == 0)
            return gPfrNativeMaps[i].map_id;
    }

    return PFR_NATIVE_MAP_INVALID;
}

static void state_clear(PfrNativeState *state)
{
    memset(state, 0, sizeof(*state));
    state->current_map = PFR_NATIVE_MAP_INVALID;
    state->dialog_restore_object_index = PFR_NATIVE_INVALID_OBJECT;
    state->starter_mon = PFR_NATIVE_STARTER_NONE;
}

static uint8_t opposite_dir(uint8_t direction)
{
    switch (direction)
    {
    case PFR_NATIVE_DIR_NORTH:
        return PFR_NATIVE_DIR_SOUTH;
    case PFR_NATIVE_DIR_SOUTH:
        return PFR_NATIVE_DIR_NORTH;
    case PFR_NATIVE_DIR_WEST:
        return PFR_NATIVE_DIR_EAST;
    case PFR_NATIVE_DIR_EAST:
        return PFR_NATIVE_DIR_WEST;
    default:
        return PFR_NATIVE_DIR_NONE;
    }
}

static const PfrNativeTile *tile_at(const PfrNativeMap *map, int16_t x, int16_t y)
{
    if (map == NULL)
        return NULL;
    if (x < 0 || y < 0 || x >= map->width || y >= map->height)
        return NULL;
    return &map->tiles[(size_t)y * map->width + x];
}

/* Check if a metatile override changes the collision value for a tile.
 * Returns the overridden collision if a matching guard is met, or -1 if no override applies. */
static int check_metatile_override(const PfrNativeMap *map, const PfrNativeState *state,
                                    int16_t x, int16_t y)
{
    size_t i;
    if (map == NULL || map->metatile_override_count == 0)
        return -1;
    for (i = 0; i < map->metatile_override_count; i++)
    {
        const PfrNativeMetatileOverride *ov = &map->metatile_overrides[i];
        if (ov->x != (uint16_t)x || ov->y != (uint16_t)y)
            continue;
        /* Check guard condition */
        switch (ov->guard_type)
        {
        case PFRN_GUARD_NONE:
            return ov->new_collision;
        case PFRN_GUARD_FLAG_SET:
            if (pfrn_flag_get(state->flags, ov->guard_param))
                return ov->new_collision;
            break;
        case PFRN_GUARD_FLAG_UNSET:
            if (!pfrn_flag_get(state->flags, ov->guard_param))
                return ov->new_collision;
            break;
        case PFRN_GUARD_VAR_EQ:
            if (ov->guard_param < PFR_NATIVE_MAX_VARS
                && state->vars[ov->guard_param] == ov->guard_value)
                return ov->new_collision;
            break;
        case PFRN_GUARD_BADGE_GE:
            if (pfrn_badge_count(state->flags, PFRN_BADGE_FLAG_START) >= (int)ov->guard_value)
                return ov->new_collision;
            break;
        }
    }
    return -1;
}

static bool is_water_behavior(uint16_t behavior)
{
    return behavior == MB_POND_WATER
        || behavior == MB_FAST_WATER
        || behavior == MB_DEEP_WATER
        || behavior == MB_OCEAN_WATER;
}

static bool has_surf_prerequisites(const PfrNativeState *state)
{
    return pfrn_badge_count(state->flags, PFRN_BADGE_FLAG_START) >= 5
        && pfrn_flag_get(state->flags, PFRN_FLAG_GOT_HM03);
}

static bool is_spin_behavior(uint16_t behavior)
{
    return behavior == MB_SPIN_RIGHT
        || behavior == MB_SPIN_LEFT
        || behavior == MB_SPIN_UP
        || behavior == MB_SPIN_DOWN
        || behavior == MB_SLIDE_EAST
        || behavior == MB_SLIDE_WEST
        || behavior == MB_SLIDE_NORTH
        || behavior == MB_SLIDE_SOUTH;
}

static uint8_t spin_direction(uint16_t behavior)
{
    switch (behavior)
    {
    case MB_SPIN_RIGHT:
    case MB_SLIDE_EAST:  return PFR_NATIVE_DIR_EAST;
    case MB_SPIN_LEFT:
    case MB_SLIDE_WEST:  return PFR_NATIVE_DIR_WEST;
    case MB_SPIN_UP:
    case MB_SLIDE_NORTH: return PFR_NATIVE_DIR_NORTH;
    case MB_SPIN_DOWN:
    case MB_SLIDE_SOUTH: return PFR_NATIVE_DIR_SOUTH;
    default:             return PFR_NATIVE_DIR_NONE;
    }
}

static bool behavior_blocks_direction(uint16_t behavior, uint8_t direction)
{
    switch (direction)
    {
    case PFR_NATIVE_DIR_NORTH:
        return behavior == MB_IMPASSABLE_NORTH
            || behavior == MB_IMPASSABLE_NORTHEAST
            || behavior == MB_IMPASSABLE_NORTHWEST;
    case PFR_NATIVE_DIR_SOUTH:
        return behavior == MB_IMPASSABLE_SOUTH
            || behavior == MB_IMPASSABLE_SOUTHEAST
            || behavior == MB_IMPASSABLE_SOUTHWEST;
    case PFR_NATIVE_DIR_WEST:
        return behavior == MB_IMPASSABLE_WEST
            || behavior == MB_IMPASSABLE_NORTHWEST
            || behavior == MB_IMPASSABLE_SOUTHWEST;
    case PFR_NATIVE_DIR_EAST:
        return behavior == MB_IMPASSABLE_EAST
            || behavior == MB_IMPASSABLE_NORTHEAST
            || behavior == MB_IMPASSABLE_SOUTHEAST;
    default:
        return false;
    }
}

static bool is_jumpable_ledge(uint16_t behavior, uint8_t direction)
{
    switch (direction)
    {
    case PFR_NATIVE_DIR_EAST:  return behavior == MB_JUMP_EAST;
    case PFR_NATIVE_DIR_WEST:  return behavior == MB_JUMP_WEST;
    case PFR_NATIVE_DIR_NORTH: return behavior == MB_JUMP_NORTH;
    case PFR_NATIVE_DIR_SOUTH: return behavior == MB_JUMP_SOUTH;
    default: return false;
    }
}

static bool stair_warp_behavior(uint16_t behavior)
{
    return behavior == MB_UP_RIGHT_STAIR_WARP
        || behavior == MB_UP_LEFT_STAIR_WARP
        || behavior == MB_DOWN_RIGHT_STAIR_WARP
        || behavior == MB_DOWN_LEFT_STAIR_WARP;
}

static bool arrow_warp_behavior(uint16_t behavior)
{
    return behavior == MB_EAST_ARROW_WARP
        || behavior == MB_WEST_ARROW_WARP
        || behavior == MB_NORTH_ARROW_WARP
        || behavior == MB_SOUTH_ARROW_WARP;
}

static bool post_step_warp_behavior(uint16_t behavior)
{
    return behavior == MB_CAVE_DOOR
        || behavior == MB_LADDER
        || behavior == MB_FALL_WARP
        || behavior == MB_REGULAR_WARP
        || behavior == MB_LAVARIDGE_1F_WARP
        || behavior == MB_UP_ESCALATOR
        || behavior == MB_DOWN_ESCALATOR
        || behavior == MB_UNION_ROOM_WARP;
}

static bool warp_door_behavior(uint16_t behavior)
{
    return behavior == MB_WARP_DOOR;
}

static bool arrow_warp_matches_direction(uint16_t behavior, uint8_t direction)
{
    switch (direction)
    {
    case PFR_NATIVE_DIR_NORTH:
        return behavior == MB_NORTH_ARROW_WARP;
    case PFR_NATIVE_DIR_SOUTH:
        return behavior == MB_SOUTH_ARROW_WARP;
    case PFR_NATIVE_DIR_WEST:
        return behavior == MB_WEST_ARROW_WARP;
    case PFR_NATIVE_DIR_EAST:
        return behavior == MB_EAST_ARROW_WARP;
    default:
        return false;
    }
}

static bool stair_warp_matches_direction(uint16_t behavior, uint8_t direction)
{
    switch (direction)
    {
    case PFR_NATIVE_DIR_WEST:
        return behavior == MB_UP_LEFT_STAIR_WARP
            || behavior == MB_DOWN_LEFT_STAIR_WARP;
    case PFR_NATIVE_DIR_EAST:
        return behavior == MB_UP_RIGHT_STAIR_WARP
            || behavior == MB_DOWN_RIGHT_STAIR_WARP;
    default:
        return false;
    }
}

static uint8_t stair_exit_direction(uint16_t behavior)
{
    switch (behavior)
    {
    case MB_UP_RIGHT_STAIR_WARP:
    case MB_DOWN_RIGHT_STAIR_WARP:
        return PFR_NATIVE_DIR_WEST;
    case MB_UP_LEFT_STAIR_WARP:
    case MB_DOWN_LEFT_STAIR_WARP:
        return PFR_NATIVE_DIR_EAST;
    default:
        return PFR_NATIVE_DIR_NONE;
    }
}

static uint8_t warp_entry_direction(uint16_t behavior, uint8_t fallback_direction)
{
    uint8_t stair_direction;

    if (behavior == MB_WARP_DOOR || behavior == MB_CAVE_DOOR)
        return PFR_NATIVE_DIR_SOUTH;
    if (behavior == MB_SOUTH_ARROW_WARP)
        return PFR_NATIVE_DIR_NORTH;
    if (behavior == MB_NORTH_ARROW_WARP)
        return PFR_NATIVE_DIR_SOUTH;
    if (behavior == MB_WEST_ARROW_WARP)
        return PFR_NATIVE_DIR_EAST;
    if (behavior == MB_EAST_ARROW_WARP)
        return PFR_NATIVE_DIR_WEST;
    stair_direction = stair_exit_direction(behavior);
    if (stair_direction != PFR_NATIVE_DIR_NONE)
        return stair_direction;
    return fallback_direction;
}

static const PfrNativeWarp *warp_at_coords(const PfrNativeMap *map, int16_t x, int16_t y)
{
    size_t i;

    if (map == NULL)
        return NULL;

    for (i = 0; i < map->warp_count; i++)
    {
        const PfrNativeWarp *warp = &map->warps[i];
        if (warp->x == x && warp->y == y)
            return warp;
    }

    return NULL;
}

static bool check_map_enter_cond(const PfrNativeState *state, const PfrNativeMapEnterAction *act)
{
    switch (act->cond_type) {
    case PFRN_GUARD_NONE:      return true;
    case PFRN_GUARD_FLAG_SET:  return pfrn_flag_get(state->flags, act->cond_param) != 0;
    case PFRN_GUARD_FLAG_UNSET:return pfrn_flag_get(state->flags, act->cond_param) == 0;
    case PFRN_GUARD_VAR_EQ:    return act->cond_param < PFR_NATIVE_MAX_VARS
                                      && state->vars[act->cond_param] == act->cond_value;
    case PFRN_GUARD_BADGE_GE:  return pfrn_badge_count(state->flags, PFRN_BADGE_FLAG_START)
                                      >= (int)act->cond_value;
    default: return true;
    }
}

static void run_map_enter_actions(PfrNativeState *state, const PfrNativeMap *map)
{
    for (size_t i = 0; i < map->enter_action_count; i++)
    {
        const PfrNativeMapEnterAction *act = &map->enter_actions[i];
        if (!check_map_enter_cond(state, act))
            continue;
        switch (act->action_type) {
        case 0: /* set_flag */
            state->flags = pfrn_flag_set(state->flags, act->action_param);
            break;
        case 1: /* set_var */
            if (act->action_param < PFR_NATIVE_MAX_VARS)
                state->vars[act->action_param] = act->action_value;
            break;
        case 2: /* clear_flag */
            state->flags = pfrn_flag_clear(state->flags, act->action_param);
            break;
        }
    }
}

static void reload_objects_for_map(PfrNativeState *state)
{
    const PfrNativeMap *map = pfr_native_get_map(state->current_map);
    size_t i;

    state->object_count = 0;
    if (map == NULL)
        return;

    /* Execute map-enter rules (sets flags/vars on map load) */
    if (map->enter_action_count > 0)
        run_map_enter_actions(state, map);

    for (i = 0; i < map->object_event_count && i < PFR_NATIVE_MAX_OBJECTS; i++)
    {
        const PfrNativeObjectEvent *src = &map->object_events[i];
        PfrNativeObjectState *dst = &state->objects[i];

        memset(dst, 0, sizeof(*dst));
        /* Check hide flag: if set, this NPC is hidden */
        if (src->hide_flag != 0xFF && pfrn_flag_get(state->flags, src->hide_flag))
        {
            dst->active = 0;
        }
        else
        {
            dst->active = 1;
        }
        dst->local_id = src->local_id;
        dst->graphics_id = src->graphics_id;
        dst->facing = src->facing;
        dst->default_facing = src->facing;
        dst->movement_type = src->movement_type;
        dst->script_id = src->script_id;
        dst->x = src->x;
        dst->y = src->y;
        state->object_count++;
    }
}

static int reset_to_map(PfrNativeState *state, PfrNativeMapId map_id, int16_t x, int16_t y,
                        uint8_t direction)
{
    const PfrNativeMap *map = pfr_native_get_map(map_id);

    if (map == NULL || tile_at(map, x, y) == NULL)
        return -1;

    state_clear(state);
    state->current_map = map_id;
    state->mode = PFR_NATIVE_MODE_OVERWORLD;
    state->player_direction = direction != PFR_NATIVE_DIR_NONE
        ? direction
        : PFR_NATIVE_DIR_SOUTH;
    state->player_gender = 0;
    state->player_x = x;
    state->player_y = y;
    memcpy(state->player_name, "RED", 4);
    reload_objects_for_map(state);
    return 0;
}

int pfr_native_reset_to_map(PfrNativeCore *core, PfrNativeMapId map_id, int16_t x, int16_t y,
                            PfrNativeDirection direction)
{
    c_init(core);
    return reset_to_map(&core->state, map_id, x, y, direction);
}

/* teleport_to_map: change position WITHOUT clearing state.
 * Preserves party, flags, vars, bag, money, pokedex -- everything.
 * Used for whiteout, fly, teleport, etc. */
static int teleport_to_map(PfrNativeState *state, PfrNativeMapId map_id,
                            int16_t x, int16_t y, uint8_t direction)
{
    const PfrNativeMap *map = pfr_native_get_map(map_id);
    if (map == NULL || tile_at(map, x, y) == NULL)
        return -1;

    state->current_map = map_id;
    state->player_x = x;
    state->player_y = y;
    state->player_direction = direction != PFR_NATIVE_DIR_NONE
        ? direction : PFR_NATIVE_DIR_SOUTH;
    state->mode = PFR_NATIVE_MODE_OVERWORLD;
    state->active_dialog_id = PFR_NATIVE_DIALOG_NONE;
    state->queued_dialog_id = PFR_NATIVE_DIALOG_NONE;
    state->dialog_page_index = 0;
    state->dialog_restore_object_index = PFR_NATIVE_INVALID_OBJECT;
    /* Clear battle state */
    memset(&state->battle, 0, sizeof(state->battle));
    reload_objects_for_map(state);
    return 0;
}

static int bootstrap_players_house_2f(PfrNativeState *state)
{
    if (reset_to_map(state, PFR_NATIVE_MAP_PLAYERS_HOUSE_2F, 6, 6, PFR_NATIVE_DIR_NORTH) != 0)
        return -1;
    state->vars[PFRN_VAR_MAP_SCENE_PALLET_TOWN_PLAYERS_HOUSE_2F] = 1;
    return 0;
}

/* Mark a species as seen in the pokedex */
static void pokedex_set_seen(PfrNativeState *state, uint16_t species)
{
    if (species == 0 || species > 412) return;
    state->pokedex_seen[(species - 1) / 16] |= (uint16_t)(1u << ((species - 1) % 16));
}

/* Mark a species as caught in the pokedex */
static void pokedex_set_caught(PfrNativeState *state, uint16_t species)
{
    if (species == 0 || species > 412) return;
    pokedex_set_seen(state, species);
    state->pokedex_caught[(species - 1) / 16] |= (uint16_t)(1u << ((species - 1) % 16));
}

/* ============================================================
 * Bag System Helpers
 * ============================================================ */

/* Get pocket array, count pointer, and max for a pocket index */
static PfrBagSlot *bag_pocket_slots(PfrBag *bag, int pocket, uint8_t **count_out, int *max_out)
{
    switch (pocket) {
    case 0: *count_out = &bag->item_count;     *max_out = PFR_BAG_ITEMS_MAX;     return bag->items;
    case 1: *count_out = &bag->key_item_count;  *max_out = PFR_BAG_KEY_ITEMS_MAX;  return bag->key_items;
    case 2: *count_out = &bag->ball_count;       *max_out = PFR_BAG_BALLS_MAX;      return bag->balls;
    default: *count_out = &bag->item_count;     *max_out = 0;                      return bag->items;
    }
}

/* Map item pocket field (1-5) to our pocket index (0-2) */
static int bag_pocket_for_item(uint16_t item_id)
{
    if (item_id == 0 || item_id >= PFR_NUM_ITEMS) return 0;
    uint8_t pocket = PFR_ITEMS[item_id].pocket;
    switch (pocket) {
    case 3: return 2;  /* Poke Balls */
    case 2: return 1;  /* Key Items */
    default: return 0; /* Items, TMs, Berries all go in Items pocket */
    }
}

/* Add item to bag. Returns 1 on success, 0 if full. */
static int bag_add_item(PfrBag *bag, uint16_t item_id, uint16_t count)
{
    if (item_id == 0 || item_id >= PFR_NUM_ITEMS || count == 0) return 0;
    int pocket = bag_pocket_for_item(item_id);
    uint8_t *cnt;
    int max;
    PfrBagSlot *slots = bag_pocket_slots(bag, pocket, &cnt, &max);
    /* Find existing slot */
    for (int i = 0; i < *cnt; i++) {
        if (slots[i].item_id == item_id) {
            slots[i].count += count;
            if (slots[i].count > 999) slots[i].count = 999;
            return 1;
        }
    }
    /* New slot */
    if (*cnt >= max) return 0;
    slots[*cnt].item_id = item_id;
    slots[*cnt].count = count;
    (*cnt)++;
    return 1;
}

/* Remove item from bag. Returns 1 on success, 0 if not found/insufficient. */
static int bag_remove_item(PfrBag *bag, uint16_t item_id, uint16_t count)
{
    if (item_id == 0 || item_id >= PFR_NUM_ITEMS || count == 0) return 0;
    int pocket = bag_pocket_for_item(item_id);
    uint8_t *cnt;
    int max;
    PfrBagSlot *slots = bag_pocket_slots(bag, pocket, &cnt, &max);
    for (int i = 0; i < *cnt; i++) {
        if (slots[i].item_id == item_id) {
            if (slots[i].count < count) return 0;
            slots[i].count -= count;
            if (slots[i].count == 0) {
                /* Shift remaining slots down */
                for (int j = i; j < *cnt - 1; j++)
                    slots[j] = slots[j + 1];
                memset(&slots[*cnt - 1], 0, sizeof(PfrBagSlot));
                (*cnt)--;
            }
            return 1;
        }
    }
    return 0;
}

/* Check if bag has item */
static int __attribute__((unused)) bag_has_item(const PfrBag *bag, uint16_t item_id)
{
    if (item_id == 0 || item_id >= PFR_NUM_ITEMS) return 0;
    int pocket = bag_pocket_for_item(item_id);
    const PfrBagSlot *slots;
    uint8_t cnt;
    switch (pocket) {
    case 0: slots = bag->items;     cnt = bag->item_count;     break;
    case 1: slots = bag->key_items; cnt = bag->key_item_count; break;
    case 2: slots = bag->balls;     cnt = bag->ball_count;     break;
    default: return 0;
    }
    for (int i = 0; i < cnt; i++)
        if (slots[i].item_id == item_id) return 1;
    return 0;
}

/* ============================================================
 * Item Use Effects
 * ============================================================ */

/* Use a healing/status/PP item on a party pokemon.
 * Returns 1 if item was consumed, 0 if it failed (can't use). */
static int use_item_on_pokemon(PfrNativeState *state, uint16_t item_id, int party_idx)
{
    if (party_idx < 0 || party_idx >= PFR_NATIVE_MAX_PARTY) return 0;
    PfrPokemon *mon = &state->party[party_idx];
    if (mon->species == 0) return 0;

    /* --- Potions (HP healing) --- */
    /* Potion=13, Super=22, Hyper=21, Max=20, Full Restore=19,
     * Fresh Water=26, Soda Pop=27, Lemonade=28, Moomoo Milk=29,
     * Berry Juice=44, Energy Root=31, EnergyPowder=30, Lava Cookie=38 */
    if (item_id == 13 || item_id == 22 || item_id == 21 || item_id == 20 ||
        item_id == 19 || item_id == 26 || item_id == 27 || item_id == 28 ||
        item_id == 29 || item_id == 44 || item_id == 31 || item_id == 30 ||
        item_id == 38) {
        if (mon->hp == 0 || mon->hp >= mon->max_hp) return 0; /* dead or full */
        uint8_t eff = PFR_ITEMS[item_id].effect;
        if (eff == 255) {
            mon->hp = mon->max_hp;
        } else {
            mon->hp += eff;
            if (mon->hp > mon->max_hp) mon->hp = mon->max_hp;
        }
        /* Full Restore also cures status */
        if (item_id == 19) mon->status = PFR_STATUS_NONE;
        bag_remove_item(&state->bag, item_id, 1);
        return 1;
    }

    /* --- Status cures --- */
    /* Antidote=14, Burn Heal=15, Ice Heal=16, Awakening=17, Parlyz Heal=18,
     * Full Heal=23, Heal Powder=32 */
    if (item_id == 14) { /* Antidote: cure PSN/TOX */
        if (!(mon->status & (PFR_STATUS_PSN | PFR_STATUS_TOX))) return 0;
        mon->status &= (uint8_t)~(PFR_STATUS_PSN | PFR_STATUS_TOX);
        bag_remove_item(&state->bag, item_id, 1);
        return 1;
    }
    if (item_id == 15) { /* Burn Heal */
        if (!(mon->status & PFR_STATUS_BRN)) return 0;
        mon->status &= (uint8_t)~PFR_STATUS_BRN;
        bag_remove_item(&state->bag, item_id, 1);
        return 1;
    }
    if (item_id == 16) { /* Ice Heal */
        if (!(mon->status & PFR_STATUS_FRZ)) return 0;
        mon->status &= (uint8_t)~PFR_STATUS_FRZ;
        bag_remove_item(&state->bag, item_id, 1);
        return 1;
    }
    if (item_id == 17) { /* Awakening */
        if (!(mon->status & PFR_STATUS_SLP)) return 0;
        mon->status &= (uint8_t)~PFR_STATUS_SLP;
        bag_remove_item(&state->bag, item_id, 1);
        return 1;
    }
    if (item_id == 18) { /* Parlyz Heal */
        if (!(mon->status & PFR_STATUS_PAR)) return 0;
        mon->status &= (uint8_t)~PFR_STATUS_PAR;
        bag_remove_item(&state->bag, item_id, 1);
        return 1;
    }
    if (item_id == 23 || item_id == 32 || item_id == 38) { /* Full Heal, Heal Powder, Lava Cookie */
        if (mon->status == PFR_STATUS_NONE) return 0;
        mon->status = PFR_STATUS_NONE;
        bag_remove_item(&state->bag, item_id, 1);
        return 1;
    }

    /* --- Revives --- */
    /* Revive=24 (half HP), Max Revive=25 (full HP), Revival Herb=33 (full) */
    if (item_id == 24 || item_id == 25 || item_id == 33) {
        if (mon->hp != 0) return 0; /* only works on fainted */
        if (item_id == 24)
            mon->hp = mon->max_hp / 2;
        else
            mon->hp = mon->max_hp;
        if (mon->hp == 0) mon->hp = 1;
        bag_remove_item(&state->bag, item_id, 1);
        return 1;
    }

    /* --- PP restores --- */
    /* Ether=34 (10PP one move), Max Ether=35 (full one move) */
    /* Elixir=36 (10PP all), Max Elixir=37 (full all) */
    /* For Ether/Max Ether we use the first move that needs PP */
    if (item_id == 34 || item_id == 35) {
        int restored = 0;
        for (int m = 0; m < 4; m++) {
            if (mon->moves[m] == 0 || mon->moves[m] >= PFR_NUM_MOVES) continue;
            uint8_t max_pp = PFR_MOVES[mon->moves[m]].pp;
            if (mon->pp[m] < max_pp) {
                if (item_id == 35) /* Max Ether */
                    mon->pp[m] = max_pp;
                else
                    mon->pp[m] = (uint8_t)(mon->pp[m] + 10 > max_pp ? max_pp : mon->pp[m] + 10);
                restored = 1;
                break;
            }
        }
        if (!restored) return 0;
        bag_remove_item(&state->bag, item_id, 1);
        return 1;
    }
    if (item_id == 36 || item_id == 37) {
        int restored = 0;
        for (int m = 0; m < 4; m++) {
            if (mon->moves[m] == 0 || mon->moves[m] >= PFR_NUM_MOVES) continue;
            uint8_t max_pp = PFR_MOVES[mon->moves[m]].pp;
            if (mon->pp[m] < max_pp) {
                if (item_id == 37) /* Max Elixir */
                    mon->pp[m] = max_pp;
                else
                    mon->pp[m] = (uint8_t)(mon->pp[m] + 10 > max_pp ? max_pp : mon->pp[m] + 10);
                restored = 1;
            }
        }
        if (!restored) return 0;
        bag_remove_item(&state->bag, item_id, 1);
        return 1;
    }

    /* --- Rare Candy (68) --- */
    if (item_id == 68) {
        if (mon->level >= 100) return 0;
        mon->level++;
        pfr_recalc_stats(mon);
        mon->hp = mon->max_hp; /* Rare candy fully heals in Gen 3 */
        /* Update experience to match new level */
        if (mon->species < PFR_NUM_SPECIES) {
            const PfrSpeciesData *base = &PFR_SPECIES[mon->species];
            if (base->growth_rate < PFR_NUM_GROWTH_RATES && mon->level <= 100)
                mon->exp = PFR_EXP_TABLES[base->growth_rate][mon->level];
        }
        /* Check for new moves at this level */
        if (mon->species < PFR_NUM_SPECIES) {
            const PfrLearnsetIndex *ls = &PFR_LEARNSETS[mon->species];
            for (uint16_t i = 0; i < ls->count; i++) {
                uint16_t packed = PFR_LEARNSET_DATA[ls->start + i];
                uint8_t learn_lv = (uint8_t)(packed >> 9);
                uint16_t move_id = packed & 0x1FFu;
                if (learn_lv == mon->level && move_id > 0 && move_id < PFR_NUM_MOVES) {
                    /* Try to learn the move */
                    int has_move = 0;
                    for (int s = 0; s < 4; s++)
                        if (mon->moves[s] == move_id) { has_move = 1; break; }
                    if (!has_move) {
                        for (int s = 0; s < 4; s++) {
                            if (mon->moves[s] == 0) {
                                mon->moves[s] = move_id;
                                mon->pp[s] = PFR_MOVES[move_id].pp;
                                break;
                            }
                        }
                    }
                }
            }
        }
        bag_remove_item(&state->bag, item_id, 1);
        return 1;
    }

    /* --- Repels (86=Repel 100 steps, 83=Super 200, 84=Max 250) --- */
    if (item_id == 86 || item_id == 83 || item_id == 84) {
        if (state->repel_steps > 0) return 0; /* already active */
        state->repel_steps = PFR_ITEMS[item_id].effect;
        bag_remove_item(&state->bag, item_id, 1);
        return 1;
    }

    /* --- X-items (battle only, handled separately) --- */
    /* --- Pokeballs (battle only, handled separately) --- */
    /* --- TMs/HMs, Key Items: not usable in this context --- */

    return 0;
}

static int bootstrap_pallet_town(PfrNativeState *state)
{
    if (reset_to_map(state, PFR_NATIVE_MAP_PALLET_TOWN, 6, 8, PFR_NATIVE_DIR_NORTH) != 0)
        return -1;

    /* Post-fetch-quest state: player has starter, beaten rival, delivered
     * Oak's parcel, received Pokedex and Pokeballs. Ready to explore. */
    state->starter_mon = 0;  /* Bulbasaur */
    state->flags = pfrn_flag_set(state->flags, PFRN_FLAG_SYS_POKEMON_GET);
    /* Create a real Bulbasaur at level 5 with proper stats/moves */
    pfr_generate_pokemon(&state->party[0], 1 /* SPECIES_BULBASAUR */, 5, &state->rng_value);
    state->party_count = 1;
    state->flags = pfrn_flag_set(state->flags, PFRN_FLAG_SYS_POKEDEX_GET);
    /* Running shoes are given on Route 3 after Pewter City, not at start */
    state->flags = pfrn_flag_set(state->flags, PFRN_FLAG_VISITED_OAKS_LAB);
    state->flags = pfrn_flag_set(state->flags, PFRN_FLAG_BEAT_RIVAL_IN_OAKS_LAB);
    state->flags = pfrn_flag_set(state->flags, PFRN_FLAG_GOT_POKEBALLS_FROM_OAK_AFTER_22_RIVAL);
    state->flags = pfrn_flag_set(state->flags, PFRN_FLAG_PALLET_LADY_NOT_BLOCKING_SIGN);
    /* Hide NPCs that should be gone after intro sequence */
    state->flags = pfrn_flag_set(state->flags, PFRN_FLAG_HIDE_OAK_IN_PALLET_TOWN);
    state->flags = pfrn_flag_set(state->flags, PFRN_FLAG_HIDE_BULBASAUR_BALL);
    state->flags = pfrn_flag_set(state->flags, PFRN_FLAG_HIDE_SQUIRTLE_BALL);
    state->flags = pfrn_flag_set(state->flags, PFRN_FLAG_HIDE_CHARMANDER_BALL);
    state->flags = pfrn_flag_set(state->flags, PFRN_FLAG_HIDE_RIVAL_IN_LAB);
    state->flags = pfrn_flag_set(state->flags, PFRN_FLAG_HIDE_POKEDEX);
    state->vars[PFRN_VAR_MAP_SCENE_PALLET_TOWN_PROFESSOR_OAKS_LAB] = 4;
    state->vars[PFRN_VAR_MAP_SCENE_VIRIDIAN_CITY_MART] = 2;
    state->vars[PFRN_VAR_STARTER_MON] = 0;

    /* Set initial heal location to player's house */
    state->last_heal_map = (uint16_t)PFR_NATIVE_MAP_PLAYERS_HOUSE_1F;
    state->last_heal_x = 8;
    state->last_heal_y = 8;
    state->money = 3000;  /* Starting money */
    state->trainer_id_num = (uint16_t)(state->rng_value & 0xFFFF);

    /* Mark Bulbasaur as caught in pokedex */
    pokedex_set_caught(state, 1);

    /* Starting inventory: 5 Pokeballs + 5 Potions (matches post-Oak in FireRed) */
    bag_add_item(&state->bag, 4, 5);   /* POKe BALL */
    bag_add_item(&state->bag, 13, 5);  /* POTION */

    /* Re-load objects now that flags are set (hides post-intro NPCs) */
    reload_objects_for_map(state);
    return 0;
}

static void open_dialog(PfrNativeState *state, uint8_t dialog_id)
{
    state->mode = PFR_NATIVE_MODE_DIALOG;
    state->active_dialog_id = dialog_id;
    state->dialog_page_index = 0;
}

static bool object_occupied_on_map(const PfrNativeState *state, const PfrNativeMap *map,
                                   int16_t x, int16_t y)
{
    size_t i;

    if (map == NULL)
        return false;

    if (map->map_id == state->current_map)
    {
        for (i = 0; i < state->object_count; i++)
        {
            const PfrNativeObjectState *obj = &state->objects[i];
            if (obj->active && obj->x == x && obj->y == y)
                return true;
        }
        return false;
    }

    for (i = 0; i < map->object_event_count; i++)
    {
        const PfrNativeObjectEvent *obj = &map->object_events[i];
        if (obj->x == x && obj->y == y)
            return true;
    }

    return false;
}

static int find_object_at(PfrNativeState *state, int16_t x, int16_t y)
{
    size_t i;
    for (i = 0; i < state->object_count; i++)
    {
        PfrNativeObjectState *obj = &state->objects[i];
        if (obj->active && obj->x == x && obj->y == y)
            return (int)i;
    }
    return -1;
}

static void move_delta(uint8_t direction, int16_t *dx, int16_t *dy)
{
    *dx = 0;
    *dy = 0;
    switch (direction)
    {
    case PFR_NATIVE_DIR_NORTH:
        *dy = -1;
        break;
    case PFR_NATIVE_DIR_SOUTH:
        *dy = 1;
        break;
    case PFR_NATIVE_DIR_WEST:
        *dx = -1;
        break;
    case PFR_NATIVE_DIR_EAST:
        *dx = 1;
        break;
    }
}

static bool movement_blocked(const PfrNativeState *state, uint8_t direction,
                             int16_t dest_x, int16_t dest_y)
{
    const PfrNativeMap *map = pfr_native_get_map(state->current_map);
    const PfrNativeTile *current_tile = tile_at(map, state->player_x, state->player_y);
    const PfrNativeTile *dest_tile = tile_at(map, dest_x, dest_y);
    int override_collision;
    uint8_t effective_collision;

    if (current_tile == NULL || dest_tile == NULL)
        return true;

    /* Water tiles: block unless player has Surf prerequisites.
     * GBA map data stores collision=0 for water (passable layer), so we must
     * check the behavior BEFORE the collision byte — otherwise the agent can
     * walk straight across oceans. */
    if (is_water_behavior(dest_tile->behavior) && !has_surf_prerequisites(state))
        return true;

    /* Determine effective collision: check metatile overrides first */
    override_collision = check_metatile_override(map, state, dest_x, dest_y);
    effective_collision = (override_collision >= 0) ? (uint8_t)override_collision
                                                    : dest_tile->collision;

    if (effective_collision != 0)
    {
        /* Water tiles with surf: allow passage */
        if (is_water_behavior(dest_tile->behavior) && has_surf_prerequisites(state))
        {
            /* allowed — fall through to remaining checks */
        }
        else
        {
            return true;
        }
    }

    if (object_occupied_on_map(state, map, dest_x, dest_y))
        return true;
    if (behavior_blocks_direction(current_tile->behavior, direction))
        return true;
    if (behavior_blocks_direction(dest_tile->behavior, opposite_dir(direction)))
        return true;
    return false;
}

static const PfrNativeConnection *connection_for_direction(const PfrNativeMap *map,
                                                           uint8_t direction)
{
    size_t i;
    uint8_t expected = PFR_NATIVE_CONN_UNKNOWN;

    if (map == NULL)
        return NULL;

    switch (direction)
    {
    case PFR_NATIVE_DIR_NORTH:
        expected = PFR_NATIVE_CONN_NORTH;
        break;
    case PFR_NATIVE_DIR_SOUTH:
        expected = PFR_NATIVE_CONN_SOUTH;
        break;
    case PFR_NATIVE_DIR_WEST:
        expected = PFR_NATIVE_CONN_WEST;
        break;
    case PFR_NATIVE_DIR_EAST:
        expected = PFR_NATIVE_CONN_EAST;
        break;
    default:
        return NULL;
    }

    for (i = 0; i < map->connection_count; i++)
    {
        if (map->connections[i].direction == expected)
            return &map->connections[i];
    }

    return NULL;
}

static bool try_connection_move(PfrNativeState *state, uint8_t direction)
{
    const PfrNativeMap *map = pfr_native_get_map(state->current_map);
    const PfrNativeConnection *connection;
    const PfrNativeMap *dest_map;
    const PfrNativeTile *current_tile;
    const PfrNativeTile *dest_tile;
    int16_t dest_x;
    int16_t dest_y;

    if (map == NULL)
        return false;

    connection = connection_for_direction(map, direction);
    if (connection == NULL || connection->dest_map == PFR_NATIVE_MAP_INVALID)
        return false;

    current_tile = tile_at(map, state->player_x, state->player_y);
    if (current_tile == NULL || behavior_blocks_direction(current_tile->behavior, direction))
        return false;

    dest_map = pfr_native_get_map(connection->dest_map);
    if (dest_map == NULL)
        return false;

    switch (direction)
    {
    case PFR_NATIVE_DIR_NORTH:
        dest_x = state->player_x - connection->offset;
        dest_y = (int16_t)dest_map->height - 1;
        break;
    case PFR_NATIVE_DIR_SOUTH:
        dest_x = state->player_x - connection->offset;
        dest_y = 0;
        break;
    case PFR_NATIVE_DIR_WEST:
        dest_x = (int16_t)dest_map->width - 1;
        dest_y = state->player_y - connection->offset;
        break;
    case PFR_NATIVE_DIR_EAST:
        dest_x = 0;
        dest_y = state->player_y - connection->offset;
        break;
    default:
        return false;
    }

    dest_tile = tile_at(dest_map, dest_x, dest_y);
    if (dest_tile == NULL)
        return false;

    /* Water tiles on connection border: block without Surf.
     * Must check behavior BEFORE collision — GBA water has collision=0. */
    if (is_water_behavior(dest_tile->behavior) && !has_surf_prerequisites(state))
        return false;

    {
        /* Check metatile overrides on the destination map */
        int override_collision = check_metatile_override(dest_map, state, dest_x, dest_y);
        uint8_t effective_collision = (override_collision >= 0) ? (uint8_t)override_collision
                                                                : dest_tile->collision;
        if (effective_collision != 0)
        {
            /* Water tiles with surf: allow */
            if (is_water_behavior(dest_tile->behavior) && has_surf_prerequisites(state))
            {
                /* Surf allowed */
            }
            else
            {
                /* Non-water collision on map border: ALLOW crossing.
                 * The GBA game allows free passage along map connection
                 * borders; the collision byte is for within-map blocking. */
            }
        }
    }
    if (object_occupied_on_map(state, dest_map, dest_x, dest_y))
        return false;

    state->current_map = dest_map->map_id;
    state->player_x = dest_x;
    state->player_y = dest_y;
    reload_objects_for_map(state);
    return true;
}

static void apply_warp_entry(PfrNativeState *state, PfrNativeMapId previous_map,
                             uint8_t previous_direction)
{
    const PfrNativeTile *dest_tile = tile_at(pfr_native_get_map(state->current_map),
                                             state->player_x,
                                             state->player_y);
    if (dest_tile != NULL)
        state->player_direction = warp_entry_direction(dest_tile->behavior, previous_direction);

    if (state->current_map == PFR_NATIVE_MAP_PLAYERS_HOUSE_2F
        && previous_map == PFR_NATIVE_MAP_PLAYERS_HOUSE_1F
        && state->vars[PFRN_VAR_MAP_SCENE_PALLET_TOWN_PLAYERS_HOUSE_2F] == 0)
    {
        state->player_direction = PFR_NATIVE_DIR_NORTH;
        state->vars[PFRN_VAR_MAP_SCENE_PALLET_TOWN_PLAYERS_HOUSE_2F] = 1;
    }
}

static bool apply_warp_from(PfrNativeState *state, const PfrNativeWarp *warp,
                            uint8_t previous_direction)
{
    const PfrNativeMap *dest_map;
    const PfrNativeWarp *dest;
    PfrNativeMapId previous_map;

    if (warp == NULL)
        return false;
    if (!warp->supported)
        return false;
    if (warp->dest_map >= PFR_NATIVE_MAP_COUNT)
        return false;

    dest_map = pfr_native_get_map(warp->dest_map);
    if (dest_map == NULL)
        return false;
    if (warp->dest_warp_id >= dest_map->warp_count)
        return false;

    dest = &dest_map->warps[warp->dest_warp_id];
    previous_map = state->current_map;
    state->current_map = warp->dest_map;
    state->player_x = dest->x;
    state->player_y = dest->y;
    reload_objects_for_map(state);
    apply_warp_entry(state, previous_map, previous_direction);
    return true;
}

static bool try_warp_at(PfrNativeState *state, int16_t x, int16_t y, uint8_t previous_direction)
{
    return apply_warp_from(state, warp_at_coords(pfr_native_get_map(state->current_map), x, y),
                           previous_direction);
}

static bool facing_matches(uint8_t mask, uint8_t direction)
{
    if (mask == 0x0F)
        return true;
    return (mask & (1u << direction)) != 0;
}

/* ---- Data-driven script executor ---- */

static bool check_guard(const PfrNativeState *state, const PfrNativeScript *script)
{
    switch (script->guard_type) {
    case PFRN_GUARD_NONE:      return true;
    case PFRN_GUARD_FLAG_SET:  return pfrn_flag_get(state->flags, script->guard_param) != 0;
    case PFRN_GUARD_FLAG_UNSET:return pfrn_flag_get(state->flags, script->guard_param) == 0;
    case PFRN_GUARD_VAR_EQ:    return script->guard_param < PFR_NATIVE_MAX_VARS
                                      && state->vars[script->guard_param] == script->guard_value;
    case PFRN_GUARD_BADGE_GE:  return pfrn_badge_count(state->flags, PFRN_BADGE_FLAG_START)
                                      >= (int)script->guard_value;
    default: return true;
    }
}

static void execute_action(PfrNativeState *state, const PfrNativeScriptAction *act)
{
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
    case PFRN_ACT_DIALOG:
        open_dialog(state, act->param);
        break;
    case PFRN_ACT_AUTO_BATTLE: {
        /* Init trainer battle: value=trainer_id_lo, param=trainer_id_hi, extra=flag_id */
        uint16_t trainer_id = (uint16_t)act->value | ((uint16_t)act->param << 8);
        uint8_t flag_id = act->extra;
        /* Set victory flag immediately (battle auto-resolves in step loop) */
        if (flag_id != PFRN_FLAG_NONE)
            state->flags = pfrn_flag_set(state->flags, flag_id);
        /* Init real battle if player has pokemon */
        if (state->party_count > 0 && state->party[0].species != 0) {
            pfr_init_trainer_battle(&state->battle, trainer_id, &state->rng_value);
            state->mode = PFR_NATIVE_MODE_BATTLE;
        } else {
            /* Fallback: auto-win for backward compat when no party initialized */
            if (state->party_count < PFR_NATIVE_MAX_PARTY)
                state->party_count++;
        }
        /* param is repurposed for trainer_id_hi, no dialog open here */
        break;
    }
    case PFRN_ACT_GIVE_STARTER: {
        /* Create a real starter pokemon at level 5 */
        static const uint16_t starter_species[3] = {1, 7, 4}; /* Bulbasaur, Squirtle, Charmander */
        uint16_t species = (act->value < 3) ? starter_species[act->value] : starter_species[0];
        state->starter_mon = act->value;
        state->flags = pfrn_flag_set(state->flags, PFRN_FLAG_SYS_POKEMON_GET);
        if (state->party_count < PFR_NATIVE_MAX_PARTY) {
            pfr_generate_pokemon(&state->party[state->party_count], species, 5, &state->rng_value);
            pokedex_set_caught(state, species);
            state->party_count++;
        }
        if (act->param != PFR_NATIVE_DIALOG_NONE)
            open_dialog(state, act->param);
        break;
    }
    case PFRN_ACT_POKECENTER_HEAL:
        pfr_heal_party(state->party, PFR_NATIVE_MAX_PARTY);
        /* Set this PokeCenter as the respawn point */
        state->last_heal_map = (uint16_t)state->current_map;
        state->last_heal_x = state->player_x;
        state->last_heal_y = state->player_y;
        if (act->param != PFR_NATIVE_DIALOG_NONE)
            open_dialog(state, act->param);
        break;
    case PFRN_ACT_OPEN_SHOP:
        /* Open shop with default Viridian-like inventory */
        memset(&state->shop, 0, sizeof(state->shop));
        state->shop.inventory[0] = 4;   /* POKe BALL */
        state->shop.inventory[1] = 13;  /* POTION */
        state->shop.inventory[2] = 14;  /* ANTIDOTE */
        state->shop.inventory[3] = 18;  /* PARLYZ HEAL */
        state->shop.inventory[4] = 86;  /* REPEL */
        state->shop.inv_count = 5;
        state->shop.menu = 0;
        state->shop.cursor = 0;
        state->mode = PFR_NATIVE_MODE_SHOP;
        break;
    case PFRN_ACT_OPEN_PC:
        state->pc_menu = 0;
        state->pc_cursor = 0;
        state->mode = PFR_NATIVE_MODE_PC;
        break;
    default:
        break;
    }
}

static bool handle_script_at(PfrNativeState *state, uint8_t script_id, int object_index);

static bool handle_script(PfrNativeState *state, uint8_t script_id)
{
    return handle_script_at(state, script_id, -1);
}

static bool handle_script_at(PfrNativeState *state, uint8_t script_id, int object_index)
{
    /* Try data-driven scripts first */
    if (script_id > 0 && script_id < gPfrNativeScriptCount)
    {
        const PfrNativeScript *script = &gPfrNativeScripts[script_id];
        /* Skip scripts with no actions AND no guard — those are legacy stubs
         * handled by the switch below. Only process data-driven scripts that
         * have either actions to execute or a guard to check. */
        if (script->action_count > 0 || script->guard_type != PFRN_GUARD_NONE)
        {
            if (!check_guard(state, script))
                return false;  /* guard failed — script not triggered */
            for (int i = 0; i < script->action_count; i++)
                execute_action(state, &script->actions[i]);
            /* Scripts with 0 actions but a passing guard = "remove obstacle"
             * (e.g. cut trees: guard=has_HM01, action=deactivate object) */
            if (script->action_count == 0 && object_index >= 0
                && object_index < (int)state->object_count)
            {
                state->objects[object_index].active = 0;
            }
            return true;
        }
    }

    /* Legacy fallback for complex scripts not yet in the data tables */
    switch (script_id)
    {
    case PFR_NATIVE_SCRIPT_PLAYERS_HOUSE_1F_MOM:
        if (pfrn_flag_get(state->flags, PFRN_FLAG_BEAT_RIVAL_IN_OAKS_LAB))
        {
            state->queued_dialog_id = PFR_NATIVE_DIALOG_PLAYERS_HOUSE_1F_MOM_HEAL_2;
            open_dialog(state, PFR_NATIVE_DIALOG_PLAYERS_HOUSE_1F_MOM_HEAL_1);
        }
        else if (state->player_gender == 0)
        {
            open_dialog(state, PFR_NATIVE_DIALOG_PLAYERS_HOUSE_1F_MOM_MALE);
        }
        else
        {
            open_dialog(state, PFR_NATIVE_DIALOG_PLAYERS_HOUSE_1F_MOM_FEMALE);
        }
        return true;
    case PFR_NATIVE_SCRIPT_PLAYERS_HOUSE_1F_TV:
        if (state->player_direction == PFR_NATIVE_DIR_NORTH)
        {
            if (state->player_gender == 0)
                open_dialog(state, PFR_NATIVE_DIALOG_PLAYERS_HOUSE_1F_TV_MALE);
            else
                open_dialog(state, PFR_NATIVE_DIALOG_PLAYERS_HOUSE_1F_TV_FEMALE);
        }
        else
        {
            open_dialog(state, PFR_NATIVE_DIALOG_PLAYERS_HOUSE_1F_TV_WRONG_SIDE);
        }
        return true;
    case PFR_NATIVE_SCRIPT_PLAYERS_HOUSE_2F_NES:
        open_dialog(state, PFR_NATIVE_DIALOG_PLAYERS_HOUSE_2F_NES);
        return true;
    case PFR_NATIVE_SCRIPT_PLAYERS_HOUSE_2F_SIGN:
        open_dialog(state, PFR_NATIVE_DIALOG_PLAYERS_HOUSE_2F_SIGN);
        return true;
    case PFR_NATIVE_SCRIPT_PLAYERS_HOUSE_2F_PC:
        open_dialog(state, PFR_NATIVE_DIALOG_PLAYERS_HOUSE_2F_PC);
        return true;
    default:
        return false;
    }
}

/* ---- Coord event check (runs after every step) ---- */
static bool check_coord_events(PfrNativeState *state)
{
    const PfrNativeMap *map = pfr_native_get_map(state->current_map);
    if (map == NULL || map->coord_event_count == 0)
        return false;

    for (size_t i = 0; i < map->coord_event_count; i++)
    {
        const PfrNativeCoordEvent *evt = &map->coord_events[i];
        if (evt->x != state->player_x || evt->y != state->player_y)
            continue;
        /* Check var guard */
        if (evt->var_id != 0xFF)
        {
            if (evt->var_id >= PFR_NATIVE_MAX_VARS)
                continue;
            if (state->vars[evt->var_id] != evt->var_value)
                continue;
        }
        if (evt->script_id != PFR_NATIVE_SCRIPT_NONE)
            return handle_script(state, evt->script_id);
    }
    return false;
}

static bool try_interaction(PfrNativeState *state)
{
    const PfrNativeMap *map = pfr_native_get_map(state->current_map);
    int16_t dx;
    int16_t dy;
    int16_t target_x;
    int16_t target_y;
    int object_index;
    size_t i;

    if (map == NULL)
        return false;

    move_delta(state->player_direction, &dx, &dy);
    target_x = state->player_x + dx;
    target_y = state->player_y + dy;

    object_index = find_object_at(state, target_x, target_y);
    if (object_index >= 0)
    {
        PfrNativeObjectState *obj = &state->objects[object_index];
        state->dialog_restore_object_index = (uint8_t)object_index;
        state->dialog_restore_object_facing = obj->facing;
        obj->facing = opposite_dir(state->player_direction);
        return handle_script_at(state, obj->script_id, object_index);
    }

    for (i = 0; i < map->bg_event_count; i++)
    {
        const PfrNativeBgEvent *event = &map->bg_events[i];
        if (event->x != target_x || event->y != target_y)
            continue;
        if (!facing_matches(event->facing_mask, state->player_direction))
            continue;
        return handle_script(state, event->script_id);
    }

    return false;
}

void c_init(PfrNativeCore *core)
{
    memset(core, 0, sizeof(*core));
    core->state.current_map = PFR_NATIVE_MAP_INVALID;
    core->state.dialog_restore_object_index = PFR_NATIVE_INVALID_OBJECT;
}

int c_reset(PfrNativeCore *core, int bootstrap_id, const PfrNativeSnapshot *snapshot)
{
    c_init(core);
    if (snapshot != NULL)
    {
        memcpy(&core->state, snapshot, sizeof(*snapshot));
        return 0;
    }
    switch (bootstrap_id)
    {
    case PFR_NATIVE_BOOTSTRAP_PLAYERS_HOUSE_2F:
        return bootstrap_players_house_2f(&core->state);
    case PFR_NATIVE_BOOTSTRAP_PALLET_TOWN:
        return bootstrap_pallet_town(&core->state);
    default:
        return -1;
    }
}

static PfrNativeStepResult step_dialog(PfrNativeCore *core, PfrNativeAction action)
{
    PfrNativeState *state = &core->state;
    const PfrNativeDialog *dialog;
    PfrNativeStepResult result = { state->mode, PFR_NATIVE_EVENT_NONE, 0, 0 };

    if (action != PFR_NATIVE_ACTION_A && action != PFR_NATIVE_ACTION_B)
        return result;

    dialog = &gPfrNativeDialogs[state->active_dialog_id];
    if (state->dialog_page_index + 1 < dialog->page_count)
    {
        state->dialog_page_index++;
        result.event = PFR_NATIVE_EVENT_DIALOG_ADVANCED;
        result.changed = 1;
        return result;
    }

    if (state->queued_dialog_id != PFR_NATIVE_DIALOG_NONE)
    {
        state->active_dialog_id = state->queued_dialog_id;
        state->queued_dialog_id = PFR_NATIVE_DIALOG_NONE;
        state->dialog_page_index = 0;
        result.event = PFR_NATIVE_EVENT_DIALOG_ADVANCED;
        result.changed = 1;
        return result;
    }

    {
        /* Check for Mom heal: dialog closing is the heal dialog with no queued follow-up */
        uint8_t closing_dialog = state->active_dialog_id;

        state->mode = PFR_NATIVE_MODE_OVERWORLD;
        state->active_dialog_id = PFR_NATIVE_DIALOG_NONE;
        state->dialog_page_index = 0;
        result.mode = state->mode;
        result.event = PFR_NATIVE_EVENT_DIALOG_CLOSED;
        result.changed = 1;

        if (state->dialog_restore_object_index != PFR_NATIVE_INVALID_OBJECT
            && state->dialog_restore_object_index < state->object_count)
        {
            state->objects[state->dialog_restore_object_index].facing =
                state->dialog_restore_object_facing;
        }
        state->dialog_restore_object_index = PFR_NATIVE_INVALID_OBJECT;
        state->dialog_restore_object_facing = PFR_NATIVE_DIR_NONE;

        /* Heal party when Mom's heal dialog finishes */
        if (closing_dialog == PFR_NATIVE_DIALOG_PLAYERS_HOUSE_1F_MOM_HEAL_2) {
            pfr_heal_party(state->party, PFR_NATIVE_MAX_PARTY);
            /* Set heal location to Mom's house */
            state->last_heal_map = (uint16_t)PFR_NATIVE_MAP_PLAYERS_HOUSE_1F;
            state->last_heal_x = 8;
            state->last_heal_y = 8;
        }
    }
    return result;
}

/* Sync party_count from the party array */
static void sync_party_count(PfrNativeState *state)
{
    state->party_count = 0;
    for (int i = 0; i < PFR_NATIVE_MAX_PARTY; i++) {
        if (state->party[i].species != 0)
            state->party_count = (uint8_t)(i + 1);
    }
}

/* Handle post-turn battle result: transition to turn result display or end battle.
 * battle_result: 0=continues, 1=won, 2=lost, 3=fled, 4=caught */
static void handle_battle_result(PfrNativeState *state, int battle_result)
{
    PfrBattleState *battle = &state->battle;

    if (battle_result == 0) {
        /* Show turn result messages before returning to main menu */
        battle->menu_state = PFR_BATTLE_MENU_TURN_RESULT;
        battle->msg_page = 0;
        /* Count message pages dynamically based on what happened */
        battle->msg_total = 0;
        /* Page: player used move */
        if (battle->last_player_move > 0) battle->msg_total++;
        /* Page: super effective / not very effective */
        if (battle->last_eff != 10 && battle->last_player_dmg > 0)
            battle->msg_total++;
        /* Page: critical hit */
        if (battle->last_was_crit) battle->msg_total++;
        /* Page: opponent used move */
        if (battle->last_opp_move > 0) battle->msg_total++;
        /* Page: opponent fainted */
        if (battle->opp_fainted) {
            battle->msg_total++; /* "Foe X fainted!" */
            battle->msg_total++; /* "gained X EXP" */
            if (battle->last_new_level > 0)
                battle->msg_total++; /* "grew to level X!" */
        }
        /* Page: player mon fainted */
        if (battle->player_fainted) {
            battle->msg_total++; /* "Your X fainted!" */
        }
        if (battle->msg_total == 0) battle->msg_total = 1;
        return;
    }

    /* Mark opponent pokemon as seen */
    for (int i = 0; i < (int)battle->opp_count; i++)
        pokedex_set_seen(state, battle->opponent[i].species);

    if (battle_result == 2) {
        /* Whiteout: heal party and teleport to last heal location.
         * Uses teleport_to_map() to PRESERVE party/flags/vars/money/pokedex. */
        pfr_heal_party(state->party, PFR_NATIVE_MAX_PARTY);
        /* Lose half money on whiteout (Gen 3 formula) */
        state->money /= 2;
        /* Teleport to last heal location (default: player's house 1F) */
        if (state->last_heal_map != PFR_NATIVE_MAP_INVALID && state->last_heal_map != 0)
            teleport_to_map(state, (PfrNativeMapId)state->last_heal_map,
                            state->last_heal_x, state->last_heal_y, PFR_NATIVE_DIR_SOUTH);
        else
            teleport_to_map(state, PFR_NATIVE_MAP_PLAYERS_HOUSE_1F, 8, 8, PFR_NATIVE_DIR_SOUTH);
        sync_party_count(state);
        return;
    }

    if (battle_result == 4) {
        /* Caught: mark as caught in pokedex */
        pokedex_set_caught(state, battle->opponent[battle->opp_slot].species);
    }

    /* Won (1), fled (3), caught (4): return to overworld */
    state->mode = PFR_NATIVE_MODE_OVERWORLD;
    memset(&state->battle, 0, sizeof(state->battle));
    sync_party_count(state);
}

PfrNativeStepResult c_step(PfrNativeCore *core, PfrNativeAction action)
{
    PfrNativeState *state = &core->state;
    PfrNativeStepResult result = { state->mode, PFR_NATIVE_EVENT_NONE, 0, 0 };
    const PfrNativeMap *map;
    int16_t dx;
    int16_t dy;
    int16_t dest_x;
    int16_t dest_y;
    bool changed = false;

    /* ---- Battle mode (menu state machine) ---- */
    if (state->mode == PFR_NATIVE_MODE_BATTLE)
    {
        PfrBattleState *battle = &state->battle;

        if (action == PFR_NATIVE_ACTION_NONE ||
            action == PFR_NATIVE_ACTION_START ||
            action == PFR_NATIVE_ACTION_SELECT) {
            /* No battle input — just return current state */
            result.mode = state->mode;
            core->last_step = result;
            return result;
        }

        /* Battle intro sequence: "Wild X appeared!" -> "Go! Y!" -> main menu */
        if (battle->intro_phase > 0 && battle->intro_phase < 3) {
            if (action == PFR_NATIVE_ACTION_A || action == PFR_NATIVE_ACTION_B) {
                battle->intro_phase++;
                if (battle->intro_phase >= 3) {
                    battle->intro_phase = 0;
                    battle->menu_state = PFR_BATTLE_MENU_MAIN;
                    battle->menu_cursor = 0;
                }
            }
            result.mode = state->mode;
            result.changed = 1;
            core->last_step = result;
            return result;
        }

        switch (battle->menu_state) {
        case PFR_BATTLE_MENU_MAIN: {
            /* Cursor 0=FIGHT, 1=BAG, 2=POKEMON, 3=RUN (2x2 grid) */
            switch (action) {
            case PFR_NATIVE_ACTION_UP:
                battle->menu_cursor ^= 2; /* toggle row */
                break;
            case PFR_NATIVE_ACTION_DOWN:
                battle->menu_cursor ^= 2;
                break;
            case PFR_NATIVE_ACTION_LEFT:
                battle->menu_cursor ^= 1; /* toggle col */
                break;
            case PFR_NATIVE_ACTION_RIGHT:
                battle->menu_cursor ^= 1;
                break;
            case PFR_NATIVE_ACTION_A:
                switch (battle->menu_cursor) {
                case 0: /* FIGHT */
                    battle->menu_state = PFR_BATTLE_MENU_FIGHT;
                    battle->menu_cursor = 0;
                    break;
                case 1: /* BAG -- open battle bag */
                    state->bag_context = 1; /* battle */
                    state->mode = PFR_NATIVE_MODE_BATTLE_BAG;
                    state->battle.menu_item_cursor = 0;
                    break;
                case 2: /* POKEMON */
                    battle->menu_state = PFR_BATTLE_MENU_PARTY;
                    battle->menu_party_cursor = 0;
                    break;
                case 3: { /* RUN */
                    int battle_result = pfr_execute_turn(state->party, battle,
                                                          PFR_BATTLE_ACT_RUN,
                                                          &state->rng_value);
                    handle_battle_result(state, battle_result);
                    break;
                }
                }
                break;
            default:
                break;
            }
            break;
        }
        case PFR_BATTLE_MENU_FIGHT: {
            /* Cursor 0-3 in 2x2 grid for 4 moves */
            switch (action) {
            case PFR_NATIVE_ACTION_UP:
                battle->menu_cursor ^= 2;
                break;
            case PFR_NATIVE_ACTION_DOWN:
                battle->menu_cursor ^= 2;
                break;
            case PFR_NATIVE_ACTION_LEFT:
                battle->menu_cursor ^= 1;
                break;
            case PFR_NATIVE_ACTION_RIGHT:
                battle->menu_cursor ^= 1;
                break;
            case PFR_NATIVE_ACTION_A: {
                /* Confirm move if valid (exists + has PP) */
                const PfrPokemon *pl = &state->party[battle->player_slot];
                int slot = battle->menu_cursor;
                uint16_t mid = pl->moves[slot];
                if (mid > 0 && mid < PFR_NUM_MOVES && pl->pp[slot] > 0) {
                    int battle_result = pfr_execute_turn(state->party, battle,
                                                          PFR_BATTLE_ACT_MOVE1 + slot,
                                                          &state->rng_value);
                    handle_battle_result(state, battle_result);
                }
                break;
            }
            case PFR_NATIVE_ACTION_B:
                battle->menu_state = PFR_BATTLE_MENU_MAIN;
                battle->menu_cursor = 0;
                break;
            default:
                break;
            }
            break;
        }
        case PFR_BATTLE_MENU_PARTY: {
            /* party_cursor 0-5 */
            switch (action) {
            case PFR_NATIVE_ACTION_UP: {
                /* Find prev non-empty slot */
                int c = battle->menu_party_cursor;
                for (int i = 0; i < PFR_NATIVE_MAX_PARTY; i++) {
                    c = (c + PFR_NATIVE_MAX_PARTY - 1) % PFR_NATIVE_MAX_PARTY;
                    if (state->party[c].species != 0) {
                        battle->menu_party_cursor = (uint8_t)c;
                        break;
                    }
                }
                break;
            }
            case PFR_NATIVE_ACTION_DOWN: {
                /* Find next non-empty slot */
                int c = battle->menu_party_cursor;
                for (int i = 0; i < PFR_NATIVE_MAX_PARTY; i++) {
                    c = (c + 1) % PFR_NATIVE_MAX_PARTY;
                    if (state->party[c].species != 0) {
                        battle->menu_party_cursor = (uint8_t)c;
                        break;
                    }
                }
                break;
            }
            case PFR_NATIVE_ACTION_A: {
                /* Switch to selected pokemon if valid */
                int sel = battle->menu_party_cursor;
                if (sel != (int)battle->player_slot &&
                    state->party[sel].species != 0 &&
                    state->party[sel].hp > 0) {
                    /* Set player_slot BEFORE calling execute_turn --
                     * the SWITCH handler uses it directly */
                    battle->player_slot = (uint8_t)sel;
                    int battle_result = pfr_execute_turn(state->party, battle,
                                                          PFR_BATTLE_ACT_SWITCH,
                                                          &state->rng_value);
                    handle_battle_result(state, battle_result);
                }
                break;
            }
            case PFR_NATIVE_ACTION_B:
                battle->menu_state = PFR_BATTLE_MENU_MAIN;
                battle->menu_cursor = 0;
                break;
            default:
                break;
            }
            break;
        }
        case PFR_BATTLE_MENU_TURN_RESULT:
            /* Any button press advances to next message or returns to MAIN */
            if (action == PFR_NATIVE_ACTION_A || action == PFR_NATIVE_ACTION_B) {
                battle->msg_page++;
                if (battle->msg_page >= battle->msg_total) {
                    /* Check if player's mon fainted and needs force switch */
                    if (battle->player_fainted) {
                        /* Check if any alive party members remain */
                        int has_alive = 0;
                        for (int i = 0; i < PFR_NATIVE_MAX_PARTY; i++) {
                            if (state->party[i].species != 0 && state->party[i].hp > 0) {
                                has_alive = 1;
                                battle->menu_party_cursor = (uint8_t)i;
                                break;
                            }
                        }
                        if (has_alive) {
                            state->mode = PFR_NATIVE_MODE_FORCE_SWITCH;
                        } else {
                            /* All fainted - battle lost handled by pfr_execute_turn */
                            battle->menu_state = PFR_BATTLE_MENU_MAIN;
                            battle->menu_cursor = 0;
                        }
                    } else {
                        battle->menu_state = PFR_BATTLE_MENU_MAIN;
                        battle->menu_cursor = 0;
                    }
                }
            }
            break;
        default:
            battle->menu_state = PFR_BATTLE_MENU_MAIN;
            battle->menu_cursor = 0;
            break;
        }

        result.mode = state->mode;
        result.changed = 1;
        core->last_step = result;
        return result;
    }

    /* ---- Start menu mode ---- */
    /* Items: 0=POKEDEX, 1=POKeMON, 2=BAG, 3=RED, 4=SAVE, 5=OPTION */
    if (state->mode == PFR_NATIVE_MODE_START_MENU)
    {
        switch (action) {
        case PFR_NATIVE_ACTION_UP:
            if (state->start_menu_cursor > 0)
                state->start_menu_cursor--;
            else
                state->start_menu_cursor = 5;
            break;
        case PFR_NATIVE_ACTION_DOWN:
            if (state->start_menu_cursor < 5)
                state->start_menu_cursor++;
            else
                state->start_menu_cursor = 0;
            break;
        case PFR_NATIVE_ACTION_A:
            switch (state->start_menu_cursor) {
            case 0: /* POKEDEX */
                state->mode = PFR_NATIVE_MODE_POKEDEX;
                break;
            case 1: /* POKeMON -> party view */
                state->mode = PFR_NATIVE_MODE_PARTY_VIEW;
                state->party_view_cursor = 0;
                break;
            case 2: /* BAG */
                state->mode = PFR_NATIVE_MODE_BAG;
                break;
            case 3: /* RED (trainer card) */
                state->mode = PFR_NATIVE_MODE_TRAINER_CARD;
                break;
            case 4: /* SAVE */
                state->mode = PFR_NATIVE_MODE_SAVE_CONFIRM;
                break;
            case 5: /* OPTION */
                state->mode = PFR_NATIVE_MODE_OPTIONS;
                break;
            }
            break;
        case PFR_NATIVE_ACTION_B:
            state->mode = PFR_NATIVE_MODE_OVERWORLD;
            break;
        default:
            break;
        }
        result.mode = state->mode;
        result.changed = 1;
        core->last_step = result;
        return result;
    }

    /* ---- Party view mode ---- */
    if (state->mode == PFR_NATIVE_MODE_PARTY_VIEW)
    {
        switch (action) {
        case PFR_NATIVE_ACTION_UP: {
            int c = state->party_view_cursor;
            for (int i = 0; i < PFR_NATIVE_MAX_PARTY; i++) {
                c = (c + PFR_NATIVE_MAX_PARTY - 1) % PFR_NATIVE_MAX_PARTY;
                if (state->party[c].species != 0) {
                    state->party_view_cursor = (uint8_t)c;
                    break;
                }
            }
            break;
        }
        case PFR_NATIVE_ACTION_DOWN: {
            int c = state->party_view_cursor;
            for (int i = 0; i < PFR_NATIVE_MAX_PARTY; i++) {
                c = (c + 1) % PFR_NATIVE_MAX_PARTY;
                if (state->party[c].species != 0) {
                    state->party_view_cursor = (uint8_t)c;
                    break;
                }
            }
            break;
        }
        case PFR_NATIVE_ACTION_A:
            if (state->party[state->party_view_cursor].species != 0) {
                /* Open sub-menu: SUMMARY / SWITCH / CANCEL */
                state->party_submenu_cursor = 0;
                if (state->party_switch_source != 0xFF) {
                    /* Second tap during switch: do the swap */
                    uint8_t src = state->party_switch_source;
                    uint8_t dst = state->party_view_cursor;
                    if (src != dst && src < PFR_NATIVE_MAX_PARTY
                        && dst < PFR_NATIVE_MAX_PARTY) {
                        PfrPokemon tmp = state->party[src];
                        state->party[src] = state->party[dst];
                        state->party[dst] = tmp;
                    }
                    state->party_switch_source = 0xFF;
                } else {
                    state->mode = PFR_NATIVE_MODE_PARTY_SUBMENU;
                }
            }
            break;
        case PFR_NATIVE_ACTION_B:
            if (state->party_switch_source != 0xFF) {
                state->party_switch_source = 0xFF; /* Cancel switch */
            } else {
                state->mode = PFR_NATIVE_MODE_START_MENU;
            }
            break;
        default:
            break;
        }
        result.mode = state->mode;
        result.changed = 1;
        core->last_step = result;
        return result;
    }

    /* ---- Pokemon summary mode ---- */
    if (state->mode == PFR_NATIVE_MODE_POKEMON_SUMMARY)
    {
        switch (action) {
        case PFR_NATIVE_ACTION_LEFT:
            if (state->summary_page > 0) {
                state->summary_page--;
                state->summary_move_cursor = 0;
                state->summary_move_selected = 0xFF;
            }
            break;
        case PFR_NATIVE_ACTION_RIGHT:
            if (state->summary_page < 1) {
                state->summary_page++;
                state->summary_move_cursor = 0;
                state->summary_move_selected = 0xFF;
            }
            break;
        case PFR_NATIVE_ACTION_UP:
            if (state->summary_page == 1 && state->summary_move_cursor > 0)
                state->summary_move_cursor--;
            break;
        case PFR_NATIVE_ACTION_DOWN:
            if (state->summary_page == 1 && state->summary_move_cursor < 3)
                state->summary_move_cursor++;
            break;
        case PFR_NATIVE_ACTION_A:
            if (state->summary_page == 1) {
                PfrPokemon *mon = &state->party[state->summary_pokemon_idx];
                if (state->summary_move_selected == 0xFF) {
                    /* Select first move for swap */
                    if (mon->moves[state->summary_move_cursor] != 0)
                        state->summary_move_selected = state->summary_move_cursor;
                } else {
                    /* Swap moves */
                    uint8_t src = state->summary_move_selected;
                    uint8_t dst = state->summary_move_cursor;
                    if (src != dst) {
                        uint16_t tmp_move = mon->moves[src];
                        uint8_t tmp_pp = mon->pp[src];
                        mon->moves[src] = mon->moves[dst];
                        mon->pp[src] = mon->pp[dst];
                        mon->moves[dst] = tmp_move;
                        mon->pp[dst] = tmp_pp;
                    }
                    state->summary_move_selected = 0xFF;
                }
            }
            break;
        case PFR_NATIVE_ACTION_B:
            if (state->summary_move_selected != 0xFF) {
                state->summary_move_selected = 0xFF; /* Cancel swap */
            } else {
                state->summary_move_selected = 0xFF;
                if (state->battle.active)
                    state->mode = PFR_NATIVE_MODE_BATTLE;
                else
                    state->mode = PFR_NATIVE_MODE_PARTY_VIEW;
            }
            break;
        default:
            break;
        }
        result.mode = state->mode;
        result.changed = 1;
        core->last_step = result;
        return result;
    }

    /* ---- Party submenu mode (SUMMARY / SWITCH / CANCEL) ---- */
    if (state->mode == PFR_NATIVE_MODE_PARTY_SUBMENU)
    {
        switch (action) {
        case PFR_NATIVE_ACTION_UP:
            if (state->party_submenu_cursor > 0)
                state->party_submenu_cursor--;
            break;
        case PFR_NATIVE_ACTION_DOWN:
            if (state->party_submenu_cursor < 2)
                state->party_submenu_cursor++;
            break;
        case PFR_NATIVE_ACTION_A:
            switch (state->party_submenu_cursor) {
            case 0: /* SUMMARY */
                state->summary_pokemon_idx = state->party_view_cursor;
                state->summary_page = 0;
                state->mode = PFR_NATIVE_MODE_POKEMON_SUMMARY;
                break;
            case 1: /* SWITCH */
                if (state->party_switch_source == 0xFF) {
                    /* First selection: mark source */
                    state->party_switch_source = state->party_view_cursor;
                    state->mode = PFR_NATIVE_MODE_PARTY_VIEW;
                } else {
                    /* Second selection: swap party slots */
                    uint8_t src = state->party_switch_source;
                    uint8_t dst = state->party_view_cursor;
                    if (src != dst && src < PFR_NATIVE_MAX_PARTY
                        && dst < PFR_NATIVE_MAX_PARTY) {
                        PfrPokemon tmp = state->party[src];
                        state->party[src] = state->party[dst];
                        state->party[dst] = tmp;
                    }
                    state->party_switch_source = 0xFF;
                    state->mode = PFR_NATIVE_MODE_PARTY_VIEW;
                }
                break;
            case 2: /* CANCEL */
                state->party_switch_source = 0xFF;
                state->mode = PFR_NATIVE_MODE_PARTY_VIEW;
                break;
            }
            break;
        case PFR_NATIVE_ACTION_B:
            state->party_switch_source = 0xFF;
            state->mode = PFR_NATIVE_MODE_PARTY_VIEW;
            break;
        default:
            break;
        }
        result.mode = state->mode;
        result.changed = 1;
        core->last_step = result;
        return result;
    }

    /* ---- Pokedex mode (scrollable list) ---- */
    if (state->mode == PFR_NATIVE_MODE_POKEDEX)
    {
        /* Navigate through national dex 1-412 */
        switch (action) {
        case PFR_NATIVE_ACTION_UP:
            if (state->pokedex_cursor > 0) state->pokedex_cursor--;
            break;
        case PFR_NATIVE_ACTION_DOWN:
            if (state->pokedex_cursor < 255) state->pokedex_cursor++;
            /* Clamp to max species we can show */
            break;
        case PFR_NATIVE_ACTION_B:
            state->pokedex_cursor = 0;
            state->mode = PFR_NATIVE_MODE_START_MENU;
            break;
        default:
            break;
        }
        result.mode = state->mode;
        result.changed = 1;
        core->last_step = result;
        return result;
    }

    /* ---- Trainer card mode ---- */
    if (state->mode == PFR_NATIVE_MODE_TRAINER_CARD)
    {
        if (action == PFR_NATIVE_ACTION_B || action == PFR_NATIVE_ACTION_A)
            state->mode = PFR_NATIVE_MODE_START_MENU;
        result.mode = state->mode;
        result.changed = 1;
        core->last_step = result;
        return result;
    }

    /* ---- Save confirm mode ---- */
    if (state->mode == PFR_NATIVE_MODE_SAVE_CONFIRM)
    {
        switch (action) {
        case PFR_NATIVE_ACTION_UP:
        case PFR_NATIVE_ACTION_DOWN:
            state->save_cursor ^= 1;
            break;
        case PFR_NATIVE_ACTION_A:
            if (state->save_cursor == 0) {
                /* YES: write state to disk */
                pfr_save_to_file(state, pfr_save_path());
            }
            state->mode = PFR_NATIVE_MODE_OVERWORLD;
            break;
        case PFR_NATIVE_ACTION_B:
            state->mode = PFR_NATIVE_MODE_START_MENU;
            break;
        default:
            break;
        }
        result.mode = state->mode;
        result.changed = 1;
        core->last_step = result;
        return result;
    }

    /* ---- Options mode ---- */
    if (state->mode == PFR_NATIVE_MODE_OPTIONS)
    {
        switch (action) {
        case PFR_NATIVE_ACTION_UP:
            if (state->options_cursor > 0) state->options_cursor--;
            break;
        case PFR_NATIVE_ACTION_DOWN:
            if (state->options_cursor < 3) state->options_cursor++;
            break;
        case PFR_NATIVE_ACTION_LEFT:
        case PFR_NATIVE_ACTION_RIGHT:
            /* Toggle the selected option */
            switch (state->options_cursor) {
            case 0: state->text_speed = (state->text_speed + 1) % 3; break;
            case 1: state->battle_scene ^= 1; break;
            case 2: state->battle_style ^= 1; break;
            case 3: state->sound_mode ^= 1; break;
            }
            break;
        case PFR_NATIVE_ACTION_B:
            state->mode = PFR_NATIVE_MODE_START_MENU;
            break;
        default:
            break;
        }
        result.mode = state->mode;
        result.changed = 1;
        core->last_step = result;
        return result;
    }

    /* ---- Bag mode (full 3-pocket navigation) ---- */
    if (state->mode == PFR_NATIVE_MODE_BAG)
    {
        PfrBag *bag = &state->bag;
        uint8_t *cnt;
        int max;
        (void)bag_pocket_slots(bag, bag->pocket, &cnt, &max);
        uint8_t *cursor = &bag->cursor[bag->pocket];
        switch (action) {
        case PFR_NATIVE_ACTION_LEFT:
            bag->pocket = (uint8_t)((bag->pocket + PFR_BAG_POCKET_COUNT - 1) % PFR_BAG_POCKET_COUNT);
            break;
        case PFR_NATIVE_ACTION_RIGHT:
            bag->pocket = (uint8_t)((bag->pocket + 1) % PFR_BAG_POCKET_COUNT);
            break;
        case PFR_NATIVE_ACTION_UP:
            if (*cursor > 0) (*cursor)--;
            break;
        case PFR_NATIVE_ACTION_DOWN:
            if (*cnt > 0 && *cursor < *cnt - 1) (*cursor)++;
            break;
        case PFR_NATIVE_ACTION_A:
            if (*cnt > 0 && *cursor < *cnt) {
                state->bag_pending_item_idx = *cursor;
                state->bag_submenu_cursor = 0;
                state->bag_context = 0; /* overworld */
                state->mode = PFR_NATIVE_MODE_BAG_SUBMENU;
            }
            break;
        case PFR_NATIVE_ACTION_B:
            state->mode = PFR_NATIVE_MODE_START_MENU;
            break;
        default:
            break;
        }
        result.mode = state->mode;
        result.changed = 1;
        core->last_step = result;
        return result;
    }

    /* ---- Bag sub-menu (USE / TOSS / CANCEL) ---- */
    if (state->mode == PFR_NATIVE_MODE_BAG_SUBMENU)
    {
        switch (action) {
        case PFR_NATIVE_ACTION_UP:
            if (state->bag_submenu_cursor > 0) state->bag_submenu_cursor--;
            break;
        case PFR_NATIVE_ACTION_DOWN:
            if (state->bag_submenu_cursor < 2) state->bag_submenu_cursor++;
            break;
        case PFR_NATIVE_ACTION_A:
            switch (state->bag_submenu_cursor) {
            case 0: { /* USE */
                PfrBag *bag = &state->bag;
                uint8_t *cnt;
                int max;
                PfrBagSlot *slots = bag_pocket_slots(bag, bag->pocket, &cnt, &max);
                if (state->bag_pending_item_idx < *cnt) {
                    uint16_t item_id = slots[state->bag_pending_item_idx].item_id;
                    /* Repels don't need a target */
                    if (item_id == 86 || item_id == 83 || item_id == 84) {
                        use_item_on_pokemon(state, item_id, 0);
                        state->mode = PFR_NATIVE_MODE_BAG;
                    } else {
                        /* Need party target selection */
                        state->bag_use_target = 0;
                        state->mode = PFR_NATIVE_MODE_BAG_USE_TARGET;
                    }
                }
                break;
            }
            case 1: { /* TOSS */
                PfrBag *bag = &state->bag;
                uint8_t *cnt;
                int max;
                PfrBagSlot *slots = bag_pocket_slots(bag, bag->pocket, &cnt, &max);
                if (state->bag_pending_item_idx < *cnt) {
                    uint16_t item_id = slots[state->bag_pending_item_idx].item_id;
                    /* Key items can't be tossed */
                    if (bag->pocket != 1) {
                        bag_remove_item(&state->bag, item_id, 1);
                        /* Adjust cursor if needed */
                        if (bag->cursor[bag->pocket] >= *cnt && *cnt > 0)
                            bag->cursor[bag->pocket] = *cnt - 1;
                    }
                }
                state->mode = PFR_NATIVE_MODE_BAG;
                break;
            }
            case 2: /* CANCEL */
                state->mode = PFR_NATIVE_MODE_BAG;
                break;
            }
            break;
        case PFR_NATIVE_ACTION_B:
            state->mode = PFR_NATIVE_MODE_BAG;
            break;
        default:
            break;
        }
        result.mode = state->mode;
        result.changed = 1;
        core->last_step = result;
        return result;
    }

    /* ---- Bag use target (select party mon to use item on) ---- */
    if (state->mode == PFR_NATIVE_MODE_BAG_USE_TARGET)
    {
        switch (action) {
        case PFR_NATIVE_ACTION_UP: {
            int c = state->bag_use_target;
            for (int i = 0; i < PFR_NATIVE_MAX_PARTY; i++) {
                c = (c + PFR_NATIVE_MAX_PARTY - 1) % PFR_NATIVE_MAX_PARTY;
                if (state->party[c].species != 0) {
                    state->bag_use_target = (uint8_t)c;
                    break;
                }
            }
            break;
        }
        case PFR_NATIVE_ACTION_DOWN: {
            int c = state->bag_use_target;
            for (int i = 0; i < PFR_NATIVE_MAX_PARTY; i++) {
                c = (c + 1) % PFR_NATIVE_MAX_PARTY;
                if (state->party[c].species != 0) {
                    state->bag_use_target = (uint8_t)c;
                    break;
                }
            }
            break;
        }
        case PFR_NATIVE_ACTION_A: {
            PfrBag *bag = &state->bag;
            uint8_t *cnt;
            int max;
            PfrBagSlot *slots = bag_pocket_slots(bag, bag->pocket, &cnt, &max);
            if (state->bag_pending_item_idx < *cnt) {
                uint16_t item_id = slots[state->bag_pending_item_idx].item_id;
                if (use_item_on_pokemon(state, item_id, state->bag_use_target)) {
                    /* Item used successfully — adjust cursor if slot removed */
                    if (bag->cursor[bag->pocket] >= *cnt && *cnt > 0)
                        bag->cursor[bag->pocket] = *cnt - 1;
                }
            }
            state->mode = PFR_NATIVE_MODE_BAG;
            break;
        }
        case PFR_NATIVE_ACTION_B:
            state->mode = PFR_NATIVE_MODE_BAG;
            break;
        default:
            break;
        }
        result.mode = state->mode;
        result.changed = 1;
        core->last_step = result;
        return result;
    }

    /* ---- Shop mode (BUY/SELL/CANCEL → item list → quantity) ---- */
    if (state->mode == PFR_NATIVE_MODE_SHOP)
    {
        PfrShopState *shop = &state->shop;
        if (shop->menu == 0) {
            /* Top menu: BUY(0) / SELL(1) / CANCEL(2) */
            switch (action) {
            case PFR_NATIVE_ACTION_UP:
                if (shop->cursor > 0) shop->cursor--;
                break;
            case PFR_NATIVE_ACTION_DOWN:
                if (shop->cursor < 2) shop->cursor++;
                break;
            case PFR_NATIVE_ACTION_A:
                if (shop->cursor == 0) { shop->menu = 1; shop->cursor = 0; shop->scroll = 0; }
                else if (shop->cursor == 1) { shop->menu = 2; shop->cursor = 0; shop->scroll = 0; }
                else { state->mode = PFR_NATIVE_MODE_OVERWORLD; }
                break;
            case PFR_NATIVE_ACTION_B:
                state->mode = PFR_NATIVE_MODE_OVERWORLD;
                break;
            default: break;
            }
        } else if (shop->menu == 1) {
            /* BUY list */
            switch (action) {
            case PFR_NATIVE_ACTION_UP:
                if (shop->cursor > 0) shop->cursor--;
                break;
            case PFR_NATIVE_ACTION_DOWN:
                if (shop->cursor < shop->inv_count - 1) shop->cursor++;
                break;
            case PFR_NATIVE_ACTION_A: {
                /* Buy 1 of selected item */
                uint16_t item_id = shop->inventory[shop->cursor];
                if (item_id > 0 && item_id < PFR_NUM_ITEMS) {
                    uint16_t price = PFR_ITEMS[item_id].price;
                    if (state->money >= price) {
                        if (bag_add_item(&state->bag, item_id, 1)) {
                            state->money -= price;
                        }
                    }
                }
                break;
            }
            case PFR_NATIVE_ACTION_B:
                shop->menu = 0; shop->cursor = 0;
                break;
            default: break;
            }
        } else if (shop->menu == 2) {
            /* SELL list (items from bag, sell at half price) */
            PfrBag *bag = &state->bag;
            uint8_t total = bag->item_count + bag->ball_count;
            switch (action) {
            case PFR_NATIVE_ACTION_UP:
                if (shop->cursor > 0) shop->cursor--;
                break;
            case PFR_NATIVE_ACTION_DOWN:
                if (total > 0 && shop->cursor < total - 1) shop->cursor++;
                break;
            case PFR_NATIVE_ACTION_A: {
                /* Sell 1 of selected item */
                int idx = shop->cursor;
                uint16_t item_id = 0;
                if (idx < bag->item_count) {
                    item_id = bag->items[idx].item_id;
                } else if (idx - bag->item_count < bag->ball_count) {
                    item_id = bag->balls[idx - bag->item_count].item_id;
                }
                if (item_id > 0 && item_id < PFR_NUM_ITEMS) {
                    uint16_t sell_price = PFR_ITEMS[item_id].price / 2;
                    if (bag_remove_item(bag, item_id, 1)) {
                        state->money += sell_price;
                        total = bag->item_count + bag->ball_count;
                        if (shop->cursor >= total && total > 0)
                            shop->cursor = total - 1;
                    }
                }
                break;
            }
            case PFR_NATIVE_ACTION_B:
                shop->menu = 0; shop->cursor = 0;
                break;
            default: break;
            }
        }
        result.mode = state->mode;
        result.changed = 1;
        core->last_step = result;
        return result;
    }

    /* ---- PC mode (DEPOSIT / WITHDRAW) ---- */
    if (state->mode == PFR_NATIVE_MODE_PC)
    {
        if (state->pc_menu == 0) {
            /* Main: DEPOSIT(0) / WITHDRAW(1) / CANCEL(2) */
            switch (action) {
            case PFR_NATIVE_ACTION_UP:
                if (state->pc_cursor > 0) state->pc_cursor--;
                break;
            case PFR_NATIVE_ACTION_DOWN:
                if (state->pc_cursor < 2) state->pc_cursor++;
                break;
            case PFR_NATIVE_ACTION_A:
                if (state->pc_cursor == 0) {
                    state->pc_menu = 1; /* deposit */
                    state->pc_cursor = 0;
                } else if (state->pc_cursor == 1) {
                    state->pc_menu = 2; /* withdraw */
                    state->pc_cursor = 0;
                    state->pc_box_cursor = 0;
                } else {
                    state->mode = PFR_NATIVE_MODE_OVERWORLD;
                }
                break;
            case PFR_NATIVE_ACTION_B:
                state->mode = PFR_NATIVE_MODE_OVERWORLD;
                break;
            default: break;
            }
        } else if (state->pc_menu == 1) {
            /* Deposit: select party mon to put in box */
            switch (action) {
            case PFR_NATIVE_ACTION_UP: {
                int c = state->pc_cursor;
                for (int i = 0; i < PFR_NATIVE_MAX_PARTY; i++) {
                    c = (c + PFR_NATIVE_MAX_PARTY - 1) % PFR_NATIVE_MAX_PARTY;
                    if (state->party[c].species != 0) { state->pc_cursor = (uint8_t)c; break; }
                }
                break;
            }
            case PFR_NATIVE_ACTION_DOWN: {
                int c = state->pc_cursor;
                for (int i = 0; i < PFR_NATIVE_MAX_PARTY; i++) {
                    c = (c + 1) % PFR_NATIVE_MAX_PARTY;
                    if (state->party[c].species != 0) { state->pc_cursor = (uint8_t)c; break; }
                }
                break;
            }
            case PFR_NATIVE_ACTION_A: {
                /* Deposit selected mon if party has >1 */
                int alive = 0;
                for (int i = 0; i < PFR_NATIVE_MAX_PARTY; i++)
                    if (state->party[i].species != 0) alive++;
                if (alive > 1 && state->pc_box_count < PFR_PC_BOX_SIZE) {
                    int idx = state->pc_cursor;
                    if (state->party[idx].species != 0) {
                        state->pc_box[state->pc_box_count] = state->party[idx];
                        state->pc_box_count++;
                        /* Remove from party by shifting */
                        for (int j = idx; j < PFR_NATIVE_MAX_PARTY - 1; j++)
                            state->party[j] = state->party[j + 1];
                        memset(&state->party[PFR_NATIVE_MAX_PARTY - 1], 0, sizeof(PfrPokemon));
                        sync_party_count(state);
                        if (state->pc_cursor >= state->party_count && state->party_count > 0)
                            state->pc_cursor = state->party_count - 1;
                    }
                }
                break;
            }
            case PFR_NATIVE_ACTION_B:
                state->pc_menu = 0; state->pc_cursor = 0;
                break;
            default: break;
            }
        } else if (state->pc_menu == 2) {
            /* Withdraw: select box mon to add to party */
            switch (action) {
            case PFR_NATIVE_ACTION_UP:
                if (state->pc_box_cursor > 0) state->pc_box_cursor--;
                break;
            case PFR_NATIVE_ACTION_DOWN:
                if (state->pc_box_count > 0 && state->pc_box_cursor < state->pc_box_count - 1)
                    state->pc_box_cursor++;
                break;
            case PFR_NATIVE_ACTION_A: {
                /* Withdraw if party has room */
                if (state->party_count < PFR_NATIVE_MAX_PARTY && state->pc_box_count > 0) {
                    int idx = state->pc_box_cursor;
                    if (idx < state->pc_box_count) {
                        state->party[state->party_count] = state->pc_box[idx];
                        state->party_count++;
                        /* Remove from box by shifting */
                        for (int j = idx; j < state->pc_box_count - 1; j++)
                            state->pc_box[j] = state->pc_box[j + 1];
                        memset(&state->pc_box[state->pc_box_count - 1], 0, sizeof(PfrPokemon));
                        state->pc_box_count--;
                        if (state->pc_box_cursor >= state->pc_box_count && state->pc_box_count > 0)
                            state->pc_box_cursor = state->pc_box_count - 1;
                    }
                }
                break;
            }
            case PFR_NATIVE_ACTION_B:
                state->pc_menu = 0; state->pc_cursor = 0;
                break;
            default: break;
            }
        }
        result.mode = state->mode;
        result.changed = 1;
        core->last_step = result;
        return result;
    }

    /* ---- Force switch mode (after party member faints mid-battle) ---- */
    if (state->mode == PFR_NATIVE_MODE_FORCE_SWITCH)
    {
        PfrBattleState *battle = &state->battle;
        switch (action) {
        case PFR_NATIVE_ACTION_UP: {
            int c = battle->menu_party_cursor;
            for (int i = 0; i < PFR_NATIVE_MAX_PARTY; i++) {
                c = (c + PFR_NATIVE_MAX_PARTY - 1) % PFR_NATIVE_MAX_PARTY;
                if (state->party[c].species != 0 && state->party[c].hp > 0) {
                    battle->menu_party_cursor = (uint8_t)c;
                    break;
                }
            }
            break;
        }
        case PFR_NATIVE_ACTION_DOWN: {
            int c = battle->menu_party_cursor;
            for (int i = 0; i < PFR_NATIVE_MAX_PARTY; i++) {
                c = (c + 1) % PFR_NATIVE_MAX_PARTY;
                if (state->party[c].species != 0 && state->party[c].hp > 0) {
                    battle->menu_party_cursor = (uint8_t)c;
                    break;
                }
            }
            break;
        }
        case PFR_NATIVE_ACTION_A: {
            int sel = battle->menu_party_cursor;
            if (state->party[sel].species != 0 && state->party[sel].hp > 0) {
                battle->player_slot = (uint8_t)sel;
                memset(battle->stat_stages[0], 0, 8);
                state->mode = PFR_NATIVE_MODE_BATTLE;
                battle->menu_state = PFR_BATTLE_MENU_MAIN;
                battle->menu_cursor = 0;
            }
            break;
        }
        default:
            break; /* B does nothing in force switch */
        }
        result.mode = state->mode;
        result.changed = 1;
        core->last_step = result;
        return result;
    }

    /* ---- Battle bag mode (select item to use in battle) ---- */
    if (state->mode == PFR_NATIVE_MODE_BATTLE_BAG)
    {
        PfrBattleState *battle = &state->battle;
        /* Build list of battle-usable items (balls + potions + status heals + X-items) */
        /* Navigate with UP/DOWN, A to use, B to go back */
        PfrBag *bag = &state->bag;
        /* Collect usable items from all pockets into a flat list */
        uint16_t usable_ids[32];
        uint8_t usable_pocket[32]; /* which pocket each came from */
        uint8_t usable_slot[32];   /* slot index in that pocket */
        int usable_count = 0;
        /* Balls pocket */
        for (int i = 0; i < bag->ball_count && usable_count < 32; i++) {
            if (battle->type == 0) { /* Only usable in wild battles */
                usable_ids[usable_count] = bag->balls[i].item_id;
                usable_pocket[usable_count] = 2;
                usable_slot[usable_count] = (uint8_t)i;
                usable_count++;
            }
        }
        /* Items pocket: potions, status heals, X-items */
        for (int i = 0; i < bag->item_count && usable_count < 32; i++) {
            uint16_t id = bag->items[i].item_id;
            /* Potions: 13, 19-22, 26-29, 44 */
            /* Status heals: 14-18, 23 */
            /* Revives: 24-25 */
            /* X-items: 73-79 */
            if ((id >= 13 && id <= 29) || id == 44 || id == 38 ||
                id == 30 || id == 31 || id == 32 || id == 33 ||
                (id >= 73 && id <= 79)) {
                usable_ids[usable_count] = id;
                usable_pocket[usable_count] = 0;
                usable_slot[usable_count] = (uint8_t)i;
                usable_count++;
            }
        }

        switch (action) {
        case PFR_NATIVE_ACTION_UP:
            if (battle->menu_item_cursor > 0) battle->menu_item_cursor--;
            break;
        case PFR_NATIVE_ACTION_DOWN:
            if (usable_count > 0 && battle->menu_item_cursor < usable_count - 1)
                battle->menu_item_cursor++;
            break;
        case PFR_NATIVE_ACTION_A: {
            if (usable_count == 0 || battle->menu_item_cursor >= usable_count) break;
            uint16_t item_id = usable_ids[battle->menu_item_cursor];
            /* Pokeballs: attempt catch */
            if (usable_pocket[battle->menu_item_cursor] == 2) {
                /* Can only throw balls at wild Pokemon */
                if (battle->type == 0) {
                    bag_remove_item(bag, item_id, 1);
                    PfrPokemon *opp = &battle->opponent[battle->opp_slot];
                    int caught = pfr_try_catch_ball(opp, item_id, &state->rng_value);
                    if (caught) {
                        battle->caught = 1;
                        /* Add to party if room, otherwise discard (PC not yet wired in battle) */
                        if (state->party_count < PFR_NATIVE_MAX_PARTY) {
                            state->party[state->party_count] = *opp;
                            state->party_count++;
                        } else if (state->pc_box_count < PFR_PC_BOX_SIZE) {
                            state->pc_box[state->pc_box_count] = *opp;
                            state->pc_box_count++;
                        }
                        pokedex_set_caught(state, opp->species);
                        battle->active = 0;
                        handle_battle_result(state, 4);
                    } else {
                        /* Failed catch: opponent attacks */
                        int battle_result = pfr_execute_turn(state->party, battle,
                                                              PFR_BATTLE_ACT_ITEM,
                                                              &state->rng_value);
                        state->mode = PFR_NATIVE_MODE_BATTLE;
                        handle_battle_result(state, battle_result);
                    }
                }
            } else {
                /* Healing/status items: use on active Pokemon, then opponent attacks */
                int slot = battle->player_slot;
                PfrPokemon *mon = &state->party[slot];
                int used = 0;
                /* Potions */
                if (item_id == 13 || item_id == 22 || item_id == 21 || item_id == 20 ||
                    item_id == 19 || item_id == 26 || item_id == 27 || item_id == 28 ||
                    item_id == 29 || item_id == 44 || item_id == 30 || item_id == 31 ||
                    item_id == 38) {
                    if (mon->hp > 0 && mon->hp < mon->max_hp) {
                        uint8_t eff = PFR_ITEMS[item_id].effect;
                        if (eff == 255) mon->hp = mon->max_hp;
                        else { mon->hp += eff; if (mon->hp > mon->max_hp) mon->hp = mon->max_hp; }
                        if (item_id == 19) mon->status = PFR_STATUS_NONE;
                        used = 1;
                    }
                }
                /* Status heals */
                else if (item_id == 14 && (mon->status & (PFR_STATUS_PSN|PFR_STATUS_TOX))) {
                    mon->status &= (uint8_t)~(PFR_STATUS_PSN|PFR_STATUS_TOX); used = 1;
                } else if (item_id == 15 && (mon->status & PFR_STATUS_BRN)) {
                    mon->status &= (uint8_t)~PFR_STATUS_BRN; used = 1;
                } else if (item_id == 16 && (mon->status & PFR_STATUS_FRZ)) {
                    mon->status &= (uint8_t)~PFR_STATUS_FRZ; used = 1;
                } else if (item_id == 17 && (mon->status & PFR_STATUS_SLP)) {
                    mon->status &= (uint8_t)~PFR_STATUS_SLP; used = 1;
                } else if (item_id == 18 && (mon->status & PFR_STATUS_PAR)) {
                    mon->status &= (uint8_t)~PFR_STATUS_PAR; used = 1;
                } else if ((item_id == 23 || item_id == 32) && mon->status) {
                    mon->status = PFR_STATUS_NONE; used = 1;
                }
                /* Revives */
                else if ((item_id == 24 || item_id == 25 || item_id == 33) && mon->hp == 0) {
                    mon->hp = (item_id == 24) ? mon->max_hp / 2 : mon->max_hp;
                    if (mon->hp == 0) mon->hp = 1;
                    used = 1;
                }
                /* X-items: stat boosts */
                else if (item_id >= 73 && item_id <= 79) {
                    int stat = -1;
                    switch (item_id) {
                    case 73: /* Guard Spec - not a stat stage, skip */ break;
                    case 74: battle->stat_stages[0][PFR_STAT_CRIT]++; used = 1; break;
                    case 75: stat = PFR_STAT_ATK; break;
                    case 76: stat = PFR_STAT_DEF; break;
                    case 77: stat = PFR_STAT_SPE; break;
                    case 78: stat = PFR_STAT_ACC; break;
                    case 79: stat = PFR_STAT_SPA; break;
                    }
                    if (stat >= 0 && battle->stat_stages[0][stat] < 6) {
                        battle->stat_stages[0][stat]++;
                        used = 1;
                    }
                }

                if (used) {
                    bag_remove_item(bag, item_id, 1);
                    /* Opponent attacks after item use */
                    int battle_result = pfr_execute_turn(state->party, battle,
                                                          PFR_BATTLE_ACT_ITEM,
                                                          &state->rng_value);
                    state->mode = PFR_NATIVE_MODE_BATTLE;
                    handle_battle_result(state, battle_result);
                }
            }
            break;
        }
        case PFR_NATIVE_ACTION_B:
            state->mode = PFR_NATIVE_MODE_BATTLE;
            battle->menu_state = PFR_BATTLE_MENU_MAIN;
            battle->menu_cursor = 1; /* Return cursor to BAG */
            break;
        default:
            break;
        }
        (void)usable_pocket;
        (void)usable_slot;
        result.mode = state->mode;
        result.changed = 1;
        core->last_step = result;
        return result;
    }

    if (state->mode == PFR_NATIVE_MODE_DIALOG)
    {
        core->last_step = step_dialog(core, action);
        return core->last_step;
    }

    if (action == PFR_NATIVE_ACTION_NONE)
    {
        core->last_step = result;
        return result;
    }

    switch (action)
    {
    case PFR_NATIVE_ACTION_UP:
    case PFR_NATIVE_ACTION_DOWN:
    case PFR_NATIVE_ACTION_LEFT:
    case PFR_NATIVE_ACTION_RIGHT:
    {
        uint8_t new_direction = PFR_NATIVE_DIR_NONE;
        switch (action)
        {
        case PFR_NATIVE_ACTION_UP:
            new_direction = PFR_NATIVE_DIR_NORTH;
            break;
        case PFR_NATIVE_ACTION_DOWN:
            new_direction = PFR_NATIVE_DIR_SOUTH;
            break;
        case PFR_NATIVE_ACTION_LEFT:
            new_direction = PFR_NATIVE_DIR_WEST;
            break;
        case PFR_NATIVE_ACTION_RIGHT:
            new_direction = PFR_NATIVE_DIR_EAST;
            break;
        default:
            break;
        }

        if (state->player_direction != new_direction)
        {
            state->player_direction = new_direction;
            changed = true;
        }

        map = pfr_native_get_map(state->current_map);
        if (map != NULL)
        {
            const PfrNativeTile *current_tile = tile_at(map, state->player_x, state->player_y);
            if (current_tile != NULL
                && ((arrow_warp_behavior(current_tile->behavior)
                     && arrow_warp_matches_direction(current_tile->behavior, new_direction))
                    || (stair_warp_behavior(current_tile->behavior)
                        && stair_warp_matches_direction(current_tile->behavior, new_direction))))
            {
                result.event = try_warp_at(state, state->player_x, state->player_y, new_direction)
                    ? PFR_NATIVE_EVENT_WARPED
                    : PFR_NATIVE_EVENT_UNSUPPORTED_WARP;
                result.changed = result.event == PFR_NATIVE_EVENT_WARPED || changed ? 1 : 0;
                break;
            }
        }

        move_delta(new_direction, &dx, &dy);
        dest_x = state->player_x + dx;
        dest_y = state->player_y + dy;

        if (map != NULL)
        {
            const PfrNativeTile *front_tile = tile_at(map, dest_x, dest_y);
            const PfrNativeWarp *front_warp = warp_at_coords(map, dest_x, dest_y);

            if (new_direction == PFR_NATIVE_DIR_NORTH
                && front_tile != NULL
                && warp_door_behavior(front_tile->behavior)
                && front_warp != NULL)
            {
                result.event = apply_warp_from(state, front_warp, new_direction)
                    ? PFR_NATIVE_EVENT_WARPED
                    : PFR_NATIVE_EVENT_UNSUPPORTED_WARP;
                result.changed = result.event == PFR_NATIVE_EVENT_WARPED || changed ? 1 : 0;
                break;
            }

            if (front_tile == NULL)
            {
                if (try_connection_move(state, new_direction))
                {
                    const PfrNativeTile *land_tile = tile_at(pfr_native_get_map(state->current_map),
                                                             state->player_x,
                                                             state->player_y);
                    if (land_tile != NULL && post_step_warp_behavior(land_tile->behavior))
                    {
                        result.event = try_warp_at(state, state->player_x, state->player_y,
                                                   new_direction)
                            ? PFR_NATIVE_EVENT_WARPED
                            : PFR_NATIVE_EVENT_UNSUPPORTED_WARP;
                    }
                    else
                    {
                        result.event = PFR_NATIVE_EVENT_MOVED;
                    }
                    result.changed = 1;
                    break;
                }
            }
        }

        /* Ledge jump: if destination tile is a jumpable ledge matching our
         * direction, leap 2 tiles (over the ledge) instead of 1. */
        if (map != NULL)
        {
            const PfrNativeTile *dest_tile = tile_at(map, dest_x, dest_y);
            if (dest_tile != NULL && is_jumpable_ledge(dest_tile->behavior, new_direction))
            {
                int16_t land_x = dest_x + dx;
                int16_t land_y = dest_y + dy;
                const PfrNativeTile *land_tile = tile_at(map, land_x, land_y);
                /* Only jump if landing tile exists and is passable */
                if (land_tile != NULL && land_tile->collision == 0
                    && !object_occupied_on_map(state, map, land_x, land_y))
                {
                    state->player_x = land_x;
                    state->player_y = land_y;
                    result.event = PFR_NATIVE_EVENT_MOVED;
                    result.changed = 1;

                    /* Check for post-step warp on landing tile */
                    if (post_step_warp_behavior(land_tile->behavior))
                    {
                        result.event = try_warp_at(state, land_x, land_y, new_direction)
                            ? PFR_NATIVE_EVENT_WARPED
                            : PFR_NATIVE_EVENT_UNSUPPORTED_WARP;
                    }
                    break;
                }
                else
                {
                    result.event = PFR_NATIVE_EVENT_BLOCKED;
                    result.changed = changed ? 1 : 0;
                    break;
                }
            }
        }

        /* Strength: try to push boulder if movement blocked by one */
        if (movement_blocked(state, new_direction, dest_x, dest_y))
        {
            int boulder_idx = find_object_at(state, dest_x, dest_y);
            if (boulder_idx >= 0
                && state->objects[boulder_idx].graphics_id == PFR_NATIVE_GFX_PUSHABLE_BOULDER
                && pfrn_badge_count(state->flags, PFRN_BADGE_FLAG_START) >= 4
                && pfrn_flag_get(state->flags, PFRN_FLAG_GOT_HM04))
            {
                /* Check if boulder destination is passable */
                int16_t boulder_dest_x = dest_x + dx;
                int16_t boulder_dest_y = dest_y + dy;
                const PfrNativeTile *boulder_dest = tile_at(map, boulder_dest_x, boulder_dest_y);
                int bov = (map != NULL) ? check_metatile_override(map, state,
                                            boulder_dest_x, boulder_dest_y) : -1;
                uint8_t bcol = (bov >= 0) ? (uint8_t)bov
                             : (boulder_dest != NULL ? boulder_dest->collision : 1);
                if (boulder_dest != NULL && bcol == 0
                    && !object_occupied_on_map(state, map, boulder_dest_x, boulder_dest_y))
                {
                    /* Push boulder */
                    state->objects[boulder_idx].x = boulder_dest_x;
                    state->objects[boulder_idx].y = boulder_dest_y;
                    /* Move player into boulder's old position */
                    state->player_x = dest_x;
                    state->player_y = dest_y;
                    result.event = PFR_NATIVE_EVENT_MOVED;
                    result.changed = 1;

                    /* Check post-step warps on landing tile */
                    {
                        const PfrNativeTile *land = tile_at(map, dest_x, dest_y);
                        if (land != NULL && post_step_warp_behavior(land->behavior))
                        {
                            result.event = try_warp_at(state, dest_x, dest_y, new_direction)
                                ? PFR_NATIVE_EVENT_WARPED
                                : PFR_NATIVE_EVENT_UNSUPPORTED_WARP;
                        }
                    }
                    break;
                }
            }
            result.event = PFR_NATIVE_EVENT_BLOCKED;
            result.changed = changed ? 1 : 0;
            break;
        }

        state->player_x = dest_x;
        state->player_y = dest_y;
        changed = true;

        map = pfr_native_get_map(state->current_map);
        {
            const PfrNativeTile *current_tile = tile_at(map, state->player_x, state->player_y);
            if (current_tile != NULL && post_step_warp_behavior(current_tile->behavior))
            {
                result.event = try_warp_at(state, state->player_x, state->player_y, new_direction)
                    ? PFR_NATIVE_EVENT_WARPED
                    : PFR_NATIVE_EVENT_UNSUPPORTED_WARP;
            }
            else
            {
                result.event = PFR_NATIVE_EVENT_MOVED;
            }
        }

        /* Spin/slide tiles: after landing, slide in the tile's direction */
        if (map != NULL)
        {
            const PfrNativeTile *land_tile = tile_at(map, state->player_x, state->player_y);
            if (land_tile != NULL && is_spin_behavior(land_tile->behavior)
                && result.event == PFR_NATIVE_EVENT_MOVED)
            {
                int slide_count = 0;
                uint8_t slide_dir = spin_direction(land_tile->behavior);
                while (slide_count < MAX_SLIDE_STEPS && slide_dir != PFR_NATIVE_DIR_NONE)
                {
                    int16_t sdx, sdy;
                    int16_t next_x, next_y;
                    const PfrNativeTile *next_tile;
                    move_delta(slide_dir, &sdx, &sdy);
                    next_x = state->player_x + sdx;
                    next_y = state->player_y + sdy;
                    if (movement_blocked(state, slide_dir, next_x, next_y))
                        break;
                    state->player_x = next_x;
                    state->player_y = next_y;
                    state->player_direction = slide_dir;
                    slide_count++;

                    /* Check for warp on slide destination */
                    next_tile = tile_at(map, next_x, next_y);
                    if (next_tile != NULL && post_step_warp_behavior(next_tile->behavior))
                    {
                        result.event = try_warp_at(state, next_x, next_y, slide_dir)
                            ? PFR_NATIVE_EVENT_WARPED
                            : PFR_NATIVE_EVENT_UNSUPPORTED_WARP;
                        break;
                    }
                    /* Check if this tile is also a spin tile */
                    if (next_tile != NULL && is_spin_behavior(next_tile->behavior))
                    {
                        slide_dir = spin_direction(next_tile->behavior);
                    }
                    else if (next_tile != NULL && next_tile->behavior == MB_STOP_SPINNING)
                    {
                        break;
                    }
                    else
                    {
                        break;  /* normal tile — stop sliding */
                    }
                }
            }
        }

        result.changed = 1;
        break;
    }
    case PFR_NATIVE_ACTION_A:
        if (try_interaction(state))
        {
            result.mode = state->mode;
            result.event = PFR_NATIVE_EVENT_DIALOG_OPENED;
            result.changed = 1;
        }
        break;
    case PFR_NATIVE_ACTION_START:
        state->mode = PFR_NATIVE_MODE_START_MENU;
        state->start_menu_cursor = 0;
        result.mode = state->mode;
        result.changed = 1;
        break;
    default:
        break;
    }

    /* Check coord events after any movement */
    if (result.event == PFR_NATIVE_EVENT_MOVED || result.event == PFR_NATIVE_EVENT_WARPED)
    {
        if (check_coord_events(state))
        {
            result.mode = state->mode;
            result.changed = 1;
        }
    }

    /* Decrement repel steps on movement */
    if (result.event == PFR_NATIVE_EVENT_MOVED && state->repel_steps > 0)
        state->repel_steps--;

    /* Check wild encounter after movement on grass/cave/water tiles */
    if (result.event == PFR_NATIVE_EVENT_MOVED
        && state->mode == PFR_NATIVE_MODE_OVERWORLD
        && state->party_count > 0
        && state->party[0].species != 0
        && state->repel_steps == 0)  /* Repel suppresses encounters */
    {
        map = pfr_native_get_map(state->current_map);
        if (map != NULL)
        {
            int16_t px = state->player_x;
            int16_t py = state->player_y;
            if (px >= 0 && py >= 0 && px < (int16_t)map->width && py < (int16_t)map->height)
            {
                const PfrNativeTile *tile = &map->tiles[(size_t)py * map->width + px];
                uint16_t species;
                uint8_t level;
                if (pfr_check_encounter(state->current_map, tile->behavior,
                                        &species, &level, &state->rng_value))
                {
                    pfr_init_wild_battle(&state->battle, species, level, &state->rng_value);
                    state->mode = PFR_NATIVE_MODE_BATTLE;
                    result.mode = state->mode;
                    result.changed = 1;
                }
            }
        }
    }

    result.mode = state->mode;
    core->last_step = result;
    return result;
}

void c_close(PfrNativeCore *core)
{
    memset(core, 0, sizeof(*core));
}

void c_save_snapshot(const PfrNativeCore *core, PfrNativeSnapshot *snapshot)
{
    if (snapshot != NULL)
        memcpy(snapshot, &core->state, sizeof(*snapshot));
}

const PfrNativeState *pfr_native_state(const PfrNativeCore *core)
{
    return &core->state;
}

size_t pfr_native_state_size(void)
{
    return sizeof(PfrNativeSnapshot);
}

static void append_substituted(char *dst, size_t dst_size, size_t *cursor,
                               const char *src, const char *player_name)
{
    while (*src != '\0' && *cursor + 1 < dst_size)
    {
        if (strncmp(src, "@PLAYER@", 8) == 0)
        {
            size_t i;
            for (i = 0; player_name[i] != '\0' && *cursor + 1 < dst_size; i++)
                dst[(*cursor)++] = player_name[i];
            src += 8;
            continue;
        }
        dst[(*cursor)++] = *src++;
    }
    dst[*cursor] = '\0';
}

void pfr_native_format_dialog_page(const PfrNativeCore *core, uint8_t dialog_id,
                                   uint8_t page_index, char *buffer,
                                   size_t buffer_size)
{
    const PfrNativeDialog *dialog;
    size_t cursor = 0;

    if (buffer_size == 0)
        return;
    buffer[0] = '\0';

    if (dialog_id >= PFR_NATIVE_DIALOG_COUNT)
        return;
    dialog = &gPfrNativeDialogs[dialog_id];
    if (page_index >= dialog->page_count)
        return;

    append_substituted(buffer, buffer_size, &cursor, dialog->pages[page_index],
                       core->state.player_name);
}

static void put_pixel(uint32_t *rgba, int stride, int x, int y, uint32_t color)
{
    if (x < 0 || y < 0 || x >= PFR_NATIVE_SCREEN_WIDTH || y >= PFR_NATIVE_SCREEN_HEIGHT)
        return;
    rgba[y * stride + x] = color;
}

static void fill_rect(uint32_t *rgba, int stride, int x, int y, int w, int h, uint32_t color)
{
    int yy;
    int xx;
    for (yy = y; yy < y + h; yy++)
    {
        for (xx = x; xx < x + w; xx++)
            put_pixel(rgba, stride, xx, yy, color);
    }
}

static void draw_rect_outline(uint32_t *rgba, int stride, int x, int y, int w, int h,
                              uint32_t color)
{
    int xx;
    int yy;
    for (xx = x; xx < x + w; xx++)
    {
        put_pixel(rgba, stride, xx, y, color);
        put_pixel(rgba, stride, xx, y + h - 1, color);
    }
    for (yy = y; yy < y + h; yy++)
    {
        put_pixel(rgba, stride, x, yy, color);
        put_pixel(rgba, stride, x + w - 1, yy, color);
    }
}

static const uint8_t *glyph_rows(char c)
{
    static const uint8_t sSpace[7]      = {0,0,0,0,0,0,0};
    static const uint8_t sUnknown[7]    = {14,17,2,4,4,0,4};
    static const uint8_t sA[7]          = {14,17,17,31,17,17,17};
    static const uint8_t sB[7]          = {30,17,17,30,17,17,30};
    static const uint8_t sC[7]          = {14,17,16,16,16,17,14};
    static const uint8_t sD[7]          = {30,17,17,17,17,17,30};
    static const uint8_t sE[7]          = {31,16,16,30,16,16,31};
    static const uint8_t sF[7]          = {31,16,16,30,16,16,16};
    static const uint8_t sG[7]          = {14,17,16,16,19,17,15};
    static const uint8_t sH[7]          = {17,17,17,31,17,17,17};
    static const uint8_t sI[7]          = {31,4,4,4,4,4,31};
    static const uint8_t sJ[7]          = {31,2,2,2,18,18,12};
    static const uint8_t sK[7]          = {17,18,20,24,20,18,17};
    static const uint8_t sL[7]          = {16,16,16,16,16,16,31};
    static const uint8_t sM[7]          = {17,27,21,21,17,17,17};
    static const uint8_t sN[7]          = {17,17,25,21,19,17,17};
    static const uint8_t sO[7]          = {14,17,17,17,17,17,14};
    static const uint8_t sP[7]          = {30,17,17,30,16,16,16};
    static const uint8_t sQ[7]          = {14,17,17,17,21,18,13};
    static const uint8_t sR[7]          = {30,17,17,30,20,18,17};
    static const uint8_t sS[7]          = {15,16,16,14,1,1,30};
    static const uint8_t sT[7]          = {31,4,4,4,4,4,4};
    static const uint8_t sU[7]          = {17,17,17,17,17,17,14};
    static const uint8_t sV[7]          = {17,17,17,17,17,10,4};
    static const uint8_t sW[7]          = {17,17,17,21,21,21,10};
    static const uint8_t sX[7]          = {17,17,10,4,10,17,17};
    static const uint8_t sY[7]          = {17,17,10,4,4,4,4};
    static const uint8_t sZ[7]          = {31,1,2,4,8,16,31};
    static const uint8_t s0[7]          = {14,17,19,21,25,17,14};
    static const uint8_t s1[7]          = {4,12,4,4,4,4,14};
    static const uint8_t s2[7]          = {14,17,1,2,4,8,31};
    static const uint8_t s3[7]          = {30,1,1,14,1,1,30};
    static const uint8_t s4[7]          = {2,6,10,18,31,2,2};
    static const uint8_t s5[7]          = {31,16,16,30,1,1,30};
    static const uint8_t s6[7]          = {14,16,16,30,17,17,14};
    static const uint8_t s7[7]          = {31,1,2,4,8,8,8};
    static const uint8_t s8[7]          = {14,17,17,14,17,17,14};
    static const uint8_t s9[7]          = {14,17,17,15,1,1,14};
    static const uint8_t sDash[7]       = {0,0,0,31,0,0,0};
    static const uint8_t sUnderscore[7] = {0,0,0,0,0,0,31};
    static const uint8_t sDot[7]        = {0,0,0,0,0,12,12};
    static const uint8_t sColon[7]      = {0,12,12,0,12,12,0};
    static const uint8_t sComma[7]      = {0,0,0,0,0,12,8};
    static const uint8_t sSlash[7]      = {1,1,2,4,8,16,16};
    static const uint8_t sBang[7]       = {4,4,4,4,4,0,4};
    static const uint8_t sQuestion[7]   = {14,17,1,2,4,0,4};
    static const uint8_t sApostrophe[7] = {4,4,2,0,0,0,0};

    if (c >= 'a' && c <= 'z')
        c = (char)toupper((unsigned char)c);

    switch (c)
    {
    case ' ': return sSpace;
    case 'A': return sA;
    case 'B': return sB;
    case 'C': return sC;
    case 'D': return sD;
    case 'E': return sE;
    case 'F': return sF;
    case 'G': return sG;
    case 'H': return sH;
    case 'I': return sI;
    case 'J': return sJ;
    case 'K': return sK;
    case 'L': return sL;
    case 'M': return sM;
    case 'N': return sN;
    case 'O': return sO;
    case 'P': return sP;
    case 'Q': return sQ;
    case 'R': return sR;
    case 'S': return sS;
    case 'T': return sT;
    case 'U': return sU;
    case 'V': return sV;
    case 'W': return sW;
    case 'X': return sX;
    case 'Y': return sY;
    case 'Z': return sZ;
    case '0': return s0;
    case '1': return s1;
    case '2': return s2;
    case '3': return s3;
    case '4': return s4;
    case '5': return s5;
    case '6': return s6;
    case '7': return s7;
    case '8': return s8;
    case '9': return s9;
    case '-': return sDash;
    case '_': return sUnderscore;
    case '.': return sDot;
    case ':': return sColon;
    case ',': return sComma;
    case '/': return sSlash;
    case '!': return sBang;
    case '?': return sQuestion;
    case '\'': return sApostrophe;
    default: return sUnknown;
    }
}

static void draw_glyph(uint32_t *rgba, int stride, int x, int y, uint32_t color, char c)
{
    const uint8_t *rows = glyph_rows(c);
    int row;
    int col;
    for (row = 0; row < 7; row++)
    {
        for (col = 0; col < 5; col++)
        {
            if (rows[row] & (1 << (4 - col)))
                put_pixel(rgba, stride, x + col, y + row, color);
        }
    }
}

static void draw_text(uint32_t *rgba, int stride, int x, int y, uint32_t color,
                      const char *text)
{
    int cursor_x = x;
    int cursor_y = y;
    size_t i;
    for (i = 0; text[i] != '\0'; i++)
    {
        if (text[i] == '\n')
        {
            cursor_x = x;
            cursor_y += 9;
            continue;
        }
        draw_glyph(rgba, stride, cursor_x, cursor_y, color, text[i]);
        cursor_x += 6;
    }
}

static uint32_t tile_color(const PfrNativeTile *tile)
{
    if (tile->collision != 0)
        return 0xFF4B5563u;
    switch (tile->behavior)
    {
    case MB_LADDER:
    case MB_UP_RIGHT_STAIR_WARP:
    case MB_UP_LEFT_STAIR_WARP:
    case MB_DOWN_RIGHT_STAIR_WARP:
    case MB_DOWN_LEFT_STAIR_WARP:
        return 0xFF0F766Eu;
    case MB_PC:
        return 0xFF166534u;
    case MB_SIGNPOST:
    case MB_BOOKSHELF:
    case MB_DRESSER:
    case MB_KITCHEN:
        return 0xFF92400Eu;
    case MB_TELEVISION:
        return 0xFF1D4ED8u;
    case MB_WINDOW:
        return 0xFF60A5FAu;
    default:
        return 0xFFE7D7B1u;
    }
}

void c_render(const PfrNativeCore *core, uint32_t *rgba, int stride_pixels)
{
    const PfrNativeState *state = &core->state;
    const PfrNativeMap *map = pfr_native_get_map(state->current_map);
    size_t i;
    int map_pixel_width;
    int map_pixel_height;
    int camera_x;
    int camera_y;
    char dialog_buffer[DIALOG_BUFFER_SIZE];
    char hud_buffer[96];

    if (rgba == NULL || map == NULL)
        return;

    fill_rect(rgba, stride_pixels, 0, 0, PFR_NATIVE_SCREEN_WIDTH, PFR_NATIVE_SCREEN_HEIGHT,
              0xFF0F172Au);

    map_pixel_width = (int)map->width * TILE_PIXELS;
    map_pixel_height = (int)map->height * TILE_PIXELS;
    camera_x = state->player_x * TILE_PIXELS + TILE_PIXELS / 2 - PFR_NATIVE_SCREEN_WIDTH / 2;
    camera_y = state->player_y * TILE_PIXELS + TILE_PIXELS / 2 - PFR_NATIVE_SCREEN_HEIGHT / 2;

    if (map_pixel_width <= PFR_NATIVE_SCREEN_WIDTH)
        camera_x = -(PFR_NATIVE_SCREEN_WIDTH - map_pixel_width) / 2;
    else if (camera_x < 0)
        camera_x = 0;
    else if (camera_x > map_pixel_width - PFR_NATIVE_SCREEN_WIDTH)
        camera_x = map_pixel_width - PFR_NATIVE_SCREEN_WIDTH;

    if (map_pixel_height <= PFR_NATIVE_SCREEN_HEIGHT)
        camera_y = -(PFR_NATIVE_SCREEN_HEIGHT - map_pixel_height) / 2;
    else if (camera_y < 0)
        camera_y = 0;
    else if (camera_y > map_pixel_height - PFR_NATIVE_SCREEN_HEIGHT)
        camera_y = map_pixel_height - PFR_NATIVE_SCREEN_HEIGHT;

    for (i = 0; i < map->tile_count; i++)
    {
        int tx = (int)(i % map->width);
        int ty = (int)(i / map->width);
        const PfrNativeTile *tile = &map->tiles[i];
        int px = tx * TILE_PIXELS - camera_x;
        int py = ty * TILE_PIXELS - camera_y;
        fill_rect(rgba, stride_pixels, px, py, TILE_PIXELS, TILE_PIXELS, tile_color(tile));
        draw_rect_outline(rgba, stride_pixels, px, py, TILE_PIXELS, TILE_PIXELS, 0x33222110u);
    }

    for (i = 0; i < state->object_count; i++)
    {
        const PfrNativeObjectState *obj = &state->objects[i];
        int px;
        int py;
        if (!obj->active)
            continue;
        px = obj->x * TILE_PIXELS - camera_x + 3;
        py = obj->y * TILE_PIXELS - camera_y + 3;
        fill_rect(rgba, stride_pixels, px, py, 10, 10, 0xFFFB7185u);
        switch (obj->facing)
        {
        case PFR_NATIVE_DIR_NORTH:
            fill_rect(rgba, stride_pixels, px + 3, py, 4, 2, 0xFFFFFFFFu);
            break;
        case PFR_NATIVE_DIR_SOUTH:
            fill_rect(rgba, stride_pixels, px + 3, py + 8, 4, 2, 0xFFFFFFFFu);
            break;
        case PFR_NATIVE_DIR_WEST:
            fill_rect(rgba, stride_pixels, px, py + 3, 2, 4, 0xFFFFFFFFu);
            break;
        case PFR_NATIVE_DIR_EAST:
            fill_rect(rgba, stride_pixels, px + 8, py + 3, 2, 4, 0xFFFFFFFFu);
            break;
        }
    }

    {
        int px = state->player_x * TILE_PIXELS - camera_x + 3;
        int py = state->player_y * TILE_PIXELS - camera_y + 3;
        fill_rect(rgba, stride_pixels, px, py, 10, 10, 0xFFE11D48u);
        switch (state->player_direction)
        {
        case PFR_NATIVE_DIR_NORTH:
            fill_rect(rgba, stride_pixels, px + 3, py, 4, 2, 0xFFFFFFFFu);
            break;
        case PFR_NATIVE_DIR_SOUTH:
            fill_rect(rgba, stride_pixels, px + 3, py + 8, 4, 2, 0xFFFFFFFFu);
            break;
        case PFR_NATIVE_DIR_WEST:
            fill_rect(rgba, stride_pixels, px, py + 3, 2, 4, 0xFFFFFFFFu);
            break;
        case PFR_NATIVE_DIR_EAST:
            fill_rect(rgba, stride_pixels, px + 8, py + 3, 2, 4, 0xFFFFFFFFu);
            break;
        }
    }

    fill_rect(rgba, stride_pixels, 4, 4, 232, 12, 0xCC111827u);
    snprintf(hud_buffer, sizeof(hud_buffer), "%s  MAP:%u  X:%d Y:%d",
             map->name, (unsigned)state->current_map, state->player_x, state->player_y);
    draw_text(rgba, stride_pixels, 6, 6, 0xFFF8FAFCu, hud_buffer);

    if (state->mode == PFR_NATIVE_MODE_DIALOG)
    {
        pfr_native_format_dialog_page(core, state->active_dialog_id, state->dialog_page_index,
                                      dialog_buffer, sizeof(dialog_buffer));
        fill_rect(rgba, stride_pixels, 8, 104, 224, 48, 0xFFF8F5E9u);
        draw_rect_outline(rgba, stride_pixels, 8, 104, 224, 48, 0xFF7C5E3Au);
        draw_text(rgba, stride_pixels, 16, 112, 0xFF111827u, dialog_buffer);
    }

    /* ---- Battle screen overlay (menu-state aware) ---- */
    if (state->mode == PFR_NATIVE_MODE_BATTLE && state->battle.active)
    {
        const PfrBattleState *bt = &state->battle;
        const PfrPokemon *player = &state->party[bt->player_slot];
        const PfrPokemon *opp = &bt->opponent[bt->opp_slot];
        char line[80];

        /* Battle background */
        fill_rect(rgba, stride_pixels, 0, 0, 240, 160, 0xFF1A2744u);

        /* Header */
        fill_rect(rgba, stride_pixels, 4, 2, 232, 12, 0xCC000000u);
        if (bt->type)
            snprintf(line, sizeof(line), "vs %s", pfr_trainer_name(bt->trainer_id));
        else
            snprintf(line, sizeof(line), "WILD BATTLE");
        draw_text(rgba, stride_pixels, 70, 4, 0xFFFFFF00u, line);

        /* Battle intro messages */
        if (bt->intro_phase > 0 && bt->intro_phase < 3) {
            fill_rect(rgba, stride_pixels, 4, 114, 232, 42, 0xFFF8F5E9u);
            draw_rect_outline(rgba, stride_pixels, 4, 114, 232, 42, 0xFF7C5E3Au);
            const char *o_name = (opp->species > 0 && opp->species < PFR_NUM_SPECIES)
                ? PFR_SPECIES_NAMES[opp->species] : "???";
            const char *p_name = (player->species > 0 && player->species < PFR_NUM_SPECIES)
                ? PFR_SPECIES_NAMES[player->species] : "???";
            if (bt->intro_phase == 1) {
                if (bt->type)
                    snprintf(line, sizeof(line), "%s wants to fight!", pfr_trainer_name(bt->trainer_id));
                else
                    snprintf(line, sizeof(line), "Wild %s appeared!", o_name);
                draw_text(rgba, stride_pixels, 10, 122, 0xFF111827u, line);
            } else {
                snprintf(line, sizeof(line), "Go! %s!", p_name);
                draw_text(rgba, stride_pixels, 10, 122, 0xFF111827u, line);
            }
            draw_text(rgba, stride_pixels, 170, 146, 0xFF999999u, "[A] Next");
        }

        /* HP bars always shown in MAIN, FIGHT, and TURN_RESULT modes */
        if (bt->menu_state == PFR_BATTLE_MENU_MAIN ||
            bt->menu_state == PFR_BATTLE_MENU_FIGHT ||
            bt->menu_state == PFR_BATTLE_MENU_TURN_RESULT) {

            /* Opponent info */
            fill_rect(rgba, stride_pixels, 8, 18, 224, 44, 0xFF2A3A5Cu);
            draw_rect_outline(rgba, stride_pixels, 8, 18, 224, 44, 0xFF4A6A9Cu);
            if (opp->species > 0 && opp->species < PFR_NUM_SPECIES) {
                snprintf(line, sizeof(line), "FOE %s  Lv%u",
                         PFR_SPECIES_NAMES[opp->species], opp->level);
                draw_text(rgba, stride_pixels, 14, 22, 0xFFFFFFFFu, line);
                int hp_pct = opp->max_hp > 0 ? (int)(opp->hp * 100 / opp->max_hp) : 0;
                fill_rect(rgba, stride_pixels, 14, 34, 160, 6, 0xFF333333u);
                int hp_w = hp_pct * 160 / 100;
                uint32_t hp_color = hp_pct > 50 ? 0xFF00CC00u : hp_pct > 20 ? 0xFFCCCC00u : 0xFFCC0000u;
                if (hp_w > 0) fill_rect(rgba, stride_pixels, 14, 34, hp_w, 6, hp_color);
                snprintf(line, sizeof(line), "HP: %u/%u", opp->hp, opp->max_hp);
                draw_text(rgba, stride_pixels, 14, 44, 0xFFCCCCCCu, line);
            }

            /* Player info */
            fill_rect(rgba, stride_pixels, 8, 66, 224, 44, 0xFF2A5C3Au);
            draw_rect_outline(rgba, stride_pixels, 8, 66, 224, 44, 0xFF4A9C6Au);
            if (player->species > 0 && player->species < PFR_NUM_SPECIES) {
                snprintf(line, sizeof(line), "%s  Lv%u",
                         PFR_SPECIES_NAMES[player->species], player->level);
                draw_text(rgba, stride_pixels, 14, 70, 0xFFFFFFFFu, line);
                int hp_pct = player->max_hp > 0 ? (int)(player->hp * 100 / player->max_hp) : 0;
                fill_rect(rgba, stride_pixels, 14, 82, 160, 6, 0xFF333333u);
                int hp_w = hp_pct * 160 / 100;
                uint32_t hp_color = hp_pct > 50 ? 0xFF00CC00u : hp_pct > 20 ? 0xFFCCCC00u : 0xFFCC0000u;
                if (hp_w > 0) fill_rect(rgba, stride_pixels, 14, 82, hp_w, 6, hp_color);
                snprintf(line, sizeof(line), "HP: %u/%u", player->hp, player->max_hp);
                draw_text(rgba, stride_pixels, 14, 92, 0xFFCCCCCCu, line);
            }
        }

        /* Bottom area: depends on menu_state */
        if (bt->menu_state == PFR_BATTLE_MENU_MAIN) {
            /* 2x2 grid: FIGHT / BAG / POKEMON / RUN */
            fill_rect(rgba, stride_pixels, 4, 114, 232, 42, 0xFFF8F5E9u);
            draw_rect_outline(rgba, stride_pixels, 4, 114, 232, 42, 0xFF7C5E3Au);
            const char *labels[4] = {"FIGHT", "BAG", "POKEMON", "RUN"};
            for (int mi = 0; mi < 4; mi++) {
                int mx = (mi % 2) * 116 + 20;
                int my = (mi / 2) * 14 + 118;
                const char *prefix = (mi == (int)bt->menu_cursor) ? ">" : " ";
                snprintf(line, sizeof(line), "%s%s", prefix, labels[mi]);
                uint32_t col = (mi == (int)bt->menu_cursor) ? 0xFFE11D48u : 0xFF111827u;
                draw_text(rgba, stride_pixels, mx, my, col, line);
            }
        }
        else if (bt->menu_state == PFR_BATTLE_MENU_FIGHT) {
            /* 2x2 grid of moves with PP */
            fill_rect(rgba, stride_pixels, 4, 114, 232, 42, 0xFFF8F5E9u);
            draw_rect_outline(rgba, stride_pixels, 4, 114, 232, 42, 0xFF7C5E3Au);
            for (int mi = 0; mi < 4; mi++) {
                uint16_t mid = player->moves[mi];
                int mx = (mi % 2) * 116 + 10;
                int my = (mi / 2) * 14 + 118;
                const char *prefix = (mi == (int)bt->menu_cursor) ? ">" : " ";
                if (mid > 0 && mid < PFR_NUM_MOVES) {
                    snprintf(line, sizeof(line), "%s%.8s %u/%u",
                             prefix, PFR_MOVE_NAMES[mid],
                             player->pp[mi], PFR_MOVES[mid].pp);
                } else {
                    snprintf(line, sizeof(line), "%s---", prefix);
                }
                uint32_t col = (mi == (int)bt->menu_cursor) ? 0xFFE11D48u : 0xFF111827u;
                draw_text(rgba, stride_pixels, mx, my, col, line);
            }
        }
        else if (bt->menu_state == PFR_BATTLE_MENU_PARTY) {
            /* Full-screen party overlay */
            fill_rect(rgba, stride_pixels, 4, 18, 232, 138, 0xFF1A2744u);
            draw_rect_outline(rgba, stride_pixels, 4, 18, 232, 138, 0xFF4A6A9Cu);
            draw_text(rgba, stride_pixels, 80, 22, 0xFFFFFF00u, "PARTY");
            for (int pi = 0; pi < PFR_NATIVE_MAX_PARTY; pi++) {
                int py_slot = 34 + pi * 18;
                const PfrPokemon *pm = &state->party[pi];
                const char *prefix = (pi == (int)bt->menu_party_cursor) ? ">" : " ";
                if (pm->species > 0 && pm->species < PFR_NUM_SPECIES) {
                    int hp_pct = pm->max_hp > 0 ? (int)(pm->hp * 100 / pm->max_hp) : 0;
                    snprintf(line, sizeof(line), "%s%.9s L%u %u/%u",
                             prefix, PFR_SPECIES_NAMES[pm->species],
                             pm->level, pm->hp, pm->max_hp);
                    uint32_t col = (pm->hp == 0) ? 0xFFCC0000u :
                                   (pi == (int)bt->menu_party_cursor) ? 0xFFFFFF00u : 0xFFFFFFFFu;
                    draw_text(rgba, stride_pixels, 12, py_slot, col, line);
                    /* Mini HP bar */
                    fill_rect(rgba, stride_pixels, 160, py_slot + 2, 60, 4, 0xFF333333u);
                    int bar_w = hp_pct * 60 / 100;
                    uint32_t hpc = hp_pct > 50 ? 0xFF00CC00u : hp_pct > 20 ? 0xFFCCCC00u : 0xFFCC0000u;
                    if (bar_w > 0) fill_rect(rgba, stride_pixels, 160, py_slot + 2, bar_w, 4, hpc);
                } else {
                    snprintf(line, sizeof(line), "%s---", prefix);
                    draw_text(rgba, stride_pixels, 12, py_slot, 0xFF666666u, line);
                }
            }
        }
        else if (bt->menu_state == PFR_BATTLE_MENU_TURN_RESULT) {
            /* Message box showing turn results */
            fill_rect(rgba, stride_pixels, 4, 114, 232, 42, 0xFFF8F5E9u);
            draw_rect_outline(rgba, stride_pixels, 4, 114, 232, 42, 0xFF7C5E3Au);

            const char *p_name = (player->species > 0 && player->species < PFR_NUM_SPECIES)
                ? PFR_SPECIES_NAMES[player->species] : "???";
            const char *o_name = (opp->species > 0 && opp->species < PFR_NUM_SPECIES)
                ? PFR_SPECIES_NAMES[opp->species] : "???";

            /* Determine which message to show based on msg_page */
            uint8_t logical_page = bt->msg_page;
            if (logical_page == 0) {
                /* Player's move */
                if (bt->last_player_dmg > 0 && bt->last_player_move > 0
                    && bt->last_player_move < PFR_NUM_MOVES) {
                    snprintf(line, sizeof(line), "%.8s used %.10s!",
                             p_name, PFR_MOVE_NAMES[bt->last_player_move]);
                    draw_text(rgba, stride_pixels, 10, 118, 0xFF111827u, line);
                    snprintf(line, sizeof(line), "Dealt %u damage!", bt->last_player_dmg);
                    draw_text(rgba, stride_pixels, 10, 130, 0xFF111827u, line);
                } else if (bt->last_player_move > 0 && bt->last_player_move < PFR_NUM_MOVES) {
                    snprintf(line, sizeof(line), "%.8s used %.10s!",
                             p_name, PFR_MOVE_NAMES[bt->last_player_move]);
                    draw_text(rgba, stride_pixels, 10, 118, 0xFF111827u, line);
                    draw_text(rgba, stride_pixels, 10, 130, 0xFF666666u, "But it missed...");
                } else {
                    draw_text(rgba, stride_pixels, 10, 118, 0xFF111827u, "...");
                }
            } else if (logical_page == 1) {
                /* Opponent's move */
                if (bt->last_opp_dmg > 0 && bt->last_opp_move > 0
                    && bt->last_opp_move < PFR_NUM_MOVES) {
                    snprintf(line, sizeof(line), "Foe %.8s used %.10s!",
                             o_name, PFR_MOVE_NAMES[bt->last_opp_move]);
                    draw_text(rgba, stride_pixels, 10, 118, 0xFF111827u, line);
                    snprintf(line, sizeof(line), "Dealt %u damage!", bt->last_opp_dmg);
                    draw_text(rgba, stride_pixels, 10, 130, 0xFF111827u, line);
                } else if (bt->last_opp_move > 0 && bt->last_opp_move < PFR_NUM_MOVES) {
                    snprintf(line, sizeof(line), "Foe %.8s used %.10s!",
                             o_name, PFR_MOVE_NAMES[bt->last_opp_move]);
                    draw_text(rgba, stride_pixels, 10, 118, 0xFF111827u, line);
                    draw_text(rgba, stride_pixels, 10, 130, 0xFF666666u, "But it missed...");
                } else {
                    draw_text(rgba, stride_pixels, 10, 118, 0xFF111827u, "...");
                }
            } else {
                /* Effectiveness or crit message */
                int eff_page_used = 0;
                if (bt->last_eff != 10 && bt->last_player_dmg > 0) {
                    if (logical_page == 2) {
                        if (bt->last_eff >= 20)
                            draw_text(rgba, stride_pixels, 10, 122, 0xFF111827u,
                                      "It's super effective!");
                        else if (bt->last_eff == 5)
                            draw_text(rgba, stride_pixels, 10, 122, 0xFF111827u,
                                      "Not very effective...");
                        else if (bt->last_eff == 0)
                            draw_text(rgba, stride_pixels, 10, 122, 0xFF111827u,
                                      "It had no effect!");
                        eff_page_used = 1;
                    }
                }
                if (!eff_page_used && bt->last_was_crit) {
                    draw_text(rgba, stride_pixels, 10, 122, 0xFF111827u,
                              "A critical hit!");
                }
            }
            draw_text(rgba, stride_pixels, 150, 146, 0xFF999999u, "[A] Continue");
        }

        /* Status bar */
        snprintf(line, sizeof(line), "Turn:%u", bt->turn);
        fill_rect(rgba, stride_pixels, 4, 148, 232, 10, 0xCC000000u);
        draw_text(rgba, stride_pixels, 10, 149, 0xFF999999u, line);
    }

    /* ---- Battle bag overlay ---- */
    if (state->mode == PFR_NATIVE_MODE_BATTLE_BAG)
    {
        char line[48];
        const PfrBag *bag = &state->bag;
        const PfrBattleState *bt = &state->battle;

        fill_rect(rgba, stride_pixels, 4, 18, 232, 138, 0xFF1A2744u);
        draw_rect_outline(rgba, stride_pixels, 4, 18, 232, 138, 0xFF4A6A9Cu);
        draw_text(rgba, stride_pixels, 80, 22, 0xFFFFFF00u, "BATTLE BAG");

        /* Build usable items list (same logic as mode handler) */
        int item_row = 0;
        int cursor_row = 0;
        /* Balls */
        for (int i = 0; i < bag->ball_count && item_row < 10; i++) {
            if (bt->type != 0) continue;
            int iy = 36 + item_row * 12;
            int is_cur = (item_row == bt->menu_item_cursor);
            if (is_cur) cursor_row = item_row;
            const char *pfx = is_cur ? ">" : " ";
            snprintf(line, sizeof(line), "%s%-14s x%u", pfx,
                     (bag->balls[i].item_id < PFR_NUM_ITEMS) ? PFR_ITEM_NAMES[bag->balls[i].item_id] : "???",
                     bag->balls[i].count);
            draw_text(rgba, stride_pixels, 10, iy,
                      is_cur ? 0xFFFFFF00u : 0xFFFFFFFFu, line);
            item_row++;
        }
        /* Usable items */
        for (int i = 0; i < bag->item_count && item_row < 10; i++) {
            uint16_t id = bag->items[i].item_id;
            if (!((id >= 13 && id <= 29) || id == 44 || id == 38 ||
                  id == 30 || id == 31 || id == 32 || id == 33 ||
                  (id >= 73 && id <= 79))) continue;
            int iy = 36 + item_row * 12;
            int is_cur = (item_row == bt->menu_item_cursor);
            const char *pfx = is_cur ? ">" : " ";
            snprintf(line, sizeof(line), "%s%-14s x%u", pfx,
                     (id < PFR_NUM_ITEMS) ? PFR_ITEM_NAMES[id] : "???",
                     bag->items[i].count);
            draw_text(rgba, stride_pixels, 10, iy,
                      is_cur ? 0xFFFFFF00u : 0xFFFFFFFFu, line);
            item_row++;
        }
        if (item_row == 0)
            draw_text(rgba, stride_pixels, 60, 70, 0xFF666666u, "No usable items");
        (void)cursor_row;
    }

    /* ---- Start menu overlay (right side) ---- */
    if (state->mode == PFR_NATIVE_MODE_START_MENU)
    {
        char line[32];
        int box_x = 160, box_y = 10, box_w = 74, box_h = 70;
        fill_rect(rgba, stride_pixels, box_x, box_y, box_w, box_h, 0xFFF8F5E9u);
        draw_rect_outline(rgba, stride_pixels, box_x, box_y, box_w, box_h, 0xFF7C5E3Au);
        const char *items[6] = {"POKEDEX", "POKEMON", "BAG", "RED", "SAVE", "OPTION"};
        for (int i = 0; i < 6; i++) {
            int iy = box_y + 4 + i * 10;
            const char *prefix = (i == (int)state->start_menu_cursor) ? ">" : " ";
            snprintf(line, sizeof(line), "%s%s", prefix, items[i]);
            uint32_t col = (i == (int)state->start_menu_cursor) ? 0xFFE11D48u : 0xFF111827u;
            draw_text(rgba, stride_pixels, box_x + 4, iy, col, line);
        }
    }

    /* ---- Party view overlay (overworld) ---- */
    if (state->mode == PFR_NATIVE_MODE_PARTY_VIEW)
    {
        char line[48];
        fill_rect(rgba, stride_pixels, 8, 16, 224, 130, 0xFF1A2744u);
        draw_rect_outline(rgba, stride_pixels, 8, 16, 224, 130, 0xFF4A6A9Cu);
        draw_text(rgba, stride_pixels, 80, 20, 0xFFFFFF00u, "PARTY");
        for (int pi = 0; pi < PFR_NATIVE_MAX_PARTY; pi++) {
            int py_slot = 32 + pi * 18;
            const PfrPokemon *pm = &state->party[pi];
            const char *prefix = (pi == (int)state->party_view_cursor) ? ">" : " ";
            if (pm->species > 0 && pm->species < PFR_NUM_SPECIES) {
                int hp_pct = pm->max_hp > 0 ? (int)(pm->hp * 100 / pm->max_hp) : 0;
                snprintf(line, sizeof(line), "%s%.9s L%u %u/%u",
                         prefix, PFR_SPECIES_NAMES[pm->species],
                         pm->level, pm->hp, pm->max_hp);
                uint32_t col = (pm->hp == 0) ? 0xFFCC0000u :
                               (pi == (int)state->party_view_cursor) ? 0xFFFFFF00u : 0xFFFFFFFFu;
                draw_text(rgba, stride_pixels, 16, py_slot, col, line);
                /* Mini HP bar */
                fill_rect(rgba, stride_pixels, 164, py_slot + 2, 56, 4, 0xFF333333u);
                int bar_w = hp_pct * 56 / 100;
                uint32_t hpc = hp_pct > 50 ? 0xFF00CC00u : hp_pct > 20 ? 0xFFCCCC00u : 0xFFCC0000u;
                if (bar_w > 0) fill_rect(rgba, stride_pixels, 164, py_slot + 2, bar_w, 4, hpc);
            } else {
                snprintf(line, sizeof(line), "%s---", prefix);
                draw_text(rgba, stride_pixels, 16, py_slot, 0xFF666666u, line);
            }
        }
    }

    /* ---- Pokemon Summary overlay ---- */
    if (state->mode == PFR_NATIVE_MODE_POKEMON_SUMMARY)
    {
        static const char *const nature_names[25] = {
            "HARDY","LONELY","BRAVE","ADAMANT","NAUGHTY",
            "BOLD","DOCILE","RELAXED","IMPISH","LAX",
            "TIMID","HASTY","SERIOUS","JOLLY","NAIVE",
            "MODEST","MILD","QUIET","BASHFUL","RASH",
            "CALM","GENTLE","SASSY","CAREFUL","QUIRKY"
        };
        static const char *const type_names[18] = {
            "NORMAL","FIGHT","FLYING","POISON","GROUND",
            "ROCK","BUG","GHOST","STEEL","FIRE",
            "WATER","GRASS","ELECTR","PSYCHC","ICE",
            "DRAGON","DARK","FAIRY"
        };
        char line[48];
        const PfrPokemon *pm = &state->party[state->summary_pokemon_idx];

        fill_rect(rgba, stride_pixels, 4, 4, 232, 152, 0xFF1A2744u);
        draw_rect_outline(rgba, stride_pixels, 4, 4, 232, 152, 0xFF4A6A9Cu);

        if (pm->species > 0 && pm->species < PFR_NUM_SPECIES) {
            const char *sp_name = PFR_SPECIES_NAMES[pm->species];
            const char *nat_name = (pm->nature < 25) ? nature_names[pm->nature] : "???";

            /* Header */
            snprintf(line, sizeof(line), "%s  Lv%u  %s", sp_name, pm->level, nat_name);
            draw_text(rgba, stride_pixels, 10, 8, 0xFFFFFF00u, line);

            if (state->summary_page == 0) {
                /* Stats page */
                draw_text(rgba, stride_pixels, 10, 20, 0xFFCCCCCCu, "-- STATS --");
                snprintf(line, sizeof(line), "HP:  %u / %u", pm->hp, pm->max_hp);
                draw_text(rgba, stride_pixels, 10, 34, 0xFFFFFFFFu, line);
                snprintf(line, sizeof(line), "ATK: %u", pm->stats[PFR_STAT_ATK]);
                draw_text(rgba, stride_pixels, 10, 46, 0xFFFFFFFFu, line);
                snprintf(line, sizeof(line), "DEF: %u", pm->stats[PFR_STAT_DEF]);
                draw_text(rgba, stride_pixels, 10, 58, 0xFFFFFFFFu, line);
                snprintf(line, sizeof(line), "SPA: %u", pm->stats[PFR_STAT_SPA]);
                draw_text(rgba, stride_pixels, 10, 70, 0xFFFFFFFFu, line);
                snprintf(line, sizeof(line), "SPD: %u", pm->stats[PFR_STAT_SPD]);
                draw_text(rgba, stride_pixels, 10, 82, 0xFFFFFFFFu, line);
                snprintf(line, sizeof(line), "SPE: %u", pm->stats[PFR_STAT_SPE]);
                draw_text(rgba, stride_pixels, 10, 94, 0xFFFFFFFFu, line);
                /* Type info */
                const PfrSpeciesData *sp = &PFR_SPECIES[pm->species];
                const char *t1 = (sp->type1 < 18) ? type_names[sp->type1] : "???";
                const char *t2 = (sp->type2 < 18 && sp->type2 != sp->type1) ? type_names[sp->type2] : NULL;
                if (t2)
                    snprintf(line, sizeof(line), "TYPE: %s/%s", t1, t2);
                else
                    snprintf(line, sizeof(line), "TYPE: %s", t1);
                draw_text(rgba, stride_pixels, 10, 110, 0xFF88CCFFu, line);
                if (pm->status) {
                    snprintf(line, sizeof(line), "STATUS: %02X", pm->status);
                    draw_text(rgba, stride_pixels, 10, 122, 0xFFFF8800u, line);
                }
            } else {
                /* Moves page with cursor + reorder */
                draw_text(rgba, stride_pixels, 10, 20, 0xFFCCCCCCu, "-- MOVES --");
                for (int mi = 0; mi < 4; mi++) {
                    uint16_t mid = pm->moves[mi];
                    int my = 34 + mi * 24;
                    int is_cursor = (mi == state->summary_move_cursor);
                    int is_selected = (mi == state->summary_move_selected);
                    const char *prefix = is_cursor ? ">" : " ";
                    if (mid > 0 && mid < PFR_NUM_MOVES) {
                        const PfrMoveData *mv = &PFR_MOVES[mid];
                        const char *tname = (mv->type < 18) ? type_names[mv->type] : "???";
                        snprintf(line, sizeof(line), "%s%s", prefix, PFR_MOVE_NAMES[mid]);
                        uint32_t name_col = is_selected ? 0xFF00FF88u :
                                            is_cursor ? 0xFFFFFF00u : 0xFFFFFFFFu;
                        draw_text(rgba, stride_pixels, 10, my, name_col, line);
                        snprintf(line, sizeof(line), " %s Pw:%u PP:%u/%u",
                                 tname, mv->power, pm->pp[mi], mv->pp);
                        draw_text(rgba, stride_pixels, 10, my + 10, 0xFF999999u, line);
                    } else {
                        snprintf(line, sizeof(line), "%s---", prefix);
                        draw_text(rgba, stride_pixels, 10, my, 0xFF666666u, line);
                    }
                }
                if (state->summary_move_selected != 0xFF)
                    draw_text(rgba, stride_pixels, 10, 132, 0xFF00FF88u, "[A] Swap  [B] Cancel");
                else
                    draw_text(rgba, stride_pixels, 10, 132, 0xFF999999u, "[A] Select  [B] Back");
            }
        } else {
            draw_text(rgba, stride_pixels, 10, 60, 0xFF666666u, "No Pokemon");
        }

        /* Footer */
        const char *page_label = state->summary_page == 0 ? "STATS" : "MOVES";
        snprintf(line, sizeof(line), "[L/R] %s  [B] Back", page_label);
        draw_text(rgba, stride_pixels, 10, 144, 0xFF999999u, line);
    }

    /* ---- Shop overlay ---- */
    if (state->mode == PFR_NATIVE_MODE_SHOP)
    {
        char line[48];
        const PfrShopState *shop = &state->shop;
        fill_rect(rgba, stride_pixels, 4, 4, 232, 152, 0xFF1A2744u);
        draw_rect_outline(rgba, stride_pixels, 4, 4, 232, 152, 0xFF4A6A9Cu);

        if (shop->menu == 0) {
            draw_text(rgba, stride_pixels, 60, 20, 0xFFFFFF00u, "POKE MART");
            snprintf(line, sizeof(line), "Money: $%u", state->money);
            draw_text(rgba, stride_pixels, 60, 36, 0xFF00CC00u, line);
            const char *opts[3] = {"BUY", "SELL", "CANCEL"};
            for (int i = 0; i < 3; i++) {
                const char *pfx = (i == shop->cursor) ? ">" : " ";
                snprintf(line, sizeof(line), "%s%s", pfx, opts[i]);
                uint32_t col = (i == shop->cursor) ? 0xFFFFFF00u : 0xFFFFFFFFu;
                draw_text(rgba, stride_pixels, 60, 56 + i * 14, col, line);
            }
        } else if (shop->menu == 1) {
            /* Buy list */
            draw_text(rgba, stride_pixels, 10, 8, 0xFFFFFF00u, "BUY");
            snprintf(line, sizeof(line), "$%u", state->money);
            draw_text(rgba, stride_pixels, 160, 8, 0xFF00CC00u, line);
            for (int i = 0; i < shop->inv_count && i < 10; i++) {
                uint16_t id = shop->inventory[i];
                int iy = 22 + i * 12;
                const char *pfx = (i == shop->cursor) ? ">" : " ";
                const char *name = (id < PFR_NUM_ITEMS) ? PFR_ITEM_NAMES[id] : "???";
                uint16_t price = (id < PFR_NUM_ITEMS) ? PFR_ITEMS[id].price : 0;
                snprintf(line, sizeof(line), "%s%-14s $%u", pfx, name, price);
                uint32_t col = (i == shop->cursor) ? 0xFFFFFF00u : 0xFFFFFFFFu;
                draw_text(rgba, stride_pixels, 10, iy, col, line);
            }
            draw_text(rgba, stride_pixels, 10, 144, 0xFF999999u, "[A] Buy  [B] Back");
        } else {
            /* Sell list */
            const PfrBag *bag = &state->bag;
            draw_text(rgba, stride_pixels, 10, 8, 0xFFFFFF00u, "SELL");
            snprintf(line, sizeof(line), "$%u", state->money);
            draw_text(rgba, stride_pixels, 160, 8, 0xFF00CC00u, line);
            int row = 0;
            for (int i = 0; i < bag->item_count && row < 8; i++, row++) {
                uint16_t id = bag->items[i].item_id;
                int iy = 22 + row * 12;
                const char *pfx = (row == shop->cursor) ? ">" : " ";
                const char *name = (id < PFR_NUM_ITEMS) ? PFR_ITEM_NAMES[id] : "???";
                uint16_t sell = (id < PFR_NUM_ITEMS) ? PFR_ITEMS[id].price / 2 : 0;
                snprintf(line, sizeof(line), "%s%-12s x%u $%u", pfx, name,
                         bag->items[i].count, sell);
                uint32_t col = (row == shop->cursor) ? 0xFFFFFF00u : 0xFFFFFFFFu;
                draw_text(rgba, stride_pixels, 10, iy, col, line);
            }
            for (int i = 0; i < bag->ball_count && row < 8; i++, row++) {
                uint16_t id = bag->balls[i].item_id;
                int iy = 22 + row * 12;
                const char *pfx = (row == shop->cursor) ? ">" : " ";
                const char *name = (id < PFR_NUM_ITEMS) ? PFR_ITEM_NAMES[id] : "???";
                uint16_t sell = (id < PFR_NUM_ITEMS) ? PFR_ITEMS[id].price / 2 : 0;
                snprintf(line, sizeof(line), "%s%-12s x%u $%u", pfx, name,
                         bag->balls[i].count, sell);
                uint32_t col = (row == shop->cursor) ? 0xFFFFFF00u : 0xFFFFFFFFu;
                draw_text(rgba, stride_pixels, 10, iy, col, line);
            }
            if (row == 0)
                draw_text(rgba, stride_pixels, 60, 70, 0xFF666666u, "Nothing to sell");
            draw_text(rgba, stride_pixels, 10, 144, 0xFF999999u, "[A] Sell  [B] Back");
        }
    }

    /* ---- PC overlay ---- */
    if (state->mode == PFR_NATIVE_MODE_PC)
    {
        char line[48];
        fill_rect(rgba, stride_pixels, 4, 4, 232, 152, 0xFF1A2744u);
        draw_rect_outline(rgba, stride_pixels, 4, 4, 232, 152, 0xFF4A6A9Cu);

        if (state->pc_menu == 0) {
            draw_text(rgba, stride_pixels, 60, 20, 0xFFFFFF00u, "BILL's PC");
            const char *opts[3] = {"DEPOSIT", "WITHDRAW", "CANCEL"};
            for (int i = 0; i < 3; i++) {
                const char *pfx = (i == state->pc_cursor) ? ">" : " ";
                snprintf(line, sizeof(line), "%s%s", pfx, opts[i]);
                uint32_t col = (i == state->pc_cursor) ? 0xFFFFFF00u : 0xFFFFFFFFu;
                draw_text(rgba, stride_pixels, 60, 50 + i * 14, col, line);
            }
            snprintf(line, sizeof(line), "Box: %u/%u  Party: %u/6",
                     state->pc_box_count, PFR_PC_BOX_SIZE, state->party_count);
            draw_text(rgba, stride_pixels, 30, 110, 0xFF999999u, line);
        } else if (state->pc_menu == 1) {
            /* Deposit: party list */
            draw_text(rgba, stride_pixels, 10, 8, 0xFFFFFF00u, "DEPOSIT Pokemon");
            for (int i = 0; i < PFR_NATIVE_MAX_PARTY; i++) {
                const PfrPokemon *pm = &state->party[i];
                int iy = 24 + i * 18;
                const char *pfx = (i == state->pc_cursor) ? ">" : " ";
                if (pm->species > 0 && pm->species < PFR_NUM_SPECIES) {
                    snprintf(line, sizeof(line), "%s%.9s L%u %u/%u",
                             pfx, PFR_SPECIES_NAMES[pm->species], pm->level, pm->hp, pm->max_hp);
                    uint32_t col = (i == state->pc_cursor) ? 0xFFFFFF00u : 0xFFFFFFFFu;
                    draw_text(rgba, stride_pixels, 10, iy, col, line);
                } else {
                    snprintf(line, sizeof(line), "%s---", pfx);
                    draw_text(rgba, stride_pixels, 10, iy, 0xFF666666u, line);
                }
            }
            snprintf(line, sizeof(line), "Box: %u/%u", state->pc_box_count, PFR_PC_BOX_SIZE);
            draw_text(rgba, stride_pixels, 10, 140, 0xFF999999u, line);
        } else {
            /* Withdraw: box list */
            draw_text(rgba, stride_pixels, 10, 8, 0xFFFFFF00u, "WITHDRAW Pokemon");
            for (int i = 0; i < (int)state->pc_box_count && i < 8; i++) {
                const PfrPokemon *pm = &state->pc_box[i];
                int iy = 24 + i * 14;
                const char *pfx = (i == state->pc_box_cursor) ? ">" : " ";
                if (pm->species > 0 && pm->species < PFR_NUM_SPECIES) {
                    snprintf(line, sizeof(line), "%s%.9s L%u %u/%u",
                             pfx, PFR_SPECIES_NAMES[pm->species], pm->level, pm->hp, pm->max_hp);
                    uint32_t col = (i == state->pc_box_cursor) ? 0xFFFFFF00u : 0xFFFFFFFFu;
                    draw_text(rgba, stride_pixels, 10, iy, col, line);
                }
            }
            if (state->pc_box_count == 0)
                draw_text(rgba, stride_pixels, 60, 70, 0xFF666666u, "Box is empty");
            snprintf(line, sizeof(line), "Party: %u/6", state->party_count);
            draw_text(rgba, stride_pixels, 10, 140, 0xFF999999u, line);
        }
    }

    /* ---- Pokedex overlay ---- */
    if (state->mode == PFR_NATIVE_MODE_POKEDEX)
    {
        char line[48];
        fill_rect(rgba, stride_pixels, 4, 4, 232, 152, 0xFF1A2744u);
        draw_rect_outline(rgba, stride_pixels, 4, 4, 232, 152, 0xFF4A6A9Cu);

        int seen_cnt = pokedex_count(state->pokedex_seen);
        int caught_cnt = pokedex_count(state->pokedex_caught);
        snprintf(line, sizeof(line), "POKEDEX  Seen:%d  Caught:%d", seen_cnt, caught_cnt);
        draw_text(rgba, stride_pixels, 10, 8, 0xFFFFFF00u, line);
        fill_rect(rgba, stride_pixels, 6, 18, 228, 1, 0xFF4A6A9Cu);

        /* Show species list starting from scroll position */
        int max_visible = 10;
        int scroll = state->pokedex_cursor > (max_visible - 1) ?
                     state->pokedex_cursor - (max_visible - 1) : 0;
        for (int i = 0; i < max_visible; i++) {
            int dex_num = scroll + i + 1; /* 1-indexed species */
            if (dex_num >= PFR_NUM_SPECIES) break;
            int iy = 22 + i * 12;
            int is_cursor = (scroll + i == state->pokedex_cursor);
            const char *prefix = is_cursor ? ">" : " ";
            int seen = (state->pokedex_seen[(dex_num - 1) / 16] >>
                       ((dex_num - 1) % 16)) & 1;
            int caught = (state->pokedex_caught[(dex_num - 1) / 16] >>
                         ((dex_num - 1) % 16)) & 1;
            const char *status = caught ? "*" : seen ? "o" : " ";
            if (seen) {
                snprintf(line, sizeof(line), "%s%03d %s %.12s", prefix, dex_num,
                         status, PFR_SPECIES_NAMES[dex_num]);
            } else {
                snprintf(line, sizeof(line), "%s%03d %s ----------", prefix, dex_num, status);
            }
            uint32_t col = is_cursor ? 0xFFFFFF00u :
                           caught ? 0xFF00CC00u :
                           seen ? 0xFFCCCCCCu : 0xFF666666u;
            draw_text(rgba, stride_pixels, 10, iy, col, line);
        }
        draw_text(rgba, stride_pixels, 10, 144, 0xFF999999u, "[U/D] Scroll  [B] Back");
    }

    /* ---- Bag overlay ---- */
    if (state->mode == PFR_NATIVE_MODE_BAG ||
        state->mode == PFR_NATIVE_MODE_BAG_SUBMENU ||
        state->mode == PFR_NATIVE_MODE_BAG_USE_TARGET)
    {
        char line[48];
        const PfrBag *bag = &state->bag;
        static const char *const pocket_names[3] = {"ITEMS", "KEY ITEMS", "BALLS"};

        fill_rect(rgba, stride_pixels, 4, 4, 232, 152, 0xFF1A2744u);
        draw_rect_outline(rgba, stride_pixels, 4, 4, 232, 152, 0xFF4A6A9Cu);

        /* Pocket tabs */
        for (int p = 0; p < 3; p++) {
            int tx = 10 + p * 76;
            uint32_t col = (p == bag->pocket) ? 0xFFFFFF00u : 0xFF888888u;
            draw_text(rgba, stride_pixels, tx, 8, col, pocket_names[p]);
        }
        fill_rect(rgba, stride_pixels, 6, 18, 228, 1, 0xFF4A6A9Cu);

        /* Items list */
        const PfrBagSlot *slots;
        uint8_t cnt;
        switch (bag->pocket) {
        case 0: slots = bag->items;     cnt = bag->item_count;     break;
        case 1: slots = bag->key_items; cnt = bag->key_item_count; break;
        case 2: slots = bag->balls;     cnt = bag->ball_count;     break;
        default: slots = bag->items;    cnt = 0;                   break;
        }
        uint8_t cursor_pos = bag->cursor[bag->pocket];
        int max_visible = 10;
        int scroll = 0;
        if (cursor_pos >= max_visible) scroll = cursor_pos - max_visible + 1;

        for (int i = 0; i < max_visible && (i + scroll) < cnt; i++) {
            int idx = i + scroll;
            const PfrBagSlot *slot = &slots[idx];
            int iy = 22 + i * 12;
            const char *prefix = (idx == cursor_pos) ? ">" : " ";
            const char *iname = (slot->item_id < PFR_NUM_ITEMS) ?
                PFR_ITEM_NAMES[slot->item_id] : "???";
            snprintf(line, sizeof(line), "%s%-16s x%u", prefix, iname, slot->count);
            uint32_t col = (idx == cursor_pos) ? 0xFFFFFF00u : 0xFFFFFFFFu;
            draw_text(rgba, stride_pixels, 10, iy, col, line);
        }
        if (cnt == 0) {
            draw_text(rgba, stride_pixels, 60, 70, 0xFF666666u, "No items");
        }

        /* Footer */
        draw_text(rgba, stride_pixels, 10, 144, 0xFF999999u, "[L/R] Pocket [A] Use [B] Back");

        /* Submenu overlay */
        if (state->mode == PFR_NATIVE_MODE_BAG_SUBMENU) {
            int sx = 160, sy = 60;
            fill_rect(rgba, stride_pixels, sx, sy, 64, 40, 0xFFF8F5E9u);
            draw_rect_outline(rgba, stride_pixels, sx, sy, 64, 40, 0xFF7C5E3Au);
            const char *sub_items[3] = {"USE", "TOSS", "CANCEL"};
            for (int i = 0; i < 3; i++) {
                const char *pfx = (i == state->bag_submenu_cursor) ? ">" : " ";
                snprintf(line, sizeof(line), "%s%s", pfx, sub_items[i]);
                uint32_t col = (i == state->bag_submenu_cursor) ? 0xFFE11D48u : 0xFF111827u;
                draw_text(rgba, stride_pixels, sx + 4, sy + 4 + i * 12, col, line);
            }
        }

        /* Use target overlay (party select) */
        if (state->mode == PFR_NATIVE_MODE_BAG_USE_TARGET) {
            fill_rect(rgba, stride_pixels, 8, 20, 224, 120, 0xFF1A2744u);
            draw_rect_outline(rgba, stride_pixels, 8, 20, 224, 120, 0xFF4A6A9Cu);
            draw_text(rgba, stride_pixels, 60, 24, 0xFFFFFF00u, "Use on which Pokemon?");
            for (int pi = 0; pi < PFR_NATIVE_MAX_PARTY; pi++) {
                const PfrPokemon *pm = &state->party[pi];
                int py = 38 + pi * 16;
                const char *prefix = (pi == state->bag_use_target) ? ">" : " ";
                if (pm->species > 0 && pm->species < PFR_NUM_SPECIES) {
                    snprintf(line, sizeof(line), "%s%.9s L%u %u/%u",
                             prefix, PFR_SPECIES_NAMES[pm->species],
                             pm->level, pm->hp, pm->max_hp);
                    uint32_t col = (pm->hp == 0) ? 0xFFCC0000u :
                                   (pi == state->bag_use_target) ? 0xFFFFFF00u : 0xFFFFFFFFu;
                    draw_text(rgba, stride_pixels, 16, py, col, line);
                } else {
                    snprintf(line, sizeof(line), "%s---", prefix);
                    draw_text(rgba, stride_pixels, 16, py, 0xFF666666u, line);
                }
            }
        }
    }
}
