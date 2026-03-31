#!/usr/bin/env bash
#
# bootstrap.sh — Generate all build artifacts needed before cmake configure.
#
# Usage:
#   git clone --recursive https://github.com/xinpw8/pokefirered-native.git
#   cd pokefirered-native
#   ./bootstrap.sh
#   cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
#   cmake --build build --target pfr_play -j$(nproc)
#
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

POKEDIR="$ROOT/third_party/pokefirered"
BUILD="$ROOT/build"
GEN="$BUILD/gen"
GEN_HDR="$BUILD/generated_include"
GEN_NATIVE="$BUILD/generated_native"

echo "=== pokefirered-native bootstrap ==="

# ── 0. Check submodule ──
if [ ! -f "$POKEDIR/src/main.c" ]; then
    echo "Initializing submodules..."
    git submodule update --init --recursive
fi

# ── 1. Build upstream tools ──
echo "[1/7] Building upstream tools..."

# preproc (charmap preprocessor)
if [ ! -x "$POKEDIR/tools/preproc/preproc" ]; then
    echo "  Building preproc..."
    (cd "$POKEDIR/tools/preproc" && g++ -O2 -o preproc *.cpp)
fi

# ── 2. Create directory structure ──
echo "[2/7] Creating directories..."
mkdir -p "$GEN" "$GEN_HDR/constants" "$GEN_HDR/data" "$GEN_NATIVE" \
         "$BUILD/assets" "$BUILD/inc" "$BUILD/asset_manifests"

# ── 3. Copy upstream sources to gen/ ──
echo "[3/7] Copying upstream sources..."
bash scripts/copy_plain_sources.sh "$POKEDIR" "$GEN"

# ── 4. Generate game_ctx files ──
echo "[4/7] Generating GameCtx..."
# Generate game_ctx.h and game_ctx_macros.h headers only (no --transform).
# The stubs files (game_ctx_stubs.c, upstream_stubs.c) are maintained in
# build/gen/ by the full dev pipeline and checked into known-good copies
# via the game_ctx_stubs/ directory.
python3 tools/gen_game_ctx.py \
    --src-dir "$POKEDIR/src" \
    --host-src-dir "$ROOT/src" \
    --out-dir "$GEN_HDR" \
    --gen-dir "$GEN" \
    --inventory-out "$ROOT/ewram_inventory.txt" 2>&1 | tail -3

# Copy known-good stubs/headers if the generator didn't create them
for stub in game_ctx_stubs.c upstream_stubs.c; do
    if [ ! -s "$GEN/$stub" ] && [ -f "$ROOT/src/known_good_stubs/$stub" ]; then
        cp "$ROOT/src/known_good_stubs/$stub" "$GEN/$stub"
    fi
done
for hdr in game_ctx_header_fixups.h; do
    if [ ! -s "$GEN_HDR/$hdr" ] && [ -f "$ROOT/src/known_good_stubs/$hdr" ]; then
        cp "$ROOT/src/known_good_stubs/$hdr" "$GEN_HDR/$hdr"
    fi
done

# ── 5. Generate script/data files ──
echo "[5/7] Generating scripts and data tables..."

# Battle scripts (in src/, committed -- only regenerate if missing)
if [ ! -s "src/upstream_battle_scripts.c" ]; then
    python3 tools/script_assembler.py \
        --pokedir "$POKEDIR" \
        --prefix pfr_bs \
        --patch-fn HostPatchBattleScriptPointers \
        --output src/upstream_battle_scripts.c \
        "$POKEDIR/data/battle_scripts_1.s" "$POKEDIR/data/battle_scripts_2.s" 2>&1 | tail -1
else
    echo "  src/upstream_battle_scripts.c exists, skipping"
fi

