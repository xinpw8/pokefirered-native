/*
 * pfr_obs_adapter.h -- Adapter: extract observations in pfr_native's 176-byte
 * format from the hosted runtime's game state.
 *
 * Enables a policy trained on pfr_native (fast, >2M SPS) to run on the hosted
 * runtime (exact GBA rendering) without retraining.
 *
 * pfr_native observation layout (176 bytes):
 *   [0:12]    scalars: player_x(2), player_y(2), map_id(2), direction(1),
 *             mode(1), badge_count(1), party_count(1), flags_lo(1), flags_hi(1)
 *   [12:93]   9x9 tile grid: behavior | (collision << 7)
 *   [93:133]  8 NPCs x 5 bytes: dx, dy, graphics_id, facing, active
 *   [133:137] global position: gx(2), gy(2)
 *   [137:176] battle obs (39 bytes)
 */

#ifndef PFR_OBS_ADAPTER_H
#define PFR_OBS_ADAPTER_H

#include <stdint.h>

/* Total size matching pfr_native_env.h PFR_OBS_SIZE */
#define PFR_NATIVE_OBS_SIZE 176

/*
 * Extract observations from the hosted runtime's live game state,
 * formatted in pfr_native's 176-byte layout.
 *
 * buf must point to at least PFR_NATIVE_OBS_SIZE (176) bytes.
 *
 * APPROXIMATIONS AND KNOWN GAPS (documented for future work):
 *
 * 1. Map ID: Uses (map_group * 256 + map_num) as uint16. pfr_native uses
 *    its own PfrNativeMapId enum. A mapping LUT is needed for full parity.
 *
 * 2. Mode: Only distinguishes OVERWORLD (0) and BATTLE (2). pfr_native has
 *    19 distinct modes (dialog, start menu, bag, etc.). Full mode detection
 *    requires inspecting callback function pointers and menu state.
 *
 * 3. Tile vocabulary: The raw metatile behavior byte from the GBA engine
 *    differs from pfr_native's custom PfrNativeTile behavior encoding.
 *    pfr_native uses (tile->behavior & 0x7F) | (tile->collision ? 0x80 : 0).
 *    A mapping LUT from GBA metatile behaviors to pfr_native's vocabulary
 *    would improve policy transfer fidelity.
 *
 * 4. Flags: Set to 0. pfr_native uses a 64-bit progression flag bitfield
 *    with its own flag IDs. Mapping GBA flags to pfr_native flags is needed.
 *
 * 5. Global position: Uses raw player_x/y as approximation. pfr_native
 *    uses a heatmap LUT (pfr_heatmap_lut.h) that maps (map_id, x, y) to
 *    a global coordinate space.
 *
 * 6. Battle obs: Zero-filled. Fully populating battle observations requires
 *    mapping the hosted runtime's BattlePokemon/gBattleMons to pfr_native's
 *    PfrBattleState struct with move types, power, PP etc. This is the
 *    hardest mapping and should be done when battle eval is prioritized.
 *
 * 7. NPCs: Sorted by distance (matching pfr_native), but graphics_id values
 *    from the GBA engine may not match pfr_native's NPC atlas IDs.
 */
void pfr_game_extract_obs_native_format(unsigned char *buf);

#endif /* PFR_OBS_ADAPTER_H */
