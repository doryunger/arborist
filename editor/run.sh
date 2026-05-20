#!/usr/bin/env bash
# Start the Arborist visual editor.
#
# Builds the project if the binary is not already present, then launches the
# editor.  On first run the registry is created empty; on subsequent runs it
# picks up where you left off.
#
# Usage:
#   ./run.sh                                   default paths, port 8081
#   ./run.sh --port 9000                       custom port
#   ./run.sh --db my.db --schema my.yaml       custom files
#   ./run.sh --help                            show all options

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$REPO_ROOT/build"
BINARY="$BUILD_DIR/editor/bt_editor"

# ── Build if needed ───────────────────────────────────────────────────────────
if [ ! -f "$BINARY" ]; then
    echo "Binary not found — building bt_editor..."
    cmake -S "$REPO_ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
    cmake --build "$BUILD_DIR" --target bt_editor -j"$(nproc 2>/dev/null || sysctl -n hw.logicalcpu)"
fi

# ── Run from editor dir so db/schema files land here by default ───────────────
cd "$SCRIPT_DIR"
exec "$BINARY" "$@"
