# Pokefirered Native Port Handoff

This file is for a fresh agent with no prior turn context.

Read this before making changes to `/home/spark-advantage/pokefirered-native` or claiming anything about the `pret/pokefirered` native-port effort.

## Non-Negotiable Requirement

The user asked for a true 1:1 native port of `pret/pokefirered` with no inferred behavior.

That means:
- upstream source is the source of truth
- prefer compiling original upstream C unchanged
- replace only GBA-specific ABI and hardware surfaces underneath it
- do not claim the task is complete until the game boots and runs natively from source-driven behavior
- bootstrap slices, tools, and isolated subsystem ports are progress only

This policy is also recorded in [AGENTS.md](/home/spark-advantage/AGENTS.md:31).

## Upstream Source of Truth

Pinned upstream checkout:
- repo: `https://github.com/pret/pokefirered`
- local path: `/home/spark-advantage/pokefirered`
- verified commit: `7e3f822652ecce0c99b626d74f455c3b93660377`

This local checkout is clean and on `master`.

## Workspace Layout

Primary paths:
- upstream source: `/home/spark-advantage/pokefirered`
- native rehost workspace: `/home/spark-advantage/pokefirered-native`

Current native-project files:
- build script: `/home/spark-advantage/pokefirered-native/CMakeLists.txt`
- summary doc: `/home/spark-advantage/pokefirered-native/README.md`
- this handoff: `/home/spark-advantage/pokefirered-native/HANDOFF.md`
- host memory mapping: `/home/spark-advantage/pokefirered-native/src/host_memory.c`
- host DMA executor: `/home/spark-advantage/pokefirered-native/src/host_dma.c`
- host BIOS shims: `/home/spark-advantage/pokefirered-native/src/host_bios.c`
- host `crt0` translation: `/home/spark-advantage/pokefirered-native/src/host_crt0.c`
- host `crt0` API: `/home/spark-advantage/pokefirered-native/src/host_crt0.h`
- host debug/assert shims: `/home/spark-advantage/pokefirered-native/src/host_debug.c`
- bounded host `AgbMain` runner: `/home/spark-advantage/pokefirered-native/src/host_agbmain.c`
- bounded host `AgbMain` API: `/home/spark-advantage/pokefirered-native/src/host_agbmain.h`
- intro/window/save/multiboot stubs: `/home/spark-advantage/pokefirered-native/src/host_intro_stubs.c`
- intro/window/save/multiboot stub state: `/home/spark-advantage/pokefirered-native/src/host_intro_stubs.h`
- host new-game/runtime data stubs: `/home/spark-advantage/pokefirered-native/src/host_new_game_stubs.c`
- host new-game/runtime state: `/home/spark-advantage/pokefirered-native/src/host_new_game_stubs.h`
- title/help/save/sound/graphics stubs: `/home/spark-advantage/pokefirered-native/src/host_title_screen_stubs.c`
- title stub state: `/home/spark-advantage/pokefirered-native/src/host_title_screen_stubs.h`
- minimal link-time stubs: `/home/spark-advantage/pokefirered-native/src/upstream_stubs.c`
- host runtime stub state header: `/home/spark-advantage/pokefirered-native/src/host_runtime_stubs.h`
- host override include root: `/home/spark-advantage/pokefirered-native/src/host_include`
- host `global.h` wrapper: `/home/spark-advantage/pokefirered-native/src/host_include/global.h`
- host `gba/gba.h` wrapper: `/home/spark-advantage/pokefirered-native/src/host_include/gba/gba.h`
- host `gba/macro.h` wrapper: `/home/spark-advantage/pokefirered-native/src/host_include/gba/macro.h`
- host `dma3.h` shim: `/home/spark-advantage/pokefirered-native/src/host_include/dma3.h`
- smoke test: `/home/spark-advantage/pokefirered-native/tests/smoke.c`
- LZ77 CLI: `/home/spark-advantage/pokefirered-native/tools/pfr_lz77.c`

## What Has Been Verified

