/*
 * pfr_obs_full.c - Comprehensive observation extraction for RL training
 *
 * Extracts rich game state matching pokemonred_puffer's observation set,
 * implemented in pure C for maximum SPS.
 */

/* Include game headers before system headers (GBA macros conflict) */
#include "global.h"
#include "game_ctx.h"
#include "pokemon.h"
#include "event_data.h"
#include "fieldmap.h"
#include "field_player_avatar.h"
#include "battle.h"
#include "item.h"
#include "pokedex.h"
#include "constants/flags.h"
#include "constants/items.h"
#include "constants/maps.h"

#include "pfr_obs_full.h"
#include "pfr_game_api.h"

#include <string.h>

/* Game globals — access via g_ctx for per-instance isolation.
 * g_ctx is declared in game_ctx.h (included above).
 * These macros give us the same names as upstream code. */

extern struct BattlePokemon gBattleMons[];

/* MAP_OFFSET for metatile coordinates */
#ifndef MAP_OFFSET
#define MAP_OFFSET 7
#endif

/*
 * Downsample 240x160 ARGB8888 framebuffer to 72x80 grayscale.
 * Uses area averaging with integer arithmetic for speed.
 * 240/72 ≈ 3.33x horizontal, 160/80 = 2x vertical
 */
static void downsample_screen(const uint32_t *src, uint8_t *dst)
{
    if (!src) {
        memset(dst, 0, PFR_SCREEN_SIZE);
        return;
    }

    /* Use simple nearest-neighbor for max speed in training */
    for (int dy = 0; dy < PFR_SCREEN_H; dy++) {
        int sy = (dy * DISPLAY_HEIGHT) / PFR_SCREEN_H;  /* 160/80 = every 2nd row */
        for (int dx = 0; dx < PFR_SCREEN_W; dx++) {
            int sx = (dx * DISPLAY_WIDTH) / PFR_SCREEN_W;  /* 240/72 ≈ every 3.33rd col */
            uint32_t argb = src[sy * DISPLAY_WIDTH + sx];
            /* ARGB8888 -> grayscale: 0.299R + 0.587G + 0.114B
             * Use integer approx: (77R + 150G + 29B) >> 8 */
            uint8_t r = (argb >> 16) & 0xFF;
            uint8_t g = (argb >> 8) & 0xFF;
            uint8_t b = argb & 0xFF;
            dst[dy * PFR_SCREEN_W + dx] = (uint8_t)((77*r + 150*g + 29*b) >> 8);
        }
    }
}

/*
 * Extract party Pokemon observation for slot i.
 * Reads from the decrypted party struct (gPlayerParty).
 */
static void extract_party_mon(int slot, PfrPartyMonObs *out)
{
    memset(out, 0, sizeof(*out));
    if (slot >= g_ctx->gPlayerPartyCount) return;

    struct Pokemon *mon = &g_ctx->gPlayerParty[slot];

    out->species   = GetMonData(mon, MON_DATA_SPECIES, NULL);
    out->level     = GetMonData(mon, MON_DATA_LEVEL, NULL);
    out->hp        = GetMonData(mon, MON_DATA_HP, NULL);
    out->max_hp    = GetMonData(mon, MON_DATA_MAX_HP, NULL);
    out->attack    = GetMonData(mon, MON_DATA_ATK, NULL);
    out->defense   = GetMonData(mon, MON_DATA_DEF, NULL);
    out->speed     = GetMonData(mon, MON_DATA_SPEED, NULL);
    out->sp_attack = GetMonData(mon, MON_DATA_SPATK, NULL);
    out->sp_defense= GetMonData(mon, MON_DATA_SPDEF, NULL);
    out->status    = GetMonData(mon, MON_DATA_STATUS, NULL);
    out->type1     = gSpeciesInfo[out->species].types[0];
    out->type2     = gSpeciesInfo[out->species].types[1];
    out->moves[0]  = GetMonData(mon, MON_DATA_MOVE1, NULL);
    out->moves[1]  = GetMonData(mon, MON_DATA_MOVE2, NULL);
    out->moves[2]  = GetMonData(mon, MON_DATA_MOVE3, NULL);
    out->moves[3]  = GetMonData(mon, MON_DATA_MOVE4, NULL);
}

