# Asset Guide

How binary assets flow from upstream source to compiled native code.

## Asset Flow

```
upstream C TUs
    | extract_incbin.py emit-manifest
    | output: .project/assets/boot_path.manifest + build/asset_manifests/*.manifest
    | pfr_assets batch: copy exact INCBIN binaries or ask upstream make to build them
    | output: build/assets/...
    | pfr_assets geninc: wrap in C initializer format
    | output: build/inc/...
    | pfr_assets preproc: replace INCBIN_U*("graphics/...") with #include
    | output: build/gen/<file>.c
    | gcc compiles the preprocessed .c file
```

## Canonical Sources

- If the exact INCBIN asset already exists in `third_party/pokefirered`, batch mode
  copies it directly.
- If the INCBIN asset does not exist yet, batch mode invokes upstream `make` for
  that exact target path and then copies the resulting binary.
- The standalone `png2gba`, `png2pal`, `pal2gba`, and `lz77` commands remain as
  developer utilities only. They are not the canonical asset path.

## INCBIN Macro Variants

| Macro | Element Size | .inc Suffix |
|-------|-------------|-------------|
| INCBIN_U8("path") | uint8_t | .u8.inc |
| INCBIN_U16("path") | uint16_t | .u16.inc |
| INCBIN_U32("path") | uint32_t | .u32.inc |

## Adding New Assets

1. Regenerate the manifest from the upstream TU set:
   `python3 tools/extract_incbin.py emit-manifest ... --output .project/assets/boot_path.manifest`
2. Verify the checked-in manifest:
   `cmake --build build --target pfr_verify_asset_manifests`
3. Let `pfr_assets batch` materialize the exact binaries from upstream.

## Directory Structure

```
build/
├── asset_manifests/   # Generated deterministic manifests used by the build
├── assets/          # Converted binary assets
│   └── ...            # Mirrors stripped graphics/ paths from INCBIN assets
├── inc/             # Generated .inc files (u8/u16/u32 variants)
│   └── (mirrors assets/ structure)
└── gen/             # Preprocessed .c files
    ├── intro.c
    ├── title_screen.c
    ├── oak_speech.c
    ├── text_window_graphics.c
    ├── text.c
    └── new_menu_helpers.c
```
