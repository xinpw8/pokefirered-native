#ifndef POKEFIRERED_NATIVE_HOST_RENDERER_H
#define POKEFIRERED_NATIVE_HOST_RENDERER_H

#include "global.h"

/*
 * GBA software PPU renderer.
 *
 * Reads VRAM, OAM, palette RAM, and I/O registers directly from the
 * memory-mapped GBA address space and composites a 240x160 ARGB8888
 * framebuffer each frame.  No SDL or platform dependency — the display
 * backend consumes the finished buffer.
 */

#define GBA_SCREEN_WIDTH  240
#define GBA_SCREEN_HEIGHT 160

/* Initialize renderer state (call once after HostMemoryInit). */
void HostRendererInit(void);

/* Render one complete frame into the internal framebuffer.
 * Call this once per VBlank, after all DMA/interrupt work has settled. */
void HostRendererRenderFrame(void);

/* Return a pointer to the 240x160 ARGB8888 pixel buffer. */
const u32 *HostRendererGetFramebuffer(void);

#endif
