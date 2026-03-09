#!/bin/bash
# capture_golden_frames.sh — Canonical golden refresh using pinned mGBA + ROM

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
GOLDEN_DIR="$PROJECT_DIR/golden_frames"
MANIFEST="$GOLDEN_DIR/manifest.txt"
CAPTURE_TOOL="$PROJECT_DIR/build/mgba_capture"
ROM="${1:-$PROJECT_DIR/third_party/pokefirered/pokefirered.gba}"

sha_file() {
    if command -v sha1sum >/dev/null 2>&1; then
        sha1sum "$1" | awk '{print $1}'
    else
        shasum "$1" | awk '{print $1}'
    fi
}

json_escape() {
    printf '%s' "$1" | sed 's/\\/\\\\/g; s/"/\\"/g'
}

if [ ! -f "$ROM" ]; then
    echo "ERROR: ROM not found at $ROM"
    exit 1
fi

if [ ! -f "$MANIFEST" ]; then
    echo "ERROR: Manifest not found at $MANIFEST"
    exit 1
fi

if [ ! -x "$CAPTURE_TOOL" ]; then
    echo "ERROR: mgba_capture not found at $CAPTURE_TOOL"
    echo "Build it with: cmake --build build --target mgba_capture"
    exit 1
fi

INPUT_FILE="$(awk '!/^#/ && NF >= 3 {print $3; exit}' "$MANIFEST")"
if [ -z "$INPUT_FILE" ]; then
    echo "ERROR: could not determine input script from $MANIFEST"
    exit 1
fi
INPUT_PATH="$PROJECT_DIR/$INPUT_FILE"
if [ ! -f "$INPUT_PATH" ]; then
    echo "ERROR: input script not found at $INPUT_PATH"
    exit 1
fi

TMPDIR="$(mktemp -d)"
trap 'rm -rf "$TMPDIR"' EXIT
CAPTURE_DIR="$TMPDIR/capture"
ROM_COPY="$TMPDIR/reference.gba"
mkdir -p "$CAPTURE_DIR"
cp "$ROM" "$ROM_COPY"
rm -f "$TMPDIR/reference.sav"

echo "=== Golden Frame Capture ==="
echo "ROM:        $ROM"
echo "Manifest:   $MANIFEST"
echo "Input:      $INPUT_PATH"
echo "Output dir: $GOLDEN_DIR"
echo ""

"$CAPTURE_TOOL" "$ROM_COPY" "$MANIFEST" "$INPUT_FILE" "$CAPTURE_DIR"

find "$GOLDEN_DIR" -maxdepth 1 -name '*.ppm' -delete
cp "$CAPTURE_DIR"/*.ppm "$GOLDEN_DIR"/

POKEFIRERED_COMMIT="$(git -C "$PROJECT_DIR/third_party/pokefirered" rev-parse HEAD)"
MGBA_COMMIT="$(git -C "$PROJECT_DIR/third_party/mgba" rev-parse HEAD)"
AGBCC_COMMIT="$(git -C "$PROJECT_DIR/third_party/agbcc" rev-parse HEAD)"
ROM_SHA1="$(sha_file "$ROM")"
INPUT_SHA1="$(sha_file "$INPUT_PATH")"

cat > "$GOLDEN_DIR/metadata.json" <<EOF
{
  "pokefirered_commit": "$(json_escape "$POKEFIRERED_COMMIT")",
  "mgba_commit": "$(json_escape "$MGBA_COMMIT")",
  "agbcc_commit": "$(json_escape "$AGBCC_COMMIT")",
  "reference_rom_sha1": "$(json_escape "$ROM_SHA1")",
  "input_script": "$(json_escape "$INPUT_FILE")",
  "input_script_sha1": "$(json_escape "$INPUT_SHA1")",
  "frame_numbering": "frame 1 is the first completed post-reset frame"
}
EOF

echo ""
echo "=== Capture complete ==="
ls -1 "$GOLDEN_DIR"/*.ppm
echo "$GOLDEN_DIR/metadata.json"
