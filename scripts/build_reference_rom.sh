#!/bin/bash
set -euo pipefail

AGBCC_DIR="${1:?usage: build_reference_rom.sh <agbcc_dir> <pokefirered_dir>}"
POKEFIRERED_DIR="${2:?usage: build_reference_rom.sh <agbcc_dir> <pokefirered_dir>}"
EXPECTED_SHA1="41cb23d8dccc8ebd7c649cd8fbb58eeace6e2fdc"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
TOOLCHAIN_ROOT="$PROJECT_DIR/build/toolchains/arm-none-eabi"
TOOLCHAIN_BIN="$TOOLCHAIN_ROOT/usr/bin"
LIBPNG_PKGCONFIG="$PROJECT_DIR/build/third_party_install/libpng/lib/pkgconfig"

sha1_file() {
    if command -v sha1sum >/dev/null 2>&1; then
        sha1sum "$1" | awk '{print $1}'
    else
        shasum "$1" | awk '{print $1}'
    fi
}

ensure_binutils() {
    local tmpdir
    local deb

    if [ -x "$TOOLCHAIN_BIN/arm-none-eabi-as" ] && [ -x "$TOOLCHAIN_BIN/arm-none-eabi-ld" ]; then
        return
    fi

    echo "--- Downloading local binutils-arm-none-eabi ---"
    tmpdir="$(mktemp -d)"
    trap 'rm -rf "$tmpdir"' RETURN
    (
        cd "$tmpdir"
        apt download binutils-arm-none-eabi >/dev/null
        deb="$(echo binutils-arm-none-eabi_*.deb)"
        mkdir -p "$TOOLCHAIN_ROOT"
        dpkg-deb -x "$deb" "$TOOLCHAIN_ROOT"
    )
}

echo "=== pfr_reference_rom ==="
echo "agbcc:       $AGBCC_DIR"
echo "pokefirered: $POKEFIRERED_DIR"

ensure_binutils
export PATH="$TOOLCHAIN_BIN:$PATH"
export PKG_CONFIG_PATH="$LIBPNG_PKGCONFIG${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
echo "binutils:    $TOOLCHAIN_BIN"
echo "pkg-config:  $LIBPNG_PKGCONFIG"

if [ ! -x "$AGBCC_DIR/agbcc" ] || [ ! -x "$AGBCC_DIR/old_agbcc" ] || [ ! -x "$AGBCC_DIR/agbcc_arm" ]; then
    echo "--- Building agbcc ---"
    (cd "$AGBCC_DIR" && ./build.sh)
fi

echo "--- Installing agbcc into pokefirered ---"
(cd "$AGBCC_DIR" && ./install.sh "$POKEFIRERED_DIR")

echo "--- Building PNG tools with repo-local libpng ---"
(cd "$POKEFIRERED_DIR/tools/gbagfx" && make LIBS='-lpng -lz -lm' -j"$(nproc)")
(cd "$POKEFIRERED_DIR/tools/rsfont" && make LIBS='-lpng -lz -lm' -j"$(nproc)")

echo "--- Building and verifying pokefirered.gba ---"
(cd "$POKEFIRERED_DIR" && make compare -j"$(nproc)")

ROM_PATH="$POKEFIRERED_DIR/pokefirered.gba"
if [ ! -f "$ROM_PATH" ]; then
    echo "ERROR: reference ROM not found at $ROM_PATH"
    exit 1
fi

ROM_SHA1="$(sha1_file "$ROM_PATH")"
echo "ROM SHA1: $ROM_SHA1"
if [ "$ROM_SHA1" != "$EXPECTED_SHA1" ]; then
    echo "ERROR: expected SHA1 $EXPECTED_SHA1"
    exit 1
fi

echo "OK: reference ROM verified at $ROM_PATH"
