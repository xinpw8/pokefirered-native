#!/bin/bash
# Copy upstream .c files to gen/ if not already populated
SRC_DIR="$1"
GEN_DIR="$2"
for f in "$SRC_DIR"/src/*.c; do
    bn=$(basename "$f")
    if [ ! -s "$GEN_DIR/$bn" ]; then
        cp "$f" "$GEN_DIR/$bn"
    fi
done
