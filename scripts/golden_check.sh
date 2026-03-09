#!/bin/bash
# golden_check.sh — Run pfr_render_test and diff each milestone against golden frames
#
# Usage:
#   ./scripts/golden_check.sh <pfr_render_test_bin> <pfr_frame_diff_bin> <golden_dir> <manifest>
#
# Exit 0 only if every milestone is compared and matches exactly.

set -euo pipefail

RENDER_TEST="$1"
FRAME_DIFF="$2"
GOLDEN_DIR="$3"
MANIFEST="$4"

TMPDIR="$(mktemp -d)"
trap 'rm -rf "$TMPDIR"' EXIT

echo "=== pfr_golden_check ==="
echo "render_test: $RENDER_TEST"
echo "frame_diff:  $FRAME_DIFF"
echo "golden_dir:  $GOLDEN_DIR"
echo "manifest:    $MANIFEST"
echo "output_dir:  $TMPDIR"
echo ""

# Step 1: Run render_test to produce frames
echo "--- Running pfr_render_test ---"
if ! "$RENDER_TEST" "$TMPDIR"; then
    echo "FAIL: pfr_render_test exited with error"
    exit 1
fi
echo ""

# Step 2: For each milestone in the manifest, diff against golden
echo "--- Diffing milestones ---"
PASS=0
FAIL=0
MISSING=0

while IFS= read -r line; do
    # Skip comments and blank lines
    trimmed="${line%%#*}"
    trimmed="$(echo "$trimmed" | xargs)"
    [ -z "$trimmed" ] && continue

    # Parse: milestone_name frame_number input_file
    read -r name frame input_file <<< "$trimmed"

    golden="$GOLDEN_DIR/${name}.ppm"
    actual="$TMPDIR/${name}.ppm"

    if [ ! -f "$golden" ]; then
        echo "FAIL: $name (missing golden frame at $golden)"
        MISSING=$((MISSING + 1))
        continue
    fi

    if [ ! -f "$actual" ]; then
        echo "FAIL: $name (missing rendered frame at $actual)"
        MISSING=$((MISSING + 1))
        continue
    fi

    diff_img="$TMPDIR/${name}_diff.ppm"
    if "$FRAME_DIFF" "$golden" "$actual" --diff-image "$diff_img"; then
        PASS=$((PASS + 1))
    else
        echo "  diff image: $diff_img"
        FAIL=$((FAIL + 1))
    fi
done < "$MANIFEST"

echo ""
echo "=== Results ==="
echo "  PASS: $PASS"
echo "  FAIL: $FAIL"
echo "  MISSING: $MISSING"

if [ "$FAIL" -gt 0 ] || [ "$MISSING" -gt 0 ]; then
    echo ""
    echo "FAIL: $FAIL mismatch(es), $MISSING missing milestone artifact(s)"
    exit 1
fi

if [ "$PASS" -eq 0 ]; then
    echo ""
    echo "FAIL: No milestones were compared"
    exit 1
fi

echo ""
echo "OK: All $PASS milestone(s) match golden frames"
exit 0
