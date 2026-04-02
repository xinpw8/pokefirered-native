#!/bin/bash
set -e
ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

# Auto-clean stale build: if generated headers contain absolute paths
# from a different directory (copied from another machine/location), nuke build/
if [ -d "build/generated_include" ]; then
    if grep -rq '#include.*"/home/' build/generated_include/ build/gen/ 2>/dev/null; then
        echo "Stale absolute paths detected in build/ -- cleaning..."
        rm -rf build
    fi
fi

# Bootstrap if no build or missing pipeline stamp
if [ ! -f "build/gen/.asset_pipeline.stamp" ]; then
    echo "Bootstrapping..."
    ./bootstrap.sh
    cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
fi

cmake --build build --target pfr_play -j$(nproc) 2>&1

# Use save file + skip-intro if a save exists, otherwise start fresh
EXTRA_ARGS=""
for sav in pokefirered_recovered.sav pokefirered.sav; do
    if [ -f "./$sav" ]; then
        EXTRA_ARGS="--load-save ./$sav --skip-intro"
        break
    fi
done

exec build/pfr_play $EXTRA_ARGS "$@"
