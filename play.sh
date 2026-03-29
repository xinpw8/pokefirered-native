#!/bin/bash
set -e
ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT/build"
cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo 2>&1 | tail -1
make pfr_play -j$(nproc) 2>&1
cd "$ROOT"
exec build/pfr_play "$@"