# Event scripts (in src/, committed -- only regenerate if missing)
if [ ! -s "src/upstream_event_scripts.c" ]; then
    python3 tools/script_assembler.py \
        --pokedir "$POKEDIR" \
        --prefix pfr_es \
        --patch-fn HostPatchEventScriptPointers \
        --output src/upstream_event_scripts.c \
        "$POKEDIR/data/event_scripts.s" 2>&1 | tail -1
else
    echo "  src/upstream_event_scripts.c exists, skipping"
fi

# Field effect scripts
python3 tools/script_assembler.py \
    --pokedir "$POKEDIR" \
    --prefix pfr_fes \
    --patch-fn HostPatchFieldEffectScriptPointers \
    --exclude-label gFieldEffectScriptPointers \
    --output "$GEN/upstream_field_effect_scripts.c" \
    "$POKEDIR/data/field_effect_scripts.s" 2>&1 | tail -1

# Battle AI scripts
python3 tools/script_assembler.py \
    --pokedir "$POKEDIR" \
    --prefix pfr_bai \
    --patch-fn HostPatchBattleAIScriptPointers \
    --exclude-label gBattleAI_ScriptsTable \
    --output "$GEN/upstream_battle_ai_scripts.c" \
    "$POKEDIR/data/battle_ai_scripts.s" 2>&1 | tail -1

# Battle anim scripts
python3 tools/script_assembler.py \
    --pokedir "$POKEDIR" \
    --prefix pfr_bas \
    --patch-fn HostPatchBattleAnimScriptPointers \
    --exclude-label gMovesWithQuietBGM \
    --exclude-label gBattleAnims_Moves \
    --exclude-label gBattleAnims_StatusConditions \
    --exclude-label gBattleAnims_General \
    --exclude-label gBattleAnims_Special \
    --output "$GEN/upstream_battle_anim_scripts.c" \
    "$POKEDIR/data/battle_anim_scripts.s" 2>&1 | tail -1

# Map data
python3 tools/map_data_to_c.py \
    --pokedir "$POKEDIR" \
    --output "$GEN/upstream_map_data.c" 2>&1 | tail -1

# Battle anim tables
python3 tools/battle_anim_tables_to_c.py \
    --input "$POKEDIR/data/battle_anim_scripts.s" \
    --output "$GEN/upstream_battle_anim_tables.c" 2>&1 | tail -1

# Specials table
python3 tools/specials_to_c.py \
    --input "$POKEDIR/data/specials.inc" \
    --output "$GEN/upstream_specials_table.c" 2>&1 | tail -1

# ── 6. Generate upstream_stubs.c (empty placeholder if not from gen_game_ctx) ──
echo "[6/7] Checking stubs..."
if [ ! -s "$GEN/upstream_stubs.c" ]; then
    # upstream_stubs.c is generated by gen_game_ctx.py --transform mode.
    # If it wasn't created in step 4, create a minimal placeholder.
    if [ -f "$ROOT/src/upstream_stubs.c" ]; then
        cp "$ROOT/src/upstream_stubs.c" "$GEN/upstream_stubs.c"
    else
        echo '/* placeholder */' > "$GEN/upstream_stubs.c"
    fi
fi

# ── 7. Verify ──
echo "[7/7] Verifying..."
MISSING=0
for f in upstream_stubs.c game_ctx_stubs.c upstream_battle_ai_scripts.c \
         upstream_battle_anim_scripts.c upstream_battle_anim_tables.c \
         upstream_field_effect_scripts.c upstream_map_data.c upstream_specials_table.c; do
    if [ ! -s "$GEN/$f" ]; then
        echo "  MISSING: $GEN/$f"
        MISSING=$((MISSING + 1))
    fi
done

GEN_COUNT=$(ls "$GEN"/*.c 2>/dev/null | wc -l)
echo "  $GEN_COUNT source files in $GEN/"

if [ "$MISSING" -gt 0 ]; then
    echo "ERROR: $MISSING required files missing"
    exit 1
fi

echo ""
echo "Bootstrap complete. Now run:"
echo "  cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo"
echo "  cmake --build build --target pfr_play -j\$(nproc)"
