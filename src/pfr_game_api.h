/*
 * pfr_game_api.h — Public API for libpfr_game.so
 *
 * This header defines the interface that the RL environment uses
 * to interact with a pokefirered game instance. Each dlmopen'd
 * copy of libpfr_game.so has its own globals, providing fully
 * independent game instances.
 *
 * Usage:
 *   void *handle = dlmopen(LM_ID_NEWLM, "libpfr_game.so", RTLD_NOW);
 *   pfr_game_boot_fn boot = dlsym(handle, "pfr_game_boot");
 *   boot();
 *   // ... load savestate, step frames, extract obs ...
 */

#ifndef PFR_GAME_API_H
#define PFR_GAME_API_H

#include <stdint.h>

/* ── Boot ── */

/* Initialize GBA memory, CRT0, and boot the game to the main loop. */
void pfr_game_boot(void);

/* ── Savestate ── */

/* Load a savestate from disk and restore it. */
int pfr_game_load_state(const char *path);

/* Capture current state into the hot buffer. */
void pfr_game_save_hot(void);

/* Restore from the hot buffer. */
void pfr_game_restore_hot(void);

/* ── Frame stepping ── */

/*
 * Set the button input and run N frames.
 * keys: GBA button bitmask (active-high: set bits = pressed buttons)
 *       DPAD_UP=0x40, DPAD_DOWN=0x80, etc.
 * n: number of frames to step
 */
void pfr_game_step_frames(uint16_t keys, int n);

/* ── Observation extraction ── */

/*
 * Extract the 226-byte observation into the provided buffer.
 * See pfr_env.h for the observation layout.
 */
void pfr_game_extract_obs(unsigned char *buf);

/*
 * Extract observations in pfr_native's 176-byte format.
 * Enables a policy trained on pfr_native to run on the hosted runtime.
 * See pfr_obs_adapter.h for layout and known approximations.
 */
void pfr_game_extract_obs_native_format(unsigned char *buf);

/* ── Reward info ── */

typedef struct {
    int16_t  player_x;
    int16_t  player_y;
    uint8_t  map_group;
    uint8_t  map_num;
    uint8_t  badges;        /* bit field */
    uint8_t  party_count;
    uint16_t party_level_sum;
    uint32_t money;
    uint8_t  in_battle;
} PfrRewardInfo;

/* Read current game state for reward computation. */
void pfr_game_get_reward_info(PfrRewardInfo *info);

/* ── Exact rendering ── */

/*
 * Exact step+render: runs game logic with full timing model including
 * per-scanline HBlank dispatch, then renders into the internal framebuffer.
 * Use this for eval/presentation, NOT for fast RL training.
 *
 * Lazily initializes the host renderer on first call.
 */
void pfr_game_step_frames_exact(uint16_t keys, int n);

/*
 * Get pointer to the 240x160 ARGB8888 framebuffer.
 * Valid until the next step call. Returns NULL if no frame has been rendered.
 */
const uint32_t *pfr_game_get_framebuffer(void);

/*
 * Copy framebuffer to caller-owned buffer.
 * dst must be at least 240*160*4 bytes.
 * stride_pixels: row stride in pixels (use 240 for packed).
 */
void pfr_game_copy_framebuffer(uint32_t *dst, int stride_pixels);

/* Render the current GBA screen state without advancing the game. */
void pfr_game_render_current_frame(void);

/* Save current state to file. */
int pfr_game_save_state(const char *path);

/* ── Ultra-fast training path ── */

/*
 * Step N frames with maximum speed for RL training.
 * Skips ALL rendering work: vblankCallback, CopyBufferedValuesToGpuRegs,
 * ProcessDma3Requests, HostDmaTriggerVBlank, timer sync.
 * Only runs game logic callbacks + vblank counter increments.
 *
 * This is UNSAFE for rendering — GPU registers and palette RAM will be stale.
 * Use pfr_game_step_frames() or pfr_game_step_frames_exact() before rendering.
 */
void pfr_game_step_frames_fast(uint16_t keys, int n);

/* Comprehensive observation and reward (pfr_obs_full.h) */
#include "pfr_obs_full.h"
void pfr_game_extract_obs_full(void *buf);
void pfr_game_get_reward_info_full(PfrRewardInfoFull *info);

/* Warp to a random spawn point for per-episode exploration diversity.
 * Called during episode reset to vary starting position each episode. */
void pfr_game_randomize_spawn(void);

#endif /* PFR_GAME_API_H */
