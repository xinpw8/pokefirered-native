# Guardrails

Anti-patterns to avoid when porting upstream code.

## DO NOT

1. **Replace host_renderer.c** — The software PPU is correct. Missing visuals
   are caused by missing *data* (font glyphs, window tiles), not renderer bugs.

2. **Manually enumerate assets** — Use `extract_incbin.py emit-manifest` to
   derive the canonical asset list from INCBIN macros. Manual enumeration is
   error-prone and goes stale.

3. **Port assembly files** — Only port C translation units. ASM files need
   different handling (not in scope for this pipeline).

4. **Break pfr_smoke** — Every change must keep the smoke test passing.
   Run it before committing.

5. **Skip the extraction pipeline** — Don't copy-paste asset data or
   hand-write .inc files. The pipeline exists to be reproducible.

6. **Adopt other projects' rendering code** — Don't copy mattneel's Zig/raylib
   code. Our renderer targets GBA hardware semantics, not GB.

7. **Over-stub** — When porting a real implementation, remove the old stub
   completely. Don't leave dead stubs that shadow real code.

8. **Ignore font initialization** — `SetFontsPointer(&gFontInfos[0])` must
   be called after `InitHeap()` in every entry point (pfr_play, render_test, smoke).

## DO

1. **Use the pipeline** — extract_incbin -> pfr_assets batch -> geninc -> preproc
   Canonical batch mode copies exact upstream INCBIN binaries and asks upstream
   `make` to materialize anything that is not already present.
2. **Remove old stubs** when real implementations are ported
3. **Test incrementally** — port one TU at a time, verify after each
4. **Check manifests and golden frames** — `pfr_verify_asset_manifests`, then
   `pfr_golden_check` after significant changes
5. **Document new TUs** in PORTING_MANIFEST.md
