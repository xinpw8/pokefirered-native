#ifndef POKEFIRERED_NATIVE_HOST_FRAME_STEP_H
#define POKEFIRERED_NATIVE_HOST_FRAME_STEP_H

#include "global.h"

typedef void (*HostFrameStepLogicFn)(void *userdata);

/*
 * Simulate one GBA frame using the shared hosted timing model:
 *   1. VBlank interrupt / DMA phase
 *   2. Caller-supplied game logic during VBlank
 *   3. Visible scanline rendering with HBlank dispatch between lines
 *
 * The caller installs interrupt handlers separately and decides how
 * to present or consume the finished framebuffer after stepping.
 */
void HostFrameStepRun(HostFrameStepLogicFn logicFn, void *userdata, bool8 renderFrame);

/*
 * Fast frame step for headless RL — runs game logic + VBlank handler
 * only, skipping per-scanline timer sync, HBlank DMA, VCount interrupts,
 * and all rendering.  Sound engine still ticks for game-logic correctness.
 */
void HostFrameStepRunFast(HostFrameStepLogicFn logicFn, void *userdata);

#endif /* POKEFIRERED_NATIVE_HOST_FRAME_STEP_H */