/*
 * Count set bits in a byte array.
 */
static uint16_t count_set_bits(const uint8_t *data, int nbytes)
{
    uint16_t count = 0;
    for (int i = 0; i < nbytes; i++) {
        uint8_t b = data[i];
        /* Brian Kernighan's bit counting */
        while (b) { count++; b &= b - 1; }
    }
    return count;
}

/*
 * Check if player has a specific item in any bag pocket.
 */
static uint8_t has_item_in_bag(uint16_t item_id)
{
    /* Check general items */
    for (int i = 0; i < BAG_ITEMS_COUNT; i++) {
        if (gSaveBlock1Ptr->bagPocket_Items[i].itemId == item_id &&
            gSaveBlock1Ptr->bagPocket_Items[i].quantity > 0)
            return 1;
    }
    /* Check key items */
    for (int i = 0; i < BAG_KEYITEMS_COUNT; i++) {
        if (gSaveBlock1Ptr->bagPocket_KeyItems[i].itemId == item_id &&
            gSaveBlock1Ptr->bagPocket_KeyItems[i].quantity > 0)
            return 1;
    }
    /* Check TM/HM pocket */
    for (int i = 0; i < BAG_TMHM_COUNT; i++) {
        if (gSaveBlock1Ptr->bagPocket_TMHM[i].itemId == item_id &&
            gSaveBlock1Ptr->bagPocket_TMHM[i].quantity > 0)
            return 1;
    }
    return 0;
}

/*
 * Count flags set in a range [start, end) by directly reading the flags byte array.
 */
static uint16_t count_flags_in_range(uint16_t start, uint16_t end)
{
    uint16_t count = 0;
    for (uint16_t f = start; f < end; f++) {
        if (FlagGet(f)) count++;
    }
    return count;
}

/*
 * pfr_game_extract_obs_full - Extract comprehensive observation
 *
 * Fills a PfrFullObs struct with complete game state.
 * Call pfr_game_step_frames_exact() or pfr_game_step_frames() first
 * if you need valid screen pixels (fast mode skips rendering).
 */