These original upstream translation units are compiled directly into the native build:
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
- `src/main.c`
- `src/intro.c`
- `src/title_screen.c`
- `src/main_menu.c`
- `src/clear_save_data_screen.c`
- `src/berry_fix_program.c`
- `src/new_game.c`
- `src/play_time.c`
- `src/easy_chat.c`
- `src/trainer_fan_club.c`
- `src/pokemon_size_record.c`
- `src/money.c`
- `src/berry_powder.c`
- `src/mystery_gift.c`
- `src/renewable_hidden_items.c`
- `src/roamer.c`

They are not rewrites. They are built from the upstream checkout.

Important qualification:
- `main.c` currently builds on host through the upstream non-`MODERN` path so the ARM-only entry-clear inline assembly in `AgbMain` is skipped
- this now verifies real `main.c` helper behavior, a host C translation of `crt0.s:intr_main`, a bounded `AgbMain` init/frame/soft-reset slice, the real `intro.c` path through the non-skipped Game Freak / Scene 1 / Scene 2 / Scene 3 sequence into natural title handoff, real `title_screen.c` progression from init into run-state plus restart/cry/main-menu/save-clear/berry-fix downstream handoffs, the real `main_menu.c` provider through save-present menu setup and New Game selection handoff, the real `clear_save_data_screen.c` provider through its confirmation/menu/clear-selection path, the real `berry_fix_program.c` provider through its multiboot progression path, and the real `new_game.c` data-init path once Oak hands off into `CB2_NewGame`
- `intro.c` and `title_screen.c` currently run against host zero-INCBIN or placeholder asset fallbacks plus narrow helper stubs; that is enough for source-driven control-flow verification, not a finished asset/runtime rehost
- `oak_speech.c` now reaches `IStudyPokemon`, the gender-selection menu, player/rival naming stubs, and a host-owned `CB2_NewGame` wrapper that executes the real upstream `NewGameInitData()` / `PlayTimeCounter_Start()` path plus the immediate stop-music, safari-reset, script-context, field-callback, and callback-handoff setup that upstream `CB2_NewGame` performs before entering overworld
- it is still not a full hosted boot through the complete `AgbMain` -> `intro.c` -> `title_screen.c` -> main-menu -> overworld flow because upstream `overworld.c` and the field/map stack are not in the build yet

## Why The Build Is Structured This Way

### 1. Use upstream C directly

The decomp is already mostly C. The correct path is rehosting, not rewriting.

### 2. Use `-iquote`, not `-I`, for upstream headers

Important pitfall: upstream has `include/strings.h`, which collides with the system's `<strings.h>` resolution if you use `-Iinclude`.

The current build intentionally uses quote-only include lookup for upstream headers:
- `-iquote/home/spark-advantage/pokefirered/include`

Do not "simplify" this to `-I` unless you want host builds to start failing in confusing ways.

### 3. Use non-PIE host executables

Important pitfall: upstream code routinely moves pointers through 32-bit hardware/register/task surfaces.

Examples already in the current slice:
- DMA source/dest registers are 32-bit
- `task.c` stores followup callbacks through 32-bit halves

The current build intentionally uses non-PIE executables:
- compiler `-fno-pie`
- linker `-no-pie`

Without that, the hosted binary lands at high 64-bit addresses and otherwise-correct upstream code starts truncating pointers.

### 4. Use host fixed-address memory mappings

A lot of upstream code dereferences GBA addresses directly:
- `REG_BASE` / `REG_*`
- `PLTT`
- `VRAM`
- `OAM`
- `EWRAM_START`
- `IWRAM_START`

To preserve that behavior, the native layer maps those address ranges into the host process with `mmap(... MAP_FIXED_NOREPLACE ...)` in `src/host_memory.c`.

Currently mapped regions:
- EWRAM
- IWRAM
- I/O register space
- palette RAM
- VRAM
- OAM

This was tested successfully on this machine.

### 5. Use `--gc-sections` and section-level compilation

The native build uses:
- `-ffunction-sections`
- `-fdata-sections`
- linker `--gc-sections`

That lets the project reuse upstream files while tolerating unresolved references inside functions that are not actually pulled into the final binary yet.

This is how `decompress.c` can be included even though some paths refer to sprite/pokemon systems that are not rehosted yet.

### 6. Use targeted host header overrides only when necessary

Current host override include root:
- `/home/spark-advantage/pokefirered-native/src/host_include`

The current host-routed headers are:
- `global.h`
- `gba/gba.h`
- `gba/macro.h`
- `dma3.h`

