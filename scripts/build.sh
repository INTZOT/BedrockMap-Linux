#!/usr/bin/env bash
# Linux build driver for BedrockMap (the counterpart of scripts/build.ps1).
#
#   ./scripts/build.sh                 # debug build into build/
#   ./scripts/build.sh --release       # release build into build_rls/
#   ./scripts/build.sh --clean         # remove the build directory first
#   ./scripts/build.sh --lupdate       # refresh translations/*.ts before building
#   ./scripts/build.sh --run           # build and start the application
#   ./scripts/build.sh -j 8            # limit the number of compile jobs
#
# cmake/ninja and the Qt Linguist tools are resolved from PATH first; the helper
# virtualenv documented in docs/Linux.md is used as a fallback so a machine
# without those packages can still build.
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_TYPE="Debug"
BUILD_DIR="build"
JOBS="$(nproc 2>/dev/null || echo 4)"
DO_CLEAN=0
DO_LUPDATE=0
DO_RUN=0

usage() {
    sed -n '2,13p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
    exit 0
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --release|-r) BUILD_TYPE="Release"; BUILD_DIR="build_rls"; shift ;;
        --debug|-d)   BUILD_TYPE="Debug";   BUILD_DIR="build";     shift ;;
        --clean)      DO_CLEAN=1; shift ;;
        --lupdate)    DO_LUPDATE=1; shift ;;
        --run)        DO_RUN=1; shift ;;
        -j|--jobs)    JOBS="$2"; shift 2 ;;
        -h|--help)    usage ;;
        *) echo "Unknown option: $1" >&2; usage ;;
    esac
done

# --- toolchain -------------------------------------------------------------
# A small virtualenv with cmake/ninja/PySide6 keeps the build reproducible on
# distributions that do not ship those packages (CachyOS/Arch here). It is only
# a fallback: anything already on PATH wins.
TOOLCHAIN_BIN="${BEDROCKMAP_TOOLCHAIN_BIN:-$HOME/.local/venvs/bedrockmap/bin}"
if [[ -d "$TOOLCHAIN_BIN" ]]; then
    PATH="$TOOLCHAIN_BIN:$PATH"
fi
export PATH

command -v cmake >/dev/null 2>&1 || {
    echo "error: cmake not found. Install it (pacman -S cmake ninja) or create the" >&2
    echo "       helper environment described in docs/Linux.md." >&2
    exit 1
}

# --- translations (optional) ------------------------------------------------
# lupdate rewrites the tracked .ts files, so it only runs when asked for.
if [[ "$DO_LUPDATE" == "1" ]]; then
    if command -v lupdate >/dev/null 2>&1; then
        LUPDATE=lupdate
    elif command -v pyside6-lupdate >/dev/null 2>&1; then
        LUPDATE=pyside6-lupdate
    else
        LUPDATE=""
    fi
    if [[ -n "$LUPDATE" ]]; then
        echo "==> updating translations with $LUPDATE"
        (cd "$PROJECT_ROOT" && "$LUPDATE" -no-obsolete -no-ui-lines -recursive ./src \
            -ts translations/zh_CN.ts translations/en.ts)
    else
        echo "warning: lupdate not found, keeping the existing translations/*.ts" >&2
    fi
fi

# --- configure --------------------------------------------------------------
cd "$PROJECT_ROOT"
if [[ "$DO_CLEAN" == "1" ]]; then
    echo "==> removing $BUILD_DIR"
    rm -rf "$BUILD_DIR"
fi

if [[ ! -f "$BUILD_DIR/CMakeCache.txt" ]]; then
    GENERATOR=()
    if command -v ninja >/dev/null 2>&1; then
        GENERATOR=(-G Ninja)
    fi
    echo "==> configuring $BUILD_DIR ($BUILD_TYPE)"
    cmake -S . -B "$BUILD_DIR" "${GENERATOR[@]}" -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
fi

# --- build ------------------------------------------------------------------
echo "==> building with $JOBS jobs"
cmake --build "$BUILD_DIR" --parallel "$JOBS"

echo "==> built $PROJECT_ROOT/$BUILD_DIR/BedrockMap"
if [[ "$DO_RUN" == "1" ]]; then
    exec "$PROJECT_ROOT/scripts/run.sh" $([[ "$BUILD_TYPE" == "Release" ]] && echo --release)
fi
