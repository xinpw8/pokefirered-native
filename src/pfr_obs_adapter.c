/*
 * pfr_obs_adapter.c -- Adapter implementation: extract observations in
 * pfr_native's 176-byte format from the hosted runtime's game state.
 *
 * See pfr_obs_adapter.h for layout documentation and known gaps.
 *
 * NOTE: This file follows the same include pattern as pfr_game_api.c.
 * Game headers must come before system headers because the GBA codebase
 * redefines malloc/calloc/abs. Do NOT include <stdlib.h>.
 */

#include "pfr_obs_adapter.h"
#include "pfr_game_api.h"
#include "pfr_env.h"

/* Upstream game headers (same order as pfr_game_api.c) */
#include "global.h"
#include "gba/gba.h"
#include "main.h"
#include "pokemon.h"
#include "event_data.h"
#include "constants/pokemon.h"
#include "constants/flags.h"
#include "global.fieldmap.h"
#include "fieldmap.h"
#include "game_ctx.h"
#include "battle.h"

/* System headers AFTER game headers to avoid macro conflicts */
#include <string.h>

/* ---- pfr_native observation layout constants (from pfr_native_env.h) ---- */

#define OBS_SCALAR_SIZE      12
#define OBS_TILE_RADIUS      4
#define OBS_TILE_DIM         (2 * OBS_TILE_RADIUS + 1)   /* 9 */
#define OBS_TILE_SIZE        (OBS_TILE_DIM * OBS_TILE_DIM) /* 81 */
#define OBS_NPC_COUNT        8
#define OBS_NPC_FEAT         5
#define OBS_NPC_SIZE         (OBS_NPC_COUNT * OBS_NPC_FEAT) /* 40 */
#define OBS_GLOBAL_POS_SIZE  4
#define OBS_BATTLE_SIZE      39

/* Byte offsets into the 176-byte buffer */
#define OFF_SCALARS     0
#define OFF_TILES       OBS_SCALAR_SIZE                           /* 12 */
#define OFF_NPCS        (OFF_TILES + OBS_TILE_SIZE)               /* 93 */
#define OFF_GLOBAL_POS  (OFF_NPCS + OBS_NPC_SIZE)                 /* 133 */
#define OFF_BATTLE      (OFF_GLOBAL_POS + OBS_GLOBAL_POS_SIZE)    /* 137 */

/* ---- popcount for badge bitmask ---- */

static uint8_t popcount8(uint8_t v)
{
    uint8_t c = 0;
    while (v) { c += v & 1; v >>= 1; }
    return c;
}

/* ---- Main extraction ---- */

