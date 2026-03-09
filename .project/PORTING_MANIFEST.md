# Porting Manifest

Track every upstream TU (translation unit) from pret/pokefirered.

## Status Key
- **Done** — Compiled and linked, pfr_smoke passes
- **Preprocessed** — Needs INCBIN preprocessing via pfr_assets (has binary asset dependencies)
- **TODO** — Not yet ported
- **N/A** — Host-owned, not from upstream

## Compiled Unchanged (direct from upstream src/)

| File | Status | Notes |
|------|--------|-------|
| random.c | Done | |
| malloc.c | Done | |
| decompress.c | Done | |
| gpu_regs.c | Done | |
| dma3_manager.c | Done | |
| task.c | Done | |
| trig.c | Done | |
| scanline_effect.c | Done | |
| palette.c | Done | |
| blend_palette.c | Done | |
| bg.c | Done | |
| sprite.c | Done | |
| window.c | Done | |
| blit.c | Done | |
| easy_chat.c | Done | |
| trainer_fan_club.c | Done | |
| pokemon_size_record.c | Done | |
| money.c | Done | |
| berry_powder.c | Done | |
| mystery_gift.c | Done | |
| renewable_hidden_items.c | Done | |
| roamer.c | Done | |
| clear_save_data_screen.c | Done | |
| berry_fix_program.c | Done | |
| main_menu.c | Done | |
| new_game.c | Done | Preprocessed rename: ResetMenuAndMonGlobals, Sav2_ClearSetDefault |
| play_time.c | Done | Preprocessed rename: PlayTimeCounter_Update |

## Preprocessed (INCBIN assets resolved by pfr_assets)

| File | Status | Notes |
|------|--------|-------|
| intro.c | Done | ~50 assets (sprites, tilemaps, palettes) |
| title_screen.c | Done | ~19 assets (logo, box art, background) |
| oak_speech.c | Done | ~19 assets (portraits, platform, pikachu) |
| text_window_graphics.c | TODO | ~28 INCBINs (window tiles + palettes) |
| text.c | TODO | ~15 INCBINs (fonts, arrow tiles) |
| new_menu_helpers.c | TODO | ~3 INCBINs (menu window gfx, std palette) |

## Direct Compile (no INCBINs)

| File | Status | Notes |
|------|--------|-------|
| text_window.c | TODO | Window border drawing, LoadStdWindowGfx |
| main.c | Done | Compiled as pfr_upstream_runtime (MODERN=0) |

## Host-Owned (not from upstream)

| File | Purpose |
|------|---------|
| host_renderer.c | Software PPU (scanline renderer) |
| host_memory.c | VRAM/OAM/PLTT allocation |
| host_dma.c | DMA3 stub (CPU memcpy) |
| host_display.c | SDL2 display backend |
| host_frame_step.c | Per-scanline frame stepping |
| host_crt0.c | CRT0 initialization |
| host_bios.c | BIOS call stubs |
| host_debug.c | Debug utilities |
| host_intro_stubs.c | Intro scene stubs (sound, save, link) |
| host_new_game_stubs.c | New game state stubs |
| host_oak_speech_stubs.c | Oak speech stubs (text, sprites, sound) |
| host_title_screen_stubs.c | Title screen stubs (menus, text printers) |
| upstream_stubs.c | Miscellaneous global stubs |
