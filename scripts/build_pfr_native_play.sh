#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="${ROOT_DIR}/build/pfr_native"
SDL_CFLAGS="$(pkg-config --cflags sdl2)"
SDL_LIBS="$(pkg-config --libs sdl2)"

mkdir -p "${OUT_DIR}"

python3 "${ROOT_DIR}/tools/gen_pfr_native_data.py" \
  --repo-root "${ROOT_DIR}" \
  --output-c "${OUT_DIR}/pfr_native_data.c"

cc -std=c11 -Wall -Wextra -Werror -I"${ROOT_DIR}/src" \
  -c "${ROOT_DIR}/src/pfr_native.c" \
  -o "${OUT_DIR}/pfr_native.o"

cc -std=c11 -Wall -Wextra -Werror -I"${ROOT_DIR}/src" \
  -c "${OUT_DIR}/pfr_native_data.c" \
  -o "${OUT_DIR}/pfr_native_data.o"

cc -std=c11 -Wall -Wextra -Werror -I"${ROOT_DIR}/src" ${SDL_CFLAGS} \
  "${ROOT_DIR}/src/pfr_native_play.c" \
  "${OUT_DIR}/pfr_native.o" \
  "${OUT_DIR}/pfr_native_data.o" \
  ${SDL_LIBS} \
  -o "${OUT_DIR}/pfr_native_play"

printf '%s\n' "${OUT_DIR}/pfr_native_play"