void pfr_game_extract_obs_full(void *buf)
{
    PfrFullObs *obs = (PfrFullObs *)buf;
    memset(obs, 0, sizeof(PfrFullObs));

    /* 1. Screen pixels (downscaled grayscale) */
    const uint32_t *fb = pfr_game_get_framebuffer();
    downsample_screen(fb, obs->screen);

    /* 2. Player scalars */
    obs->scalars.player_x     = gSaveBlock1Ptr->pos.x;
    obs->scalars.player_y     = gSaveBlock1Ptr->pos.y;
    obs->scalars.map_group    = gSaveBlock1Ptr->location.mapGroup;
    obs->scalars.map_num      = gSaveBlock1Ptr->location.mapNum;
    obs->scalars.map_layout_id= gSaveBlock1Ptr->mapLayoutId;
    obs->scalars.direction    = g_ctx->gObjectEvents[g_ctx->gPlayerAvatar.objectEventId].facingDirection;
    obs->scalars.running_state= g_ctx->gPlayerAvatar.runningState;
    obs->scalars.in_battle    = (g_ctx->gBattleTypeFlags != 0) ? 1 : 0;
    obs->scalars.battle_outcome = g_ctx->gBattleOutcome;
    obs->scalars.weather      = gSaveBlock1Ptr->weather;
    { extern u32 GetMoney(u32 *moneyPtr); obs->scalars.money = GetMoney(&gSaveBlock1Ptr->money); }
    obs->scalars.step_counter = 0; /* filled by env wrapper */

    /* Badges bitmask */
    obs->scalars.badges = 0;
    for (int i = 0; i < PFR_NUM_BADGES; i++) {
        if (FlagGet(FLAG_BADGE01_GET + i))
            obs->scalars.badges |= (1 << i);
    }

    /* 3. Party */
    for (int i = 0; i < PFR_MAX_PARTY; i++) {
        extract_party_mon(i, &obs->party[i]);
    }

    /* 4. Bag items (general pocket, first 20) */
    for (int i = 0; i < PFR_MAX_BAG_ITEMS && i < BAG_ITEMS_COUNT; i++) {
        obs->bag_items[i].item_id  = gSaveBlock1Ptr->bagPocket_Items[i].itemId;
        obs->bag_items[i].quantity = gSaveBlock1Ptr->bagPocket_Items[i].quantity;
    }

    /* 5. Key items (30 slots) */
    for (int i = 0; i < PFR_MAX_KEY_ITEMS && i < BAG_KEYITEMS_COUNT; i++) {
        obs->key_items[i] = gSaveBlock1Ptr->bagPocket_KeyItems[i].itemId;
    }

    /* 6. Event flags (full 288-byte array directly from SaveBlock1) */
    memcpy(obs->event_flags, gSaveBlock1Ptr->flags, PFR_NUM_FLAG_BYTES);

    /* 7. Pokedex seen + owned */
    memcpy(obs->pokedex_seen, gSaveBlock1Ptr->seen1, PFR_DEX_FLAG_BYTES);
    memcpy(obs->pokedex_owned, gSaveBlock2Ptr->pokedex.owned, PFR_DEX_FLAG_BYTES);

    /* 8. NPCs (all 16 ObjectEvents) */
    int16_t px = gSaveBlock1Ptr->pos.x;
    int16_t py = gSaveBlock1Ptr->pos.y;
    for (int i = 0; i < PFR_MAX_NPCS_FULL; i++) {
        struct ObjectEvent *oe = &g_ctx->gObjectEvents[i];
        obs->npcs[i].active          = oe->active;
        obs->npcs[i].graphics_id     = oe->graphicsId;
        obs->npcs[i].x               = oe->currentCoords.x - px;
        obs->npcs[i].y               = oe->currentCoords.y - py;
        obs->npcs[i].facing_direction = oe->facingDirection;
        obs->npcs[i].movement_type   = oe->movementType;
    }

    /* 9. Tile grid (9x9 metatile behaviors centered on player) */
    int cx = px + MAP_OFFSET;
    int cy = py + MAP_OFFSET;
    int idx = 0;
    for (int dy = -PFR_TILE_RADIUS; dy <= PFR_TILE_RADIUS; dy++) {
        for (int dx = -PFR_TILE_RADIUS; dx <= PFR_TILE_RADIUS; dx++) {
            obs->tile_grid[idx++] = MapGridGetMetatileBehaviorAt(cx + dx, cy + dy);
        }
    }
}

/*
 * pfr_game_get_reward_info_full - Extract comprehensive reward info
 */
