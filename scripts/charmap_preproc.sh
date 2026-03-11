#!/bin/bash
# charmap_preproc.sh — Apply Pokemon character encoding to _()/__() strings
#
# Usage: charmap_preproc.sh <preproc_bin> <charmap_file> <file1> [file2 ...]
#
# Runs the upstream preproc tool first for generated source files, then a
# Python cleanup pass that catches any remaining _()/__() macros the upstream
# tool leaves behind. Header overlays are processed in-place by the Python pass
# only because upstream preproc refuses .h inputs.

set -euo pipefail

PREPROC="$1"
CHARMAP="$2"
shift 2
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PY_CHARMAP="${SCRIPT_DIR}/../tools/preproc_charmap.py"

for f in "$@"; do
    if [ ! -f "$f" ]; then
        echo "charmap_preproc: skip missing $f" >&2
        continue
    fi
    TMPFILE="${f}.charmap_tmp"
    TMPFILE2="${f}.charmap_tmp2"
    case "${f##*.}" in
        h)
            cp "$f" "$TMPFILE"
            ;;
        *)
            "$PREPROC" "$f" "$CHARMAP" > "$TMPFILE"
            ;;
    esac
    python3 "$PY_CHARMAP" "$CHARMAP" "$TMPFILE" "$TMPFILE2"
    mv "$TMPFILE2" "$f"
    rm -f "$TMPFILE"
done

echo "  charmap_preproc: processed $# files"
