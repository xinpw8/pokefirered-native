#ifndef POKEFIRERED_NATIVE_HOST_DISPLAY_H
#define POKEFIRERED_NATIVE_HOST_DISPLAY_H

#include "global.h"

/*
 * SDL2 display backend.
 *
 * Opens a window, presents the renderer's ARGB8888 framebuffer each
 * frame, and polls host input into REG_KEYINPUT.
 */

/* Initialize the SDL2 window and renderer.  Returns TRUE on success. */
bool8 HostDisplayInit(void);

/* Present the current framebuffer from HostRendererGetFramebuffer().
 * Also polls SDL events and updates REG_KEYINPUT.
 * Returns FALSE if the user closed the window. */
bool8 HostDisplayPresent(void);

/* Clean up SDL resources. */
void HostDisplayDestroy(void);

#endif
