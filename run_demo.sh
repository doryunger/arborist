#!/usr/bin/env bash
# Start the Arborist live NPC demo with the real-time tree viewer.
#
# Builds the project if the binary is not already present, then launches a
# simulated guard NPC that ticks continuously while streaming its behavior
# tree state to a browser-based monitor.
#
# Usage:
#   ./run_demo.sh          — start on default port 8080
#   ./run_demo.sh 9000     — start on a custom port

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
BINARY="$BUILD_DIR/examples/bt_demo"

# ── Build if needed ───────────────────────────────────────────────────────────
if [ ! -f "$BINARY" ]; then
    echo "Binary not found — building bt_demo..."
    cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
    cmake --build "$BUILD_DIR" --target bt_demo -j"$(nproc 2>/dev/null || sysctl -n hw.logicalcpu)"
fi

exec "$BINARY" "$@"
