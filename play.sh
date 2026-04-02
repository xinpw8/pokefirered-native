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

# Use save file + skip-intro if a save exists, otherwise start fresh from title
EXTRA_ARGS=""
if [ -f "./pokefirered_recovered.sav" ]; then
    EXTRA_ARGS="--load-save ./pokefirered_recovered.sav --skip-intro"
fi

exec build/pfr_play $EXTRA_ARGS "$@"
