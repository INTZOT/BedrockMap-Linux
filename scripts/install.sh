#!/usr/bin/env bash
# Installs BedrockMap into a prefix (counterpart of the Windows deploy.ps1 for
# local use, without packaging an archive).
#
#   ./scripts/install.sh                  # install into ~/.local (no root needed)
#   ./scripts/install.sh --prefix /usr/local
#   ./scripts/install.sh --release --prefix /usr
#
# The layout produced by cmake --install is:
#   <prefix>/bin/BedrockMap
#   <prefix>/share/BedrockMap/{block_color.json,biome_color.json,translations/}
#   <prefix>/share/applications/BedrockMap.desktop
#   <prefix>/share/icons/hicolor/scalable/apps/BedrockMap.svg
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PREFIX="${HOME}/.local"
BUILD_DIR="build_rls"
BUILD_TYPE="Release"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --prefix)  PREFIX="$2"; shift 2 ;;
        --release) BUILD_DIR="build_rls"; BUILD_TYPE="Release"; shift ;;
        --debug)   BUILD_DIR="build";     BUILD_TYPE="Debug";   shift ;;
        -h|--help) sed -n '2,16p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
done

"$PROJECT_ROOT/scripts/build.sh" $([[ "$BUILD_TYPE" == "Release" ]] && echo --release)

# build.sh resolves cmake for itself, but a child script cannot change this shell's
# PATH, so the same fallback is applied here as well.
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

echo "==> installing into $PREFIX"
cmake --install "$PROJECT_ROOT/$BUILD_DIR" --prefix "$PREFIX"

# Refresh the desktop/icon caches when the prefix is a user-visible one.
if [[ -d "$PREFIX/share/applications" ]] && command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database "$PREFIX/share/applications" >/dev/null 2>&1 || true
fi
if [[ -d "$PREFIX/share/icons/hicolor" ]] && command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache -q -t -f "$PREFIX/share/icons/hicolor" >/dev/null 2>&1 || true
fi

echo "==> done. Make sure $PREFIX/bin is on PATH, then run: BedrockMap"
