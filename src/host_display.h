#ifndef POKEFIRERED_NATIVE_HOST_DISPLAY_H
#define POKEFIRERED_NATIVE_HOST_DISPLAY_H

#include <stddef.h>

#include "global.h"

/*
 * Display backend for pokefirered-native.
 *
 * When built with HOST_DISPLAY_SDL2, opens a 720x480 (3x) window with
 * keyboard input mapped to GBA buttons.
 *
 * Without SDL2, operates in headless mode dumping PPM frames to disk.
 */

/* Initialize the display backend.  Returns TRUE on success. */
bool8 HostDisplayInit(void);

enum
{
    HOST_DISPLAY_ACTION_NONE            = 0,
    HOST_DISPLAY_ACTION_QUICKSAVE      = 1 << 0,
    HOST_DISPLAY_ACTION_QUICKLOAD      = 1 << 1,
    HOST_DISPLAY_ACTION_STATE_SAVE     = 1 << 2,
    HOST_DISPLAY_ACTION_STATE_LOAD     = 1 << 3,
    HOST_DISPLAY_ACTION_STATE_SAVE_AS  = 1 << 4,
    HOST_DISPLAY_ACTION_STATE_LOAD_AS  = 1 << 5,
    HOST_DISPLAY_ACTION_REPAIR_BAG     = 1 << 6,
    HOST_DISPLAY_ACTION_SCREENSHOT     = 1 << 7,
};

/* Render the current GBA state and present it.
 * In SDL2 mode, also polls input into REG_KEYINPUT.
 * Returns FALSE if the user requested quit. */
bool8 HostDisplayPresent(void);

/* Clean up display resources. */
void HostDisplayDestroy(void);

/* Consume host-only debug actions triggered by the window backend. */
u32 HostDisplayConsumeActions(void);
bool8 HostDisplayConsumeSelectedStatePath(char *outPath, size_t outSize);
bool8 HostDisplayHasModalOverlay(void);
void HostDisplaySetStateDir(const char *dir);
void HostDisplayTakeScreenshot(void);

/* Frame dump control (works in both SDL and headless modes). */
void HostDisplaySetDumpDir(const char *dir);
void HostDisplayEnableDump(bool8 enable);

/* Frame counter. */
u32 HostDisplayGetFrameCount(void);

/* Fast-forward / render policy. */
u32 HostDisplayGetFastForwardFactor(void);
bool8 HostDisplayNeedsFrameRender(void);


/* ── Egg-hatch auto-walk + daycare/egg tracking OSD ── */

#define EGG_HATCH_MAX_DAYCARE 2
#define EGG_HATCH_MAX_EGGS    6

struct EggHatchDaycareMon {
    u16 species;
    u8  level;
    u32 steps;
};

struct EggHatchEggInfo {
    u16 species;
    u8  egg_cycles;     /* current friendship value (acts as egg cycle counter) */
    u8  base_egg_cycles; /* from species base stats (initial value) */
};

struct EggHatchInfo {
    struct EggHatchDaycareMon daycare[EGG_HATCH_MAX_DAYCARE];
    u8 daycare_count;
    struct EggHatchDaycareMon route5;
    bool8 route5_occupied;
    struct EggHatchEggInfo eggs[EGG_HATCH_MAX_EGGS];
    u8 egg_count;
    u32 total_steps;  /* player step counter for reference */
};

/* Called by pfr_play.c each frame to provide tracking data. */
void HostDisplaySetEggHatchInfo(const struct EggHatchInfo *info);

/* Query auto-walk state (for input injection). */
bool8 HostDisplayIsEggHatchActive(void);

#endif