Reason:
- upstream C almost always enters through `global.h`
- upstream `global.h` pulls `gba/gba.h`, and upstream `gba/gba.h` pulls `gba/macro.h`
- generic DMA/palette code like `palette.c` uses `DmaCopy16` / `DmaSet` directly
- upstream `dma3_manager.c` queue logic is valuable and should stay unchanged
- upstream `include/dma3.h` expands to GBA DMA register macros through `DmaCopy*` / `DmaFill*`
- for host execution, we need those macros to resolve to the host DMA layer instead of raw hardware writes

So:
- `host_include/global.h` is a pinned copy of upstream `global.h` that routes into the host `gba` wrapper and defines `_()` as identity for native builds
- `host_include/gba/gba.h` routes the GBA header surface through the host `macro.h` and keeps `INTR_VECTOR` as a 32-bit fixed-address slot on aarch64
- `host_include/gba/macro.h` preserves the upstream macro API but redirects `DmaSet` / `DmaStop` into the host DMA executor
- `host_include/dma3.h` preserves the upstream DMA3 manager API and queue behavior while implementing the transfer primitives on top of `CpuCopy*` / `CpuFill*`

Do not widen header overrides casually. Only override when it preserves upstream behavior better than rewriting call sites.

### 7. Emulate DMA from the register surface, not rewritten call sites

The current generic DMA executor lives in:
- `/home/spark-advantage/pokefirered-native/src/host_dma.c`

It treats the mapped DMA registers as the source of truth and currently supports the semantics needed by the verified slice:
- immediate-start transfers
- HBlank-triggered transfers
- VBlank-triggered transfers
- repeat and destination reload behavior used by `scanline_effect.c`
- automatic execution of unchanged `DmaCopy*` / `DmaFill*` users routed through the host `gba/macro.h`

This is intentionally below the upstream C layer. `scanline_effect.c` is compiled unchanged.

## Current Native Build

Build commands:
```sh
cmake -S /home/spark-advantage/pokefirered-native -B /home/spark-advantage/pokefirered-native/build
cmake --build /home/spark-advantage/pokefirered-native/build -j4
```

Produced targets:
- `pfr_smoke`
- `pfr_render_test`
- `pfr_play`
- `pfr_lz77`

## Current Verification

Verified commands and outcomes:
- `cmake --build /home/spark-advantage/pokefirered-native/build -j4`
- `/home/spark-advantage/pokefirered-native/build/pfr_smoke`
  - expected output: `pfr_smoke: ok`
- `/home/spark-advantage/pokefirered-native/build/pfr_render_test /tmp/pfr_render_codex_20260308`
  - expected outcome: `PASS: Renderer produced visible output.` with `14 non-empty frames out of 20 sampled`
- `timeout 8 /home/spark-advantage/pokefirered-native/build/pfr_play`
  - expected outcome: the interactive SDL loop survives until `timeout` kills it, with no immediate crash during boot
- `/home/spark-advantage/pokefirered-native/build/pfr_lz77` on a known simple blob
  - expected outcome: successful decode to `ABCD`

### What `pfr_smoke` currently proves

