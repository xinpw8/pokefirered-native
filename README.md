# pokefirered-native

This is the first native rehost slice for `pret/pokefirered`.

For a fresh agent handoff with the full current state, read `HANDOFF.md` in this directory first.

Scope:
- Keep upstream C as the source of truth.
- Do not rewrite game logic when an original upstream translation unit can be reused directly.
- Replace only the GBA ABI surface needed to execute those upstream files on a host platform.

Pinned upstream:
- Repository: `https://github.com/pret/pokefirered`
- Submodule checkout: `third_party/pokefirered`
- Verified commit: `7e3f822652ecce0c99b626d74f455c3b93660377`

Published compatibility fork:
- Submodule URL: `https://github.com/xinpw8/pokefirered`
- Branch with native-host compatibility changes: `pokemon-firered-native-puffer`

What is wired today:
- Original upstream files compiled directly into the native build:
  - `src/random.c`
  - `src/malloc.c`
  - `src/decompress.c`
  - `src/gpu_regs.c`
  - `src/dma3_manager.c`
  - `src/task.c`
  - `src/trig.c`
  - `src/scanline_effect.c`
  - `src/palette.c`
  - `src/blend_palette.c`
  - `src/bg.c`
  - `src/sprite.c`
  - `src/main.c` for hosted helper/interrupt/runtime verification through the upstream non-`MODERN` path
  - `src/intro.c` for the real copyright-screen through Game Freak reveal-name/reveal-logo, Scene 1, Scene 2, Scene 3, and the natural non-skipped title-screen handoff
  - `src/title_screen.c` for real title-screen init, run-state progression, and downstream handoffs for restart/cry/main-menu/save-clear/berry-fix
  - `src/main_menu.c` for the real main-menu GPU/task/menu-state flow reached from the title-screen cry path
  - `src/oak_speech.c` for the real New Game provider reached from the main-menu handoff, currently proven through controls-guide page transitions, Pikachu intro exit, Oak init / `MUS_ROUTE24`, the welcome / `IStudyPokemon` / gender-selection flow, player and rival naming handoffs, and the callback transition into a host-owned `CB2_NewGame` seam
  - `src/clear_save_data_screen.c` for the real save-clear confirmation flow reached from the title-screen delete-save chord
  - `src/berry_fix_program.c` for the real berry-fix multiboot state machine reached from the title-screen berry-fix chord
- Host-backed fixed-address GBA memory regions:
  - EWRAM
  - IWRAM
  - I/O registers
  - palette RAM
  - VRAM
  - OAM
- Non-PIE host executables so upstream 32-bit pointer packing and DMA register surfaces stay usable on the 64-bit host.
- Host `global.h`/`gba.gba.h`/`gba.macro.h` routing so unchanged upstream `DmaSet`/`DmaStop` users execute against the host DMA layer instead of raw hardware writes, and so fixed 32-bit slots like `INTR_VECTOR` stay 32-bit on the host.
- A host `crt0`-derived interrupt dispatcher and `AgbMain` runner that drive real upstream init/loop code with hosted scanline/VBlank delivery and exit through the original soft-reset path.
- Narrow host intro/window/save/multiboot shims that let unchanged upstream `intro.c` advance from the copyright screen through the early Game Freak sequence into Scene 1.
- Narrow host title/help/save/sound/graphics/menu/multiboot shims, isolated under `host_title_screen_stubs.c` / `host_title_screen_stubs.h`, that let unchanged upstream `title_screen.c`, `main_menu.c`, `clear_save_data_screen.c`, and `berry_fix_program.c` progress through their current source-driven paths.
- Narrow host Oak Speech text/window/menu/naming/mon-sprite shims, isolated under `host_oak_speech_stubs.c` / `host_oak_speech_stubs.h`, that let unchanged upstream `oak_speech.c` link and execute its controls-guide, Pikachu-intro, and first Oak-dialogue path without widening gameplay rewrites into the upstream file.
- Host implementations for the BIOS/debug surface those files depend on:
  - `CpuSet`
  - `CpuFastSet`
  - `BgAffineSet`
  - `ObjAffineSet`
  - `LZ77UnCompWram`
  - `LZ77UnCompVram`
  - `RLUnCompWram`
  - `RLUnCompVram`
  - basic assert / print handlers
- Host DMA register executor for immediate/HBlank/VBlank transfer semantics used by reused upstream code.
- Host `dma3.h` shim for native execution of the original DMA3 manager queue logic.
- Minimal upstream globals/stubs required to link unused sections out safely.

