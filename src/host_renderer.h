#ifndef POKEFIRERED_NATIVE_HOST_RENDERER_H
#define POKEFIRERED_NATIVE_HOST_RENDERER_H

#include "global.h"

/*
 * GBA software PPU renderer (per-scanline).
 *
 * Reads VRAM, OAM, palette RAM, and I/O registers directly from the
 * memory-mapped GBA address space and composites a 240x160 ARGB8888
 * framebuffer one scanline at a time.  No SDL or platform dependency —
 * the display backend consumes the finished buffer.
 *
 * Per-scanline rendering enables correct HBlank DMA effects (scanline
 * palette shifts, scroll wave effects in intro scenes, etc.) when the
 * caller interleaves rendering with HBlank DMA / interrupt dispatch.
 */

#define GBA_SCREEN_WIDTH  240
#define GBA_SCREEN_HEIGHT 160

/* Initialize renderer state (call once after HostMemoryInit). */
void HostRendererInit(void);

/* Latch affine BG reference points and prepare for a new frame.
 * Call once at VBlank time, before rendering any scanlines. */
void HostRendererStartFrame(void);

/* Render a single scanline into the internal framebuffer.
 * Call for scanlines 0-159 in order.  Between calls, the caller should
 * dispatch HBlank DMA and interrupts so per-scanline register changes
 * are visible to the next scanline. */
void HostRendererRenderScanline(u16 y);

/* Convenience: render a full frame (StartFrame + 160 scanlines).
 * Useful for tests that don't need per-scanline HBlank effects. */
void HostRendererRenderFrame(void);

/* Return a pointer to the 240x160 ARGB8888 pixel buffer. */
const u32 *HostRendererGetFramebuffer(void);

#endif