`tests/smoke.c` validates:
- upstream RNG sequence from `random.c`
- upstream heap allocator behavior from `malloc.c`
- host `CpuSet` / `CpuFill` / `CpuCopy` behavior needed by reused upstream code
- upstream LZ77 path through `decompress.c`
- host RL decompression path used by GBA BIOS ABI surface
- upstream GPU register buffering and interrupt-bit sync from `gpu_regs.c`
- upstream DMA3 queue behavior from `dma3_manager.c`
- upstream task list behavior used by `scanline_effect.c`
- upstream sine-table driven scanline wave generation from `trig.c` + `scanline_effect.c`
- HBlank DMA register behavior for the first verified scanline-effect path
- upstream palette buffer transfer and normal-fade initialization from `palette.c` + `blend_palette.c`
- upstream BG control, BG VRAM copy, tilemap buffer, and scroll register behavior from `bg.c`
- upstream sprite reset, sheet/palette load, create/destroy, and OAM reset behavior from `sprite.c`
- upstream `main.c` helper/runtime behavior for key init, timer-based RNG seeding, callback setup, interrupt table setup, flash-timer hookup, Pokemon cry clearing, and VBlank/HBlank/VCount/Serial dispatch through the real `gIntrTable`
- host C translation of `crt0.s:intr_main` priority/ack/mask behavior under `IF`/`IE`/`IME`
- host BIOS affine setup through `BgAffineSet` and `ObjAffineSet` so unchanged upstream sprite/background affine users can execute on the host
- bounded upstream `AgbMain` initialization and frame-loop behavior through the real init path, callback setup, loop body, VBlank wait, first copyright callback frame, and soft-reset exit path
- real upstream `intro.c` callback progression from `CB2_InitCopyrightScreenAfterBootup` through copyright fade-out, intro setup, the first `CB2_Intro` frame, the Game Freak star, reveal-name, reveal-logo, Scene 1, Scene 2, Scene 3, and the natural non-skipped handoff into `CB2_InitTitleScreen`
- real upstream `title_screen.c` progression from init through the first title-loop frame, run-state setup, timeout restart, cry-to-main-menu handoff, delete-save handoff into `CB2_SaveClearScreen_Init`, and berry-fix handoff into `CB2_InitBerryFixProgram`
- real upstream `main_menu.c` progression through save-present menu setup, continue-stat printing, fade-in/input-ready state, and New Game selection handoff into `StartNewGameScene`
- real upstream `oak_speech.c` progression through `StartNewGameScene()`, initial New Game callback/task setup, controls-guide page transitions, Pikachu intro exit, Oak init / `MUS_ROUTE24`, Oak's first two welcome messages, `IStudyPokemon`, the player gender question, player/rival naming handoffs, and the callback transition into `CB2_NewGame`
- real upstream `new_game.c` effects under the hosted `CB2_NewGame` wrapper, including different-save marking, trainer-id/default-data seeding, initial Player's House 2F warp setup, starter money, PC item setup, RSE national-dex flag/var seeding, trainer tower reset, play-time start, and the immediate host-owned stop-music / safari-reset / script-context / field-callback / callback-handoff setup that mirrors the upstream `overworld.c:CB2_NewGame` body before the real map/field stack takes over
- real upstream `clear_save_data_screen.c` progression through init, GPU/window setup, confirmation prompt, yes-no menu creation, and yes-selection clear-save handling
- real upstream `berry_fix_program.c` progression through init, begin/connect/power-off scene changes, multiboot init/start, and successful advance into the follow-instructions scene

This is still a bootstrap test, not a game boot test.

## Exact Files And Roles

### Native rehost files

`/home/spark-advantage/pokefirered-native/CMakeLists.txt`
- defines the host build
- pulls in the current upstream translation units unchanged
- uses `-iquote` for both the host override include root and the upstream include root
- forces non-PIE executables so 32-bit upstream pointer assumptions hold on the host
- builds `src/main.c` as a separate hosted object with `MODERN=0` so the rest of the translation unit can execute unchanged on aarch64
- builds `src/intro.c` and `src/title_screen.c` as separate hosted objects so the boot callback chain can advance without rewriting those upstream files
- now also generates upstream `map_groups.h`, `map_event_ids.h`, and `region_map_sections.h` through upstream `mapjson`/`jsonproc` so additional field/new-game translation units can compile without hand-written headers

`/home/spark-advantage/pokefirered-native/src/host_memory.c`
- maps fixed host memory at GBA-style addresses
- zeroes the mapped regions on reset
- this is what allows upstream MMIO-style code to run without rewriting `REG_*` access patterns

`/home/spark-advantage/pokefirered-native/src/host_dma.c`
- executes DMA semantics from the mapped DMA registers
- provides the current HBlank/VBlank transfer behavior needed by `scanline_effect.c`
- shadows full host source/dest pointers per DMA channel so upstream `DmaFill*` stack temporaries still work through the 32-bit register surface
- stays beneath the upstream C layer instead of replacing the upstream call sites

`/home/spark-advantage/pokefirered-native/src/host_include/global.h`
- pinned host copy of upstream `global.h`
- routes upstream translation units into the host `gba` wrapper without rewriting those translation units
- now also provides default zero-INCBIN fallbacks so asset-bearing upstream files like `intro.c` can compile unchanged on host