What is explicitly not done yet:
- fully user-visible and user-playable boot through the real intro/title/main-menu/Oak flow
- exact `crt0.s:start_vector` CPU-mode/stack model
- MMIO / PPU register rehost
- full interrupt timing model
- m4a audio
- flash/save hardware
- serial/RFU link

Build:
```sh
cmake -S . -B build
cmake --build build --target pfr_rl_native pfr_rl_runner pfr_smoke -j
```

The build bootstraps the upstream helper tools it needs (`tools/preproc/preproc` and `tools/wav2agb/wav2agb`) into the CMake build tree automatically, generates the required JSON/map headers into `build/generated_include`, and materializes the direct-sound `.bin` assets from the tracked upstream `.wav` sources. A fresh clone does not need a manual submodule-side tool build before preprocessing, charmap generation, or sound-sample assembly.

Reproducible clone:
```sh
git clone --recurse-submodules https://github.com/xinpw8/pokefirered-native.git
cd pokefirered-native
git submodule update --init --recursive
```

Targets:
- `pfr_smoke`: verifies the native bootstrap against exact-source RNG, heap, decompression, GPU register buffering, DMA3 request processing, scanline-effect HBlank DMA/task behavior, palette transfer/fade setup, BG control/tilemap/VRAM behavior, sprite sheet/palette/core object behavior, `main.c` helper/interrupt/runtime behavior, a `crt0.s`-derived host interrupt dispatcher, a bounded hosted `AgbMain` init/frame/soft-reset slice, the real upstream `intro.c` path through the full non-skipped Game Freak / Scene 1 / Scene 2 / Scene 3 sequence into natural title-screen handoff, real upstream `title_screen.c` progression from init into run-state plus restart/cry/main-menu/save-clear/berry-fix downstream handoffs, the real upstream `main_menu.c` provider through save-present continue/new-game menu setup and New Game selection handoff, the real upstream `oak_speech.c` provider through `StartNewGameScene()`, controls-guide page transitions, Pikachu intro exit, Oak init / `MUS_ROUTE24`, `IStudyPokemon`, gender selection, player/rival naming handoffs, and the callback transition into `CB2_NewGame`, the real upstream `new_game.c` effects under a host-owned `CB2_NewGame` seam that mirrors more of the upstream setup sequence, the real upstream `clear_save_data_screen.c` provider through confirmation prompt, yes-no menu creation, and clear-save selection handling, and the real upstream `berry_fix_program.c` provider through begin/connect/power-off scenes and successful multiboot progression into the follow-instructions scene.
- `pfr_render_test`: runs a bounded 600-frame boot through copyright/Game Freak/intro rendering and currently passes with `11` non-empty sampled frames after the corrected `AgbMain`-order frame loop.
- `pfr_play`: runs the current interactive SDL boot harness so a user can drive the intro/title/main-menu path live, subject to the remaining renderer/window/text fidelity gaps.
- `pfr_lz77`: uses the original upstream decompression entrypoints to decode an LZ77 blob.
- `pfr_rl_native`: reusable native RL/staticvec archive exposing `PfrRlCore`, `PfrEnvSlot`, savestate slots, and RAM packet/state helpers for external consumers.
- `pfr_rl_runner`: minimal CLI over `PfrRlCore` for save/state boot, step, reset, and packet capture.

Debug workflow:
```sh
# Clone an existing save into a branch file, boot that branch headless and
# unthrottled, and auto-continue straight into gameplay.
/home/spark-advantage/pokefirered-native/build/pfr_play \
  --headless --unthrottled --mute \
  --save-path /tmp/branches/viridian_branch.sav \
  --load-save /home/spark-advantage/pokefirered-native/build/pokefirered.sav \
  --autoplay-continue
```

Useful `pfr_play` options:
- `--save-path <path>`: choose the active save file for this run.
- `--load-save <path>`: copy another save file into the active save path before boot.
- `--snapshot-dir <path>`: choose where F5 quicksave snapshots are written.
- `--state-dir <path>`: choose where Shift+F1 / Shift+F2 browse for savestate files.
- `--quickload-path <path>`: choose which snapshot F9 quickload restores.
- `--control-file <path>`: consume live debug commands from a regular file and delete it after processing.
- `--headless`: run without opening an SDL window.
- `--unthrottled`: disable SDL vsync pacing for faster runs.
- `--fast-forward <n>`: simulate `n` frames per present when unthrottled.
- `--mute`: skip host audio init.
- `--max-speed`: shorthand for `--headless --unthrottled --mute` plus heavier debug suppression.
- `--autoplay-continue`: auto-press through title/menu and continue from the current save.
- `--autoplay-oak`: auto-drive the new-game Oak flow.

