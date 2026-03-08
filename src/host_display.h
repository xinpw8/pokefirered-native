#ifndef POKEFIRERED_NATIVE_HOST_DISPLAY_H
#define POKEFIRERED_NATIVE_HOST_DISPLAY_H

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

/* Render the current GBA state and present it.
 * In SDL2 mode, also polls input into REG_KEYINPUT.
 * Returns FALSE if the user requested quit. */
bool8 HostDisplayPresent(void);

/* Clean up display resources. */
void HostDisplayDestroy(void);

/* Frame dump control (works in both SDL and headless modes). */
void HostDisplaySetDumpDir(const char *dir);
void HostDisplayEnableDump(bool8 enable);

/* Frame counter. */
u32 HostDisplayGetFrameCount(void);

#endif
