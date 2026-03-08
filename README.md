# pokefirered-native

This is the first native rehost slice for `pret/pokefirered`.

For a fresh agent handoff with the full current state, read `HANDOFF.md` in this directory first.

Scope:
- Keep upstream C as the source of truth.
- Do not rewrite game logic when an original upstream translation unit can be reused directly.
- Replace only the GBA ABI surface needed to execute those upstream files on a host platform.

Pinned upstream:
- Repository: `https://github.com/pret/pokefirered`
- Local checkout: `../pokefirered`
- Verified commit: `7e3f822652ecce0c99b626d74f455c3b93660377`

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
  - `src/clear_save_data_screen.c` for the real save-clear confirmation flow reached from the title-screen delete-save chord
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
- Narrow host title/help/save/sound/graphics shims, isolated under `host_title_screen_stubs.c` / `host_title_screen_stubs.h`, that let unchanged upstream `title_screen.c` progress from init into run-state and its current callback handoff edges.
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
- full boot through the real intro/title/main-menu flow
- exact `crt0.s:start_vector` CPU-mode/stack model
- MMIO / PPU register rehost
- full interrupt timing model
- m4a audio
- flash/save hardware
- serial/RFU link

Build:
```sh
cmake -S /home/spark-advantage/pokefirered-native -B /home/spark-advantage/pokefirered-native/build
cmake --build /home/spark-advantage/pokefirered-native/build -j
```

Targets:
- `pfr_smoke`: verifies the native bootstrap against exact-source RNG, heap, decompression, GPU register buffering, DMA3 request processing, scanline-effect HBlank DMA/task behavior, palette transfer/fade setup, BG control/tilemap/VRAM behavior, sprite sheet/palette/core object behavior, `main.c` helper/interrupt/runtime behavior, a `crt0.s`-derived host interrupt dispatcher, a bounded hosted `AgbMain` init/frame/soft-reset slice, the real upstream `intro.c` path through the full non-skipped Game Freak / Scene 1 / Scene 2 / Scene 3 sequence into natural title-screen handoff, real upstream `title_screen.c` progression from init into run-state plus restart/cry/main-menu/berry-fix handoffs, and the real upstream `clear_save_data_screen.c` provider through confirmation prompt, yes-no menu creation, and clear-save selection handling.
- `pfr_lz77`: uses the original upstream decompression entrypoints to decode an LZ77 blob.

LZ77 tool:
```sh
/home/spark-advantage/pokefirered-native/build/pfr_lz77 input.bin output.bin
```

Next rehost boundary, based on upstream source:
1. replace the remaining title callback-edge stubs beneath `src/title_screen.c` with real downstream providers for `main_menu.c` and `berry_fix_program.c` while keeping `title_screen.c` itself unchanged
2. tighten `host_crt0.c` / `host_agbmain.c` toward a closer `crt0.s:start_vector` / startup / interrupt model now that the deeper intro/title flow is proven
3. `src/m4a.c`, `src/m4a_1.s`, `src/sound.c`
4. `src/save.c`, `src/load_save.c`, `src/agb_flash*.c`, `src/link.c`