Interactive debug shortcuts:
- `F1`: instant hotload of the latest in-memory savestate.
- `F2`: instant hotsave of the full current in-memory state.
- `Shift+F1`: open an in-window modal overlay to browse and load a savestate file without restarting.
- `Shift+F2`: open an in-window modal overlay to browse and save a savestate file without restarting.
- `Space`: toggle the currently selected turbo speed on and off while the game is running.
- `Shift+Space`: cycle the windowed turbo preset between `2x`, `4x`, `8x`, `16x`, `32x`, `64x`, `128x`, and `256x` without leaving the game. The default preset is `16x`.
- `F5`: force an overworld save when possible, then export an immutable timestamped snapshot under the snapshot dir.
- `F9`: copy the configured quickload snapshot, or the newest snapshot in the snapshot dir, into the active save path and restart `pfr_play`.

Savestate note:
- `F1` / `F2` and `statesave` / `stateload` are true live-process savestates. They restore instantly, but the `.pfrstate` files are only compatible with the same running `pfr_play` session.
- `F5` / `F9`, `saveas`, and `load` are battery-save branch files. They survive restarts and are the right tool for long-lived branch trees like separate starter paths.
- While the Shift+F1 / Shift+F2 overlay is open, gameplay input is locked to the overlay and emulation is paused until you finish or cancel.

Agent-friendly live commands:
```sh
# Start a branch headless at max speed and listen for live commands.
/home/spark-advantage/pokefirered-native/build/pfr_play \
  --max-speed \
  --fast-forward 4096 \
  --save-path /tmp/branches/squirtle_route1.sav \
  --load-save /home/spark-advantage/pokefirered-native/build/pokefirered.sav \
  --state-dir /tmp/states \
  --autoplay-continue \
  --control-file /tmp/pfr_control.txt

# While it is running, queue a command by writing a fresh file.
printf 'saveas /tmp/branches/squirtle_route1_after_oak.sav\n' > /tmp/pfr_control.txt
printf 'quicksave route1_branch\n' > /tmp/pfr_control.txt
printf 'hotsave\n' > /tmp/pfr_control.txt
printf 'statesave /tmp/states/route1_live.pfrstate\n' > /tmp/pfr_control.txt
printf 'hotload\n' > /tmp/pfr_control.txt
printf 'stateload /tmp/states/route1_live.pfrstate\n' > /tmp/pfr_control.txt
printf 'load /tmp/branches/bulbasaur_lab.sav\n' > /tmp/pfr_control.txt
printf 'quickload\n' > /tmp/pfr_control.txt
```

Supported control-file commands:
- `hotsave`: capture an instant in-memory savestate.
- `hotload`: restore the latest in-memory savestate without restarting.
- `statesave <path>`: save the current live savestate to a new `.pfrstate` file without overwriting an existing one.
- `stateload <path>`: restore a `.pfrstate` file from the current live session without restarting.
- `quicksave [label]`: create an immutable timestamped snapshot in the snapshot dir.
- `saveas <path>`: copy the current active save to an exact path without overwriting an existing file.
- `load <path>`: restore an exact save file into the active save path and restart into Continue.
- `quickload [path]`: restore the configured quickload path, the provided path, or the newest snapshot and restart.
- `list`: log known snapshot paths to `pfr_play_trace.log`.
- `quit`: stop the current `pfr_play` process.

LZ77 tool:
```sh
/home/spark-advantage/pokefirered-native/build/pfr_lz77 input.bin output.bin
```

Next rehost boundary, based on upstream source:
1. replace the current host-owned `CB2_NewGame` seam with the real upstream `overworld.c` new-game entry without pulling in an unbounded field/map/link dependency wave
2. add the minimum renderer/window/text/runtime support needed to make the existing `pfr_play` intro/title/main-menu/Oak path honestly user-visible and user-playable
3. tighten `host_crt0.c` / `host_agbmain.c` toward a closer `crt0.s:start_vector` / startup / interrupt model now that the deeper intro/title/menu/Oak flow is proven
4. `src/m4a.c`, `src/m4a_1.s`, `src/sound.c`
5. `src/save.c`, `src/load_save.c`, `src/agb_flash*.c`, `src/link.c`
