#!/usr/bin/env bash
# Start the Arborist visual editor playground.
#
# Builds the project if the binary is not already present, then launches the
# editor with the City Security Guard NPC scenario pre-loaded.
#
# Usage:
#   ./run.sh          — start on default port 8081
#   ./run.sh 9000     — start on a custom port

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$REPO_ROOT/build"
BINARY="$BUILD_DIR/playground/bt_playground"

# ── Build if needed ───────────────────────────────────────────────────────────
if [ ! -f "$BINARY" ]; then
    echo "Binary not found — building bt_playground..."
    cmake -S "$REPO_ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
    cmake --build "$BUILD_DIR" --target bt_playground -j"$(nproc 2>/dev/null || sysctl -n hw.logicalcpu)"
fi

# ── Run from playground dir so schema/db files land here ──────────────────────
cd "$SCRIPT_DIR"
exec "$BINARY" "$@"
