#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="${ROOT_DIR}/build/pfr_world"

mkdir -p "${OUT_DIR}"

python3 "${ROOT_DIR}/tools/gen_pfr_world_data.py" \
  --repo-root "${ROOT_DIR}" \
  --output-json "${OUT_DIR}/world_data.json" \
  --output-warp-report "${OUT_DIR}/warp_report.json" \
  --output-summary "${OUT_DIR}/summary.json" \
  --output-atlas "${OUT_DIR}/world_atlas.ppm"

python3 "${ROOT_DIR}/tests/test_pfr_world_data.py"

printf 'world json: %s\n' "${OUT_DIR}/world_data.json"
printf 'warp report: %s\n' "${OUT_DIR}/warp_report.json"
printf 'summary: %s\n' "${OUT_DIR}/summary.json"
printf 'atlas: %s\n' "${OUT_DIR}/world_atlas.ppm"