void pfr_game_get_reward_info_full(PfrRewardInfoFull *info)
{
    memset(info, 0, sizeof(*info));

    /* Basic info */
    info->player_x       = gSaveBlock1Ptr->pos.x;
    info->player_y       = gSaveBlock1Ptr->pos.y;
    info->map_group      = gSaveBlock1Ptr->location.mapGroup;
    info->map_num        = gSaveBlock1Ptr->location.mapNum;
    info->party_count    = g_ctx->gPlayerPartyCount;
    { extern u32 GetMoney(u32 *moneyPtr); info->money = GetMoney(&gSaveBlock1Ptr->money); }
    info->in_battle      = (g_ctx->gBattleTypeFlags != 0) ? 1 : 0;

    /* Badges */
    info->badges = 0;
    for (int i = 0; i < 8; i++) {
        if (FlagGet(FLAG_BADGE01_GET + i))
            info->badges |= (1 << i);
    }

    /* Party level sum + HP avg */
    uint32_t level_sum = 0;
    uint32_t hp_pct_sum = 0;
    for (int i = 0; i < g_ctx->gPlayerPartyCount && i < 6; i++) {
        level_sum += GetMonData(&g_ctx->gPlayerParty[i], MON_DATA_LEVEL, NULL);
        uint16_t hp = GetMonData(&g_ctx->gPlayerParty[i], MON_DATA_HP, NULL);
        uint16_t maxhp = GetMonData(&g_ctx->gPlayerParty[i], MON_DATA_MAX_HP, NULL);
        if (maxhp > 0) hp_pct_sum += (hp * 100) / maxhp;
    }
    info->party_level_sum = (uint16_t)level_sum;
    info->party_hp_sum_pct = (g_ctx->gPlayerPartyCount > 0)
        ? (uint8_t)(hp_pct_sum / g_ctx->gPlayerPartyCount)
        : 0;

    /* Pokedex counts */
    info->pokedex_seen_count  = count_set_bits(gSaveBlock1Ptr->seen1, PFR_DEX_FLAG_BYTES);
    info->pokedex_owned_count = count_set_bits(gSaveBlock2Ptr->pokedex.owned, PFR_DEX_FLAG_BYTES);

    /* Event flag counts by range */
    info->story_flags_set   = count_flags_in_range(0x230, 0x4B0);
    info->trainer_flags_set = count_flags_in_range(0x500, 0x800);
    info->system_flags_set  = count_flags_in_range(0x800, 0x900);

    /* Key items */
    info->has_hm01        = has_item_in_bag(ITEM_HM01);
    info->has_hm02        = has_item_in_bag(ITEM_HM02);
    info->has_hm03        = has_item_in_bag(ITEM_HM03);
    info->has_hm04        = has_item_in_bag(ITEM_HM04);
    info->has_hm05        = has_item_in_bag(ITEM_HM05);
    info->has_ss_ticket   = has_item_in_bag(ITEM_SS_TICKET);
    info->has_silph_scope = has_item_in_bag(ITEM_SILPH_SCOPE);
    info->has_poke_flute  = has_item_in_bag(ITEM_POKE_FLUTE);
    info->has_card_key    = has_item_in_bag(ITEM_CARD_KEY);
    info->has_lift_key    = has_item_in_bag(ITEM_LIFT_KEY);
    info->has_gold_teeth  = has_item_in_bag(ITEM_GOLD_TEETH);
    info->has_bicycle     = has_item_in_bag(ITEM_BICYCLE);
    info->has_tea         = has_item_in_bag(ITEM_TEA);
    info->has_secret_key  = has_item_in_bag(ITEM_SECRET_KEY);

    /* Gym leaders defeated */
    info->defeated_brock    = FlagGet(FLAG_DEFEATED_BROCK);
    info->defeated_misty    = FlagGet(FLAG_DEFEATED_MISTY);
    info->defeated_surge    = FlagGet(FLAG_DEFEATED_LT_SURGE);
    info->defeated_erika    = FlagGet(FLAG_DEFEATED_ERIKA);
    info->defeated_koga     = FlagGet(FLAG_DEFEATED_KOGA);
    info->defeated_sabrina  = FlagGet(FLAG_DEFEATED_SABRINA);
    info->defeated_blaine   = FlagGet(FLAG_DEFEATED_BLAINE);
    info->defeated_giovanni = FlagGet(FLAG_DEFEATED_LEADER_GIOVANNI);
    info->defeated_e4_lorelei = FlagGet(FLAG_DEFEATED_LORELEI);
    info->defeated_e4_bruno   = FlagGet(FLAG_DEFEATED_BRUNO);
    info->defeated_e4_agatha  = FlagGet(FLAG_DEFEATED_AGATHA);
    info->defeated_e4_lance   = FlagGet(FLAG_DEFEATED_LANCE);
    info->defeated_champion   = FlagGet(FLAG_DEFEATED_CHAMP);
    info->game_clear          = FlagGet(FLAG_SYS_GAME_CLEAR);
}
