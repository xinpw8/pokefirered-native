# Agent Protocol

How the porting agent should approach tasks.

## Workflow

1. **Check manifest** — Read PORTING_MANIFEST.md to see what's done/TODO
2. **Pick next TU** — Choose the next TODO file, prefer simpler ones first
3. **Run extraction** — `python3 tools/extract_incbin.py` on the target file
4. **Refresh asset manifests** — Regenerate the checked-in manifest with `extract_incbin.py emit-manifest`
5. **Verify manifests** — Run `pfr_verify_asset_manifests` before relying on the asset pipeline
6. **Update pipeline** — Ensure geninc + preproc handle the new file
7. **Add to CMake** — Create OBJECT library or add to pfr_upstream_core
8. **Fix stubs** — Remove conflicting stubs, add new minimal stubs
9. **Build** — `cmake --build build`
10. **Refresh oracle if needed** — `cmake --build build --target pfr_refresh_goldens`
11. **Test** — Run `pfr_smoke`, then `pfr_golden_check`
12. **Update manifest** — Mark TU as Done in PORTING_MANIFEST.md

## Stub Removal Checklist

When porting a TU that provides real implementations:
- Search all `host_*_stubs.c` files for the function name
- Remove the stub definition
- Remove any tracking counters if no longer needed
- Keep host-specific tracking in pfr_play.c/render_test.c if still useful

## Decision Framework

- **Function is in upstream TU being ported** -> Remove stub, use real impl
- **Function is in a different upstream TU (not yet ported)** -> Keep/add stub
- **Function is hardware-specific (sound, link, flash)** -> Always stub
- **Function is host-specific (display, memory mapping)** -> Keep in host_*.c
