#!/bin/bash
set -e
ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

# Bootstrap if build/gen/ doesn't exist yet (fresh clone)
if [ ! -f "build/gen/.asset_pipeline.stamp" ]; then
    echo "First run detected -- bootstrapping..."
    ./bootstrap.sh
    cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
fi

cmake --build build --target pfr_play -j$(nproc) 2>&1

# Use pokefirered_recovered.sav if it exists, otherwise start fresh
SAVE_ARG=""
if [ -f "./pokefirered_recovered.sav" ]; then
    SAVE_ARG="--load-save ./pokefirered_recovered.sav"
fi

exec build/pfr_play $SAVE_ARG --skip-intro "$@"
