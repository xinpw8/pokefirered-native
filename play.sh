#!/bin/bash
set -e
ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"
cmake --build build --target pfr_play -j$(nproc) 2>&1
exec build/pfr_play --load-save ./pokefirered_recovered.sav --skip-intro "$@"
