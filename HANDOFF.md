# PFRN (Pokemon FireRed Native) — Clean Handoff
# Date: 2026-03-27
# Written by: Claude (context reset handoff)

## PROJECT IDENTITY
Native C port of Pokemon Fire Red. Takes pret/pokefirered decompilation (282 upstream .c files),
replaces GBA hardware with host equivalents, compiles to native Linux binary.
Used as PufferLib 4.0 RL environment.

## LOCATION
spark-advantage@192.168.0.26:/home/spark-advantage/pokefirered-native/
Branch: pfr-render-option-c
Last commit: d513c5a (add Mt Moon B1F spawn points)
GitHub: github.com/xinpw8/pokefirered-native

## WHAT ACTUALLY WORKS (VERIFIED)
- Build: pfr_play, pfr_game (libpfr_game.so), pfr_render_test compile clean
- Build FAILS on tests/battle_test.c (stale GameCtx references) — non-blocking
- Boot chain: Copyright → Intro → Title Screen — WORKS with audio
- 282/282 upstream .c files compiled, 37,075 symbols linked
- ELF symbol table auto-resolves all callback names at runtime
- Sound init (HostNativeSoundInit) works
- INCBIN: zero stubs — all resolved via asset pipeline
- Audio forwarding: SSH -R tunnel to WSLg PulseAudio works
- X11 forwarding: SSH -X/-Y for SDL window works

## WHAT IS BROKEN (HONEST)
1. **Overworld renders black screen** — After Oak speech → New Game, the overworld
   callbacks (CB1_Overworld, CB2_Overworld) ARE from real upstream overworld.c,
   but the display is black. The framebuffer goes uniform 0xFF000000.
   This is the PRIMARY blocker.

2. **Oak speech Pokemon sprites look like MissingNo** — During intro, the Pokemon
   in Oak's hands render garbled. Likely a sprite palette or tile data issue.

3. **Gender selection has mangled selector** — The boy/girl menu cursor renders
   nonsense characters. Text encoding was fixed (charmap preprocessing confirmed
   BOY = BC C9 D3 FF) but the cursor/selector graphics are still wrong.

4. **battle_test.c won't compile** — References stale GameCtx struct members.
   Fix: update to use direct globals instead of g_ctx->

## ARCHITECTURE

### Hardware Replacement Layer (26 host_*.c files)
| File | Lines | Purpose |
|------|-------|---------|
| host_renderer.c | 1002 | GBA PPU — per-scanline software renderer (Mode 0/1, sprites, windows, blend) |
| host_display.c | 1611 | SDL2 window, input mapping, frame pacing, savestate UI |
| host_sound_mixer.c | ~1400 | Replaces GBA m4a audio hardware |
| host_sound_init.c | ~200 | Native sound system init (bypasses GBA IWRAM pointers) |
| host_audio.c | ~300 | SDL audio callback, feeds PCM from SoundMain |
| host_flash.c | 437 | Save persistence (replaces GBA flash chip) |
| host_memory.c | 91 | mmap'd GBA address space (IWRAM, EWRAM, VRAM, OAM, PLTT, IO) |
| host_display.c | 246 | VBlank/HBlank dispatch, display registers |
| host_bios.c | 243 | BIOS calls (LZ77, div, etc) |
| host_dma.c | 165 | DMA transfer emulation |
| host_timer.c | 126 | GBA timer registers |
| host_agbmain.c | ~150 | Entry point wrapper |
| host_savestate.c | ~500 | Hot savestate for RL episode resets |

### Stub Files (4 remaining — mostly test instrumentation now)
| File | Actual Stubs | Notes |
|------|-------------|-------|
| host_intro_stubs.c | GameCubeMultiBoot (5 funcs) | Legitimate — GC link cable not needed |
| host_new_game_stubs.c | NONE — just test counters | Overworld callbacks removed, using real upstream |
| host_oak_speech_stubs.c | Test counters only | DoNamingScreen stub removed, using real upstream |
| host_title_screen_stubs.c | MultiBootInit/Main/Start/Complete | GBA link hardware, legitimate stubs |

### RL Environment (PufferLib 4.0)
- Location: ~/pufferlib-option-b/pufferlib/ocean/pfr_native/
- Each agent dlopen's libpfr_game.so (independent memory)
- 176-byte observation, 8-button action space
- Rewards: exploration, new map, warp, badge, battle, level, pokemon seen/caught
- Global heatmap: shared float32 buffer (1392x824)
- KNOWN BUG: heatmap LUT uses wrong map IDs (sequential vs group*256+num)

## WHAT NEEDS DOING (PRIORITY ORDER)

### P0 — Black screen overworld (THE blocker)
The game reaches CB2_Overworld but renders all-black. Likely causes:
- FieldClearVBlankHBlankCallbacks / SetFieldVBlankCallback not wiring correctly
- Forced blank bit (DISPCNT bit 7) stuck on
- Map tile/metatile data not loading (DoMapLoadLoop completing but VRAM empty)
- BG control registers not being set for overworld layers

DEBUG APPROACH: Dump DISPCNT, BGxCNT, VRAM word count, PLTT word count at
the moment CB2_Overworld starts running. Compare with emulator state.
The emulator (mGBA) is installed at ~/mgba/build/sdl/mgba and the ROM is at
~/pokefirered/pokefirered.gba — use it as ground truth.

### P1 — MissingNo sprites in Oak speech
Sprite tile data or palette not loading correctly for the Pokemon shown
during Oak's introduction. Check if OAM attributes reference correct
VRAM tile base and palette.

### P2 — Gender selector garbled
Charmap text encoding is correct (verified). The menu cursor/selector
graphics themselves are wrong. May be a window or tile issue in the
text/menu rendering system.

### P3 — Fix battle_test.c
Update to use direct globals instead of g_ctx-> (GameCtx was disabled).

### P4 — Heatmap LUT
Fix pfr_heatmap_lut.h to use map_group*256+map_num instead of sequential IDs.

### P5 — RL reward shaping
Agents stuck in Pallet Town. 0 warps, 0 badges. Need better incentives.

## BUILD & RUN

### Build:
cd ~/pokefirered-native && cmake --build build -j8

### Run interactively (with X11 + audio forwarding from WSL):
ssh -Y -R 24713:/mnt/wslg/PulseServer spark-advantage@192.168.0.26
cd pokefirered-native && ./build/pfr_play

### Run headless test:
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 10 ./build/pfr_play 2>&1 | head -50

### Controls:
Z=A  X=B  RShift=Select  Enter=Start  Arrows=D-pad
S=R  A=L  Escape=Quit  Space=Turbo  F5=Quicksave  F9=Quickload

## CREDENTIALS
- spark-advantage sudo: puffertank
- daa (WSL2) sudo: rtkm1y
- SSH from daa uses existing key auth

## ABSOLUTE RULES
1. No unbounded disk writes. Cap everything.
2. No stubs unless the feature genuinely doesn't exist natively (link cable, GC multiboot).
3. Build must compile clean (ignoring battle_test.c for now).
4. Every claim must be verified with actual output, not assumptions.
5. The pret disasm IS ground truth. If it was in the original, it must work in the native port.