void pfr_game_extract_obs_native_format(unsigned char *buf)
{
    int i, dx, dy, idx;
    int16_t px, py, map_x, map_y;
    struct ObjectEvent *playerObj;
    uint8_t badge_bits;

    memset(buf, 0, PFR_NATIVE_OBS_SIZE);

    /* ================================================================
     * (a) Scalars [0:12]
     * ================================================================ */

    /* player_x (2 bytes LE) */
    px = gSaveBlock1Ptr->pos.x;
    buf[0] = (unsigned char)(px & 0xFF);
    buf[1] = (unsigned char)((px >> 8) & 0xFF);

    /* player_y (2 bytes LE) */
    py = gSaveBlock1Ptr->pos.y;
    buf[2] = (unsigned char)(py & 0xFF);
    buf[3] = (unsigned char)((py >> 8) & 0xFF);

    /* map_id (2 bytes LE)
     * APPROXIMATION: pfr_native uses PfrNativeMapId enum.
     * Here we encode (map_group * 256 + map_num) as a uint16.
     * A proper mapping table is needed for full compatibility. */
    {
        uint16_t map_id = (uint16_t)gSaveBlock1Ptr->location.mapGroup * 256u
                        + (uint16_t)gSaveBlock1Ptr->location.mapNum;
        buf[4] = (unsigned char)(map_id & 0xFF);
        buf[5] = (unsigned char)((map_id >> 8) & 0xFF);
    }

    /* direction (1 byte) */
    playerObj = &g_ctx->gObjectEvents[g_ctx->gPlayerAvatar.objectEventId];
    buf[6] = playerObj->facingDirection & 0x0F;

    /* mode (1 byte)
     * APPROXIMATION: only detect overworld vs battle.
     * pfr_native has 19 modes (PFR_NATIVE_MODE_*). Full detection requires
     * inspecting gMain.callback2 function pointers and menu state machines.
     * For now: 0 = overworld (default), 2 = battle. */
    buf[7] = gMain.inBattle ? 2 : 0;

    /* badge_count (1 byte) */
    badge_bits = 0;
    for (i = 0; i < 8; i++) {
        if (FlagGet(FLAG_BADGE01_GET + i))
            badge_bits |= (1u << i);
    }
    buf[8] = popcount8(badge_bits);

    /* party_count (1 byte) */
    buf[9] = (unsigned char)gSaveBlock1Ptr->playerPartyCount;

    /* flags_lo, flags_hi (2 bytes)
     * APPROXIMATION: set to 0. pfr_native uses a 64-bit progression flag
     * bitfield with its own PfrNativeFlag IDs. Mapping GBA flags to
     * pfr_native's flag system is needed for full policy compatibility. */
    buf[10] = 0;
    buf[11] = 0;

    /* ================================================================
     * (b) Tile grid [12:93] -- 9x9 metatile behaviors
     * ================================================================
     * APPROXIMATION: The raw metatile behavior byte from
     * MapGridGetMetatileBehaviorAt() differs from pfr_native's custom tile
     * vocabulary. pfr_native encodes tiles as:
     *   (tile->behavior & 0x7F) | (tile->collision ? 0x80 : 0)
     * The GBA engine returns the full metatile behavior byte which uses
     * different bit assignments. A mapping LUT would improve transfer
     * fidelity, but the raw byte is a reasonable first approximation. */
    map_x = px + MAP_OFFSET;
    map_y = py + MAP_OFFSET;

    for (dy = -OBS_TILE_RADIUS; dy <= OBS_TILE_RADIUS; dy++) {
        for (dx = -OBS_TILE_RADIUS; dx <= OBS_TILE_RADIUS; dx++) {
            idx = (dy + OBS_TILE_RADIUS) * OBS_TILE_DIM + (dx + OBS_TILE_RADIUS);
            {
                u32 behavior = MapGridGetMetatileBehaviorAt(
                    map_x + dx, map_y + dy);
                buf[OFF_TILES + idx] = (unsigned char)(behavior & 0xFF);
            }
        }
    }

    /* ================================================================
     * (c) NPCs [93:133] -- 8 nearest NPCs x 5 bytes each
     * ================================================================
     * Layout per NPC: dx(1), dy(1), graphics_id(1), facing(1), active(1)
     *
     * Uses simple selection for top-8 nearest to avoid qsort (which requires
     * <stdlib.h>, conflicting with the GBA codebase's malloc redefinition).
     *
     * NOTE: graphics_id values from the GBA engine may not match pfr_native's
     * NPC atlas IDs. pfr_native uses its own npc_atlas.h mapping. */
    {
        /* Candidate buffer: obj index + distance^2 for all active NPCs */
        int cand_idx[16];
        int cand_dist[16];
        int n_cand = 0;
        unsigned char *npc_out = buf + OFF_NPCS;

        /* Collect all active non-player NPCs */
        for (i = 0; i < OBJECT_EVENTS_COUNT; i++) {
            struct ObjectEvent *obj = &g_ctx->gObjectEvents[i];
            int odx, ody;
            if (!obj->active || obj->isPlayer)
                continue;
            odx = obj->currentCoords.x - px;
            ody = obj->currentCoords.y - py;
            cand_idx[n_cand] = i;
            cand_dist[n_cand] = odx * odx + ody * ody;
            n_cand++;
        }

        /* Selection sort: pick 8 nearest */
        {
            int picked = 0;
            int used[16];
            memset(used, 0, sizeof(used));

            while (picked < OBS_NPC_COUNT && picked < n_cand) {
                int best = -1;
                int best_d = 0x7FFFFFFF;
                int j;

                for (j = 0; j < n_cand; j++) {
                    if (!used[j] && cand_dist[j] < best_d) {
                        best = j;
                        best_d = cand_dist[j];
                    }
                }
                if (best < 0) break;

                used[best] = 1;
                {
                    struct ObjectEvent *obj = &g_ctx->gObjectEvents[cand_idx[best]];
                    int odx = obj->currentCoords.x - px;
                    int ody = obj->currentCoords.y - py;

                    /* Clamp to [-127, 127] for int8 representation */
                    if (odx < -127) odx = -127;
                    if (odx > 127) odx = 127;
                    if (ody < -127) ody = -127;
                    if (ody > 127) ody = 127;

                    npc_out[picked * OBS_NPC_FEAT + 0] = (unsigned char)(int8_t)odx;
                    npc_out[picked * OBS_NPC_FEAT + 1] = (unsigned char)(int8_t)ody;
                    npc_out[picked * OBS_NPC_FEAT + 2] = obj->graphicsId;
                    npc_out[picked * OBS_NPC_FEAT + 3] = obj->facingDirection & 0x0F;
                    npc_out[picked * OBS_NPC_FEAT + 4] = 1;  /* active */
                }
                picked++;
            }
        }
        /* Remaining slots already zeroed by memset */
    }

    /* ================================================================
     * (d) Global position [133:137] -- gx(2), gy(2)
     * ================================================================
     * APPROXIMATION: Uses raw player_x/y. pfr_native uses a heatmap LUT
     * (pfr_heatmap_lut.h) to convert (map_id, local_x, local_y) into a
     * global coordinate space. The LUT is generated offline from pfr_native's
     * map database and accounts for map connections. Without it, indoor maps
     * and disconnected areas will have overlapping global coords. */
    {
        unsigned char *gpos = buf + OFF_GLOBAL_POS;
        gpos[0] = (unsigned char)(px & 0xFF);
        gpos[1] = (unsigned char)((px >> 8) & 0xFF);
        gpos[2] = (unsigned char)(py & 0xFF);
        gpos[3] = (unsigned char)((py >> 8) & 0xFF);
    }

    /* ================================================================
     * (e) Battle obs [137:176] -- 39 bytes
     * ================================================================
     * APPROXIMATION: Zero-filled for now. Fully populating battle observations
     * requires mapping the hosted runtime's BattlePokemon structs and move data
     * to pfr_native's observation encoding. The mapping involves:
     *
     * - bobs[0]:  battle_active (1 if in battle)
     * - bobs[1]:  player type1 (from gBattleMons[0].type1)
     * - bobs[2]:  player type2 (from gBattleMons[0].type2)
     * - bobs[3]:  player hp_pct (hp * 255 / maxHP)
     * - bobs[4]:  player level
     * - bobs[5..8]:  move types (4 moves -- needs pfr_native PFR_MOVES table)
     * - bobs[9..12]: move powers / 2 (4 moves -- needs pfr_native PFR_MOVES table)
     * - bobs[13..16]: move PP (4 moves, from gBattleMons[0].pp[])
     * - bobs[17]: player status (gBattleMons[0].status2 low byte)
     * - bobs[18]: opponent type1 (gBattleMons[1].type1)
     * - bobs[19]: opponent type2 (gBattleMons[1].type2)
     * - bobs[20]: opponent hp_pct
     * - bobs[21]: opponent level
     * - bobs[22]: opponent status
     * - bobs[23..27]: party HP% (slots 1-5)
     * - bobs[28..32]: party alive flags (slots 1-5)
     * - bobs[33]: weather
     * - bobs[34]: turn
     * - bobs[35]: menu_state
     * - bobs[36]: menu_cursor
     * - bobs[37]: menu_party_cursor
     * - bobs[38]: reserved (0)
     *
     * The GBA engine has all this data in g_ctx->gBattleMons[0..3] and
     * g_ctx->gPlayerParty[0..5], but the move type/power lookup requires
     * pfr_native's PFR_MOVES table which isn't available in the hosted
     * runtime. A shared move data table or runtime lookup is needed.
     *
     * For overworld-only eval, zero-fill is correct behavior since
     * pfr_native also zeros battle obs when not in battle.
     */
    /* Already zeroed by memset -- nothing to do */
}
