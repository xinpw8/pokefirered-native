# Porting Guide

How to port a new upstream TU (C translation unit) from pret/pokefirered.

## Prerequisites
- Upstream source tree at `third_party/pokefirered/`
- Build directory at `build/`
- Python 3 available

## Step-by-Step Process

### 1. Scan for INCBIN Dependencies
```bash
python3 tools/extract_incbin.py third_party/pokefirered/src/<file>.c \
    --upstream-root third_party/pokefirered
```
This lists all binary assets the file needs (fonts, graphics, palettes).

### 2. Refresh the Canonical Asset Manifest
Regenerate the deterministic manifest from the upstream TU set:
```bash
python3 tools/extract_incbin.py emit-manifest third_party/pokefirered/src/<file>.c \
    --upstream-root third_party/pokefirered \
    --output .project/assets/boot_path.manifest
```
Then verify it:
```bash
cmake --build build --target pfr_verify_asset_manifests
```

### 3. Generate .inc Files
The canonical `pfr_assets batch` + `pfr_assets geninc` pipeline materializes the
exact upstream INCBIN binaries, then converts them to C initializer `.inc` files.

### 4. Preprocess the Source
```bash
pfr_assets preproc <input.c> <inc_dir> <output.c>
```
This replaces `INCBIN_U8/U16/U32("path")` with `{ #include "path.inc" }`.

### 5. Add to CMakeLists.txt
For files with INCBINs: add as a preprocessed OBJECT library (like pfr_upstream_intro).
For files without INCBINs: add directly to pfr_upstream_core.

### 6. Identify Unresolved Symbols
```bash
python3 tools/extract_stubs.py third_party/pokefirered/src/<file>.c \
    --compiled-symbols /tmp/compiled.txt
```
Or just try to compile and fix linker errors.

### 7. Remove Conflicting Stubs
If the upstream TU provides a real implementation of something currently stubbed,
remove the stub from `host_*_stubs.c` files.

### 8. Add Minimal New Stubs
For any new unresolved symbols, write minimal stubs (return 0, no-op, etc.)
in the appropriate `host_*_stubs.c` file.

### 9. Verify
- `pfr_smoke` must still pass
- `pfr_render_test` should show improved rendering
- `pfr_golden_check` for pixel-perfect comparison

## Common Patterns

### INCBIN Resolution
```
upstream: INCBIN_U16("graphics/fonts/latin_normal.latfont")
  -> extract_incbin.py emit-manifest: records the exact INCBIN dependency
  -> pfr_assets batch: copies/builds the exact upstream binary to assets/fonts/latin_normal.latfont
  -> pfr_assets geninc: generates inc/fonts/latin_normal.latfont.u16.inc
  -> pfr_assets preproc: replaces INCBIN with { #include "path.u16.inc" }
```

### Symbol Rename Pattern
When upstream has a function that conflicts with a host stub:
```cmake
target_compile_definitions(pfr_upstream_foo PRIVATE
    ConflictingFunc=UpstreamConflictingFunc
)
```
