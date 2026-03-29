#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="${ROOT_DIR}/build/pfr_native/demo"
BIN="$("${ROOT_DIR}/scripts/build_pfr_native_play.sh")"
SCRIPT="A A A R R R R U U U U D D L A"

mkdir -p "${OUT_DIR}"
"${BIN}" --dump-dir "${OUT_DIR}" --script "${SCRIPT}"

python3 - "${OUT_DIR}" <<'PY'
import math
import sys
from pathlib import Path

out_dir = Path(sys.argv[1])
frames = sorted(out_dir.glob("*.ppm"))
if not frames:
    raise SystemExit("no frames")

selected = frames[:12]

def read_ppm(path: Path):
    data = path.read_bytes()
    if not data.startswith(b"P6\n"):
        raise ValueError(f"{path} is not P6 ppm")
    parts = data.split(b"\n", 3)
    _, dims, maxv, pixels = parts
    w, h = [int(x) for x in dims.split()]
    if maxv.strip() != b"255":
        raise ValueError(f"{path} max value is not 255")
    return w, h, pixels

w, h, _ = read_ppm(selected[0])
cols = 4
rows = math.ceil(len(selected) / cols)
sheet_w = cols * w
sheet_h = rows * h
sheet = bytearray([0x18, 0x18, 0x18] * (sheet_w * sheet_h))

for idx, path in enumerate(selected):
    fw, fh, pixels = read_ppm(path)
    if fw != w or fh != h:
        raise ValueError("frame sizes differ")
    col = idx % cols
    row = idx // cols
    for y in range(h):
        src_start = y * w * 3
        src_end = src_start + w * 3
        dst_start = ((row * h + y) * sheet_w + col * w) * 3
        dst_end = dst_start + w * 3
        sheet[dst_start:dst_end] = pixels[src_start:src_end]

sheet_path = out_dir / "contact_sheet.ppm"
sheet_path.write_bytes(f"P6\n{sheet_w} {sheet_h}\n255\n".encode("ascii") + sheet)
print(sheet_path)
PY

printf 'capture written to %s\n' "${OUT_DIR}"