`/home/spark-advantage/pokefirered-native/src/host_include/gba/gba.h`
- host wrapper for the upstream GBA header surface
- ensures upstream `global.h` users see the host `gba/macro.h`
- keeps `INTR_VECTOR` as a 32-bit fixed-address slot so `InitIntrHandlers` does not overrun mapped IWRAM on aarch64

`/home/spark-advantage/pokefirered-native/src/host_include/gba/macro.h`
- preserves the upstream macro surface
- redirects generic `DmaSet` / `DmaStop` users into `host_dma.c`

`/home/spark-advantage/pokefirered-native/src/host_bios.c`
- implements current BIOS ABI surface needed by reused upstream files
- includes:
  - `CpuSet`
  - `CpuFastSet`
  - `LZ77UnCompWram`
  - `LZ77UnCompVram`
  - `RLUnCompWram`
  - `RLUnCompVram`
  - `RegisterRamReset`
  - `VBlankIntrWait`
  - `Sqrt`
  - `Div`
- still intentionally aborts for unimplemented surfaces like `SoftReset`, `BgAffineSet`, `ObjAffineSet`, `MultiBoot`, `ArcTan2`
- `SoftReset` is now intercepted for the bounded hosted `AgbMain` runner and still aborts outside that controlled path
- zero-header LZ77 assets now decode as no-ops so the host zero-INCBIN fallback can drive unchanged callback code without fake asset blobs

`/home/spark-advantage/pokefirered-native/src/host_crt0.c`
- host C translation of the `crt0.s:intr_main` interrupt selection/ack/mask/dispatch flow
- raises/dispatches pending IRQs through `IF`/`IE`/`IME` instead of calling `gIntrTable` directly
- seeds host startup in VBlank so early upstream `DISPSTAT` programming behaves like the verified boot slice expects

`/home/spark-advantage/pokefirered-native/src/host_crt0.h`
- exposes the host interrupt raise/dispatch API and startup init helper used by smoke tests and the hosted `AgbMain` runner

`/home/spark-advantage/pokefirered-native/src/host_agbmain.c`
- provides the current bounded host entry for running real upstream `AgbMain`
- starts a host scanline thread that raises VCount/HBlank/VBlank through the host `crt0` translation
- exits via the original soft-reset path using a host longjmp escape in the controlled test environment

`/home/spark-advantage/pokefirered-native/src/host_agbmain.h`
- exposes the bounded `AgbMain` runner API and the host hooks used by BIOS/stub code during that run

`/home/spark-advantage/pokefirered-native/src/host_debug.c`
- provides host implementations for AGB/mGBA/NoCash print and assert entrypoints used by upstream debug macros

`/home/spark-advantage/pokefirered-native/src/upstream_stubs.c`
- provides the minimum globals/stubs needed to link currently reused upstream translation units
- now also carries the minimal non-boot runtime globals/stubs needed for the verified `main.c` helper/interrupt slice
- this is intentionally small and should stay that way

`/home/spark-advantage/pokefirered-native/src/host_intro_stubs.c`
- provides the narrow multiboot/window/save/menu/audio/util helper surface needed by the current `intro.c` slice
- these are bootstrap shims for control-flow verification only and should be replaced with upstream providers where practical as the port moves forward

`/home/spark-advantage/pokefirered-native/src/host_intro_stubs.h`
- exposes intro-specific stub counters/state so smoke coverage can prove the real callback chain advanced through the expected host seams

`/home/spark-advantage/pokefirered-native/src/host_title_screen_stubs.c`
- provides the narrow title/help/save/sound/graphics extern surface needed to link and run the current `title_screen.c` slice
- carries explicit placeholder graphics globals for the title-screen assets that come from upstream `graphics.c` externs rather than local `INCBIN_*`s

`/home/spark-advantage/pokefirered-native/src/host_title_screen_stubs.h`
- exposes title-specific stub counters/state so smoke coverage can prove the real title-screen init path reached the expected host seams

`/home/spark-advantage/pokefirered-native/src/host_runtime_stubs.h`
- exposes stub call counters/state used by the smoke test to verify that real upstream `main.c` interrupt handlers are driving the host stub surface as expected

