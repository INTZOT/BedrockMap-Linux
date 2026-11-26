#!/usr/bin/env bash
# Runs a previously built BedrockMap on Linux (counterpart of scripts/run.ps1).
#
#   ./scripts/run.sh              # debug build
#   ./scripts/run.sh --release    # release build
#   ./scripts/run.sh --x11        # force the X11/XWayland backend (see docs/Linux.md)
#   ./scripts/run.sh --wayland    # force the native Wayland backend
#   ./scripts/run.sh -- <args>    # extra arguments are forwarded to the application
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="build"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --release|-r) BUILD_DIR="build_rls"; shift ;;
        --debug|-d)   BUILD_DIR="build";     shift ;;
        # Some Wayland/NVIDIA combinations cannot create the RHI surface the map
        # items need; the X11 backend goes through GLX instead.
        --x11)        export QT_QPA_PLATFORM=xcb; shift ;;
        --wayland)    export QT_QPA_PLATFORM=wayland; shift ;;
        --)           shift; break ;;
        *) break ;;
    esac
done

EXECUTABLE="$PROJECT_ROOT/$BUILD_DIR/BedrockMap"
if [[ ! -x "$EXECUTABLE" ]]; then
    echo "error: $EXECUTABLE not found. Build it first: ./scripts/build.sh" >&2
    exit 1
fi

exec "$EXECUTABLE" "$@"
