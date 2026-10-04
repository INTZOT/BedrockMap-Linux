#!/usr/bin/env bash
# Builds a release tree and packs it into a relocatable tarball, mirroring the
# Windows scripts/deploy.ps1. The bundle relies on the distribution Qt 6 runtime
# (the same way the Windows package relies on windeployqt-copied DLLs).
#
#   ./scripts/deploy.sh              # -> dist/BedrockMap-<tag>-linux-x86_64.tar.gz
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_ROOT"

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

VERSION_TAG="$(git describe --tags --abbrev=0 2>/dev/null || true)"
if [[ -z "$VERSION_TAG" ]]; then
    VERSION_TAG="unknown"
    echo "warning: no git tag found, naming the archive with 'unknown'" >&2
fi

ARCH="$(uname -m)"
BUNDLE_NAME="BedrockMap-${VERSION_TAG}-linux-${ARCH}"
DIST_DIR="$PROJECT_ROOT/dist"
BUNDLE_DIR="$DIST_DIR/$BUNDLE_NAME"
BUILD_DIR="build_rls"

# Fresh release build.
./scripts/build.sh --release --clean

rm -rf "$BUNDLE_DIR"
mkdir -p "$BUNDLE_DIR"
cmake --install "$PROJECT_ROOT/$BUILD_DIR" --prefix "$BUNDLE_DIR/usr"

# Convenience launcher so the bundle can be run straight from the unpacked tree.
cat > "$BUNDLE_DIR/BedrockMap" <<'LAUNCHER'
#!/usr/bin/env bash
# Starts the bundled build. Assets are found relative to usr/bin/BedrockMap.
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec "$HERE/usr/bin/BedrockMap" "$@"
LAUNCHER
chmod +x "$BUNDLE_DIR/BedrockMap"

# Keep a copy of the licence and the Linux notes inside the archive.
cp LICENSE "$BUNDLE_DIR/" 2>/dev/null || true
cp docs/Linux.md "$BUNDLE_DIR/" 2>/dev/null || true

ARCHIVE="$DIST_DIR/$BUNDLE_NAME.tar.gz"
tar -C "$DIST_DIR" -czf "$ARCHIVE" "$BUNDLE_NAME"
echo "==> $ARCHIVE"