`/home/spark-advantage/pokefirered-native/src/host_include/dma3.h`
- host override for upstream `dma3.h`
- preserves the upstream DMA3 manager interface while swapping transfer backends from hardware DMA to host copy/fill primitives

`/home/spark-advantage/pokefirered-native/tests/smoke.c`
- current regression test for the bootstrap slice
- now includes a bounded `AgbMain` run that verifies real upstream init side effects and loop progress under hosted `crt0`-style interrupt delivery
- now also drives the real `intro.c` path through the Game Freak star phase and verifies an early-input handoff into real `title_screen.c` init plus the first title-loop frame

`/home/spark-advantage/pokefirered-native/tools/pfr_lz77.c`
- native utility that runs the original upstream decompression entrypoints on a file

### Upstream files already analyzed and important

Boot/runtime roots:
- `/home/spark-advantage/pokefirered/src/rom_header.s`
- `/home/spark-advantage/pokefirered/src/crt0.s`
- `/home/spark-advantage/pokefirered/src/main.c`

Current reused upstream slices:
- `/home/spark-advantage/pokefirered/src/random.c`
- `/home/spark-advantage/pokefirered/src/malloc.c`
- `/home/spark-advantage/pokefirered/src/decompress.c`
- `/home/spark-advantage/pokefirered/src/gpu_regs.c`
- `/home/spark-advantage/pokefirered/src/dma3_manager.c`
- `/home/spark-advantage/pokefirered/src/task.c`
- `/home/spark-advantage/pokefirered/src/trig.c`
- `/home/spark-advantage/pokefirered/src/scanline_effect.c`
- `/home/spark-advantage/pokefirered/src/palette.c`
- `/home/spark-advantage/pokefirered/src/blend_palette.c`
- `/home/spark-advantage/pokefirered/src/bg.c`
- `/home/spark-advantage/pokefirered/src/sprite.c`
- `/home/spark-advantage/pokefirered/src/main.c`
- `/home/spark-advantage/pokefirered/src/intro.c`
- `/home/spark-advantage/pokefirered/src/title_screen.c`

Core GBA ABI / hardware boundaries:
- `/home/spark-advantage/pokefirered/include/gba/io_reg.h`
- `/home/spark-advantage/pokefirered/include/gba/macro.h`
- `/home/spark-advantage/pokefirered/include/gba/syscall.h`
- `/home/spark-advantage/pokefirered/src/libagbsyscall.s`

Next boot/runtime path:
- `/home/spark-advantage/pokefirered/src/intro.c` through its natural non-skipped title handoff
- `/home/spark-advantage/pokefirered/src/title_screen.c` beyond init into fade/run/restart/cry and menu/save transitions

Later heavy subsystems:
- `/home/spark-advantage/pokefirered/src/m4a.c`
- `/home/spark-advantage/pokefirered/src/m4a_1.s`
- `/home/spark-advantage/pokefirered/src/sound.c`
- `/home/spark-advantage/pokefirered/src/save.c`
- `/home/spark-advantage/pokefirered/src/load_save.c`
- `/home/spark-advantage/pokefirered/src/agb_flash.c`
- `/home/spark-advantage/pokefirered/src/link.c`

## Entry And Control Flow Already Mapped

Boot entry:
- `rom_header.s` label `Start` branches to `start_vector`
- `crt0.s:start_vector` sets IRQ and SYS stacks, writes `INTR_VECTOR`, then jumps to `AgbMain`
- `crt0.s:intr_main` dispatches interrupts by scanning `IF/IE` bits and calling through `gIntrTable`

Main runtime:
- `main.c:AgbMain` is the hosted equivalent of the game main loop
- no normal C `main()` exists in upstream
- the host build now compiles `main.c` directly and verifies several public helpers plus interrupt dispatch
- the host build now also runs a bounded `AgbMain` slice through real init and frame-loop behavior
- `AgbMain` now reaches the first real `intro.c` callback frame under host control, but it still does not run end-to-end through the real intro/title boot chain

Important behavior in `AgbMain`:
- resets RAM / clears memory
- initializes GPU register manager
- initializes key input, interrupts, sound, RFU, flash detection, map music, BGs, heap, fonts
- enters an infinite frame loop
- per frame it reads keys, handles soft reset combo, runs link/callback logic, updates play time and music, then waits for vblank

The first callback target set during init is:
- `CB2_InitCopyrightScreenAfterBootup`

That means a true boot path eventually needs enough of the callback, interrupt, palette, BG, sprite, and task systems to reach and display the early intro/title flow.

## Hardware Dependencies Already Mapped

These are the main blocking GBA-facing surfaces for a real boot:
- MMIO register semantics from `io_reg.h`
- interrupt enable/disable and dispatch
- DMA start/stop and HBlank/VBlank timing
- PPU-facing register effects and palette/VRAM/OAM transfer semantics
- m4a audio register + mixer behavior
- flash save hardware behavior
- serial/RFU link behavior

## What Is Not Done Yet

Do not claim these exist:
- no native boot through the full real intro/title/main-menu callback chain
- no exact `crt0.s:start_vector` CPU-mode/stack rehost
- no full IRQ/PPU timing model
- no m4a audio rehost
- no flash/save implementation
- no link or RFU implementation
- no actual game rendering loop

The current project is still a validated rehost bootstrap, not a finished port.

## Important Pitfalls

### `main.c` currently relies on the upstream non-`MODERN` path on host

`AgbMain` has inline ARM assembly under `MODERN` that clears memory on entry. That is not host-executable on aarch64 as-is.

The current host build therefore compiles `main.c` with `MODERN=0`:
- this keeps the rest of the translation unit unchanged and verified
- this is sufficient for the current helper/interrupt slice
- this is not, by itself, a finished host boot solution for `AgbMain`

If you push farther into boot, keep that distinction explicit.

### `INTR_VECTOR` is a fixed 32-bit slot, even on host

Upstream defines `INTR_VECTOR` at `0x03007FFC`, the end of IWRAM.

On GBA that is a 32-bit slot. On aarch64, treating it as `void **` causes an 8-byte store/load that overruns the mapped IWRAM range.

The host `gba/gba.h` wrapper now overrides `INTR_VECTOR` back to a 32-bit register slot. Do not remove that unless you also change the host memory/ABI model deliberately.

### `crt0.s:intr_main` is now translated, but `start_vector` is not fully modeled

The host build now has a C translation of the `crt0.s:intr_main` priority/ack/mask/dispatch flow in `src/host_crt0.c`.

That is enough for the current verified `AgbMain` and `intro.c` slices, but it is still not a full host-equivalent rehost of:
- `crt0.s:start_vector` CPU mode changes
- IRQ/SYS stack setup
- exact hardware interrupt timing

Do not overstate this as a finished `crt0.s` port.

### The current `intro.c` and `title_screen.c` slices still use placeholder asset surfaces

`intro.c` now compiles unchanged, but its `INCBIN_*` assets currently come through the host `global.h` zero fallback.

`title_screen.c` also uses host placeholder graphics globals from `src/host_title_screen_stubs.c` for the upstream `graphics.c` externs it references.

To keep the real callback chain executable, host LZ77 decode treats a zero header as an empty asset and the narrow intro/title helper surfaces are stubbed beneath the upstream files.

That is good enough to prove source-driven control flow through the Game Freak star phase and first title-screen frame. It is not a finished graphics/content rehost.

### `title_screen.c` needs explicit host graphics globals

Unlike `intro.c`, part of the title-screen asset surface comes from externs declared in upstream `graphics.h` and normally defined in `graphics.c`.

The host zero-INCBIN fallback does not cover those externs.

That is why `src/host_title_screen_stubs.c` now carries explicit placeholder definitions for:
- `gGraphics_TitleScreen_*`
- `gTitleScreen_Slash_Pal`
- `gTitleScreen_BlankSprite_Tiles`

If you push farther into title or replace those with upstream providers, keep the distinction clear.

### The hosted `global.h` copy is pinned to the current upstream commit

`/home/spark-advantage/pokefirered-native/src/host_include/global.h` started as a direct copy of the pinned upstream `include/global.h`.

Use it carefully:
- keep it aligned with upstream if the pinned commit changes
- do not casually hand-edit unrelated parts
- the intended delta is include routing for the host GBA macro surface, not behavior changes

### `scanline_effect.c`, `palette.c`, and `bg.c` already forced the host DMA/macro surface

Those slices are now integrated and verified, which means:
- the build must stay non-PIE unless you replace every remaining 32-bit pointer assumption
- the current DMA executor and host `gba/macro.h` routing must keep preserving immediate/HBlank/repeat/dest-reload semantics

The next boot/runtime work should build on that existing behavior rather than reintroducing higher-level rewrites.

### Do not casually replace upstream includes or sources

The current strategy is intentionally conservative:
- compile original upstream files directly whenever feasible
- add the smallest possible host layer beneath them
- only override a header when the macro surface itself is the hardware dependency

## Recommended Next Step

If continuing from here, the most defensible next step is:
1. replace the current host-owned `CB2_NewGame` seam with the real upstream `overworld.c` new-game entry or a narrower extracted equivalent; the direct whole-TU attempt currently explodes into the field/map/link stack and is the next real blocker
2. make the existing `pfr_play` path honestly user-visible and user-playable by filling the remaining window/text/masking gaps in the hosted renderer/runtime surface
3. tighten `host_crt0.c` and `host_agbmain.c` toward a closer `crt0.s:start_vector` / startup model now that the deeper intro/title/menu/Oak flow is proven
4. keep the now-verified `crt0`/AgbMain/palette/bg/sprite/task/scanline/title/menu/Oak path as the runtime base while m4a, save, and link stay explicitly pending

Current Oak Speech note:
- the immediate post-Pikachu-intro Nidoran sprite crash was removed by adding blank mon-pic / mon-palette fallbacks plus a dummy multiuse sprite template in the host seam
- the passing smoke boundary now reaches Oak init / `MUS_ROUTE24`, Oak's `Welcome to the world` and `This world` messages, and the Nidoran release / `IsInhabitedFarAndWide` line plus cry via the placeholder-expansion seam in `host_oak_speech_stubs.c`
- `src/pfr_play.c` and `tests/render_test.c` were corrected to follow upstream `AgbMain` ordering instead of double-running tasks/fades outside `callback2`; this removed a real timing mismatch behind the user-reported too-fast title/intro behavior
- the next honest Oak boundary is `IStudyPokemon`, then `TellMeALittleAboutYourself` / gender selection, not another re-assertion of the already-proven early lines

A likely practical sub-plan for `main.c` from here:
- keep the bounded `AgbMain` runner for smoke-level control
- push `intro.c` and `title_screen.c` farther by implementing or reusing the smallest remaining gflib/help/menu/save/sound helpers the next callbacks need
- replace zero-INCBIN/bootstrap shims with real asset/runtime providers as soon as a cleaner upstream-backed route exists
- keep `m4a`, flash/save, and link clearly marked as pending while driving farther into boot

## Reproduction Checklist For A Fresh Agent

1. Read this file.
2. Read `/home/spark-advantage/AGENTS.md` and confirm the non-completion rule for this project.
3. Build the current native workspace:
   ```sh
   cmake -S /home/spark-advantage/pokefirered-native -B /home/spark-advantage/pokefirered-native/build
   cmake --build /home/spark-advantage/pokefirered-native/build -j4
   ```
4. Run:
   ```sh
   /home/spark-advantage/pokefirered-native/build/pfr_smoke
   ```
   Expect:
   ```
   pfr_smoke: ok
   ```
5. If touching decompression, also verify:
   ```sh
   tmpdir=$(mktemp -d)
   printf '\x10\x04\x00\x00\x00ABCD' > "$tmpdir/in.lz77"
   /home/spark-advantage/pokefirered-native/build/pfr_lz77 "$tmpdir/in.lz77" "$tmpdir/out.bin"
   cmp -s "$tmpdir/out.bin" <(printf 'ABCD')
   rm -rf "$tmpdir"
   ```
6. Only after that start the next rehost slice.

## Short Status Summary

Current state in one sentence:
- the native bootstrap can already execute original upstream RNG, heap, decompression, GPU register buffering, DMA3 manager, task scheduler, trig table, scanline-effect HBlank DMA, palette logic, BG logic, sprite core code, the non-skipped `intro.c` callback chain through Scene 3 into natural title handoff, and the real `title_screen.c` path into run-state plus current callback handoff edges under a host-side GBA memory/BIOS/DMA shim layer, but it does not boot the game yet.
