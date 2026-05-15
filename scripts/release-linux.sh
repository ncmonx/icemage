#!/usr/bin/env bash
# release-linux.sh — build + package icmg Linux x64 release archive.
#
# Run inside WSL2 Ubuntu (or any glibc Linux). Produces:
#   /tmp/icmg-<VERSION>-linux-x64.tar.gz
#   /tmp/icmg-<VERSION>-linux-x64.tar.gz.sha256
#
# After running, manually upload both files to the public GH release:
#   gh release upload v<VERSION> /tmp/icmg-<VERSION>-linux-x64.tar.gz \
#     /tmp/icmg-<VERSION>-linux-x64.tar.gz.sha256 \
#     --repo ncmonx/icm-graph
#
# First-time setup:
#   sudo apt update
#   sudo apt install -y cmake ninja-build build-essential zlib1g-dev curl git

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

# 1. Detect version from CMakeLists.txt.
VERSION=$(grep -E '^project\(icmg VERSION' CMakeLists.txt | sed -E 's/.*VERSION ([0-9.]+).*/\1/')
if [[ -z "$VERSION" ]]; then
    echo "release-linux: failed to detect version from CMakeLists.txt" >&2
    exit 1
fi
echo "==> Building icmg v$VERSION for Linux x64..."

# 2. Clean configure (Release + ONNX + tree-sitter both ON).
rm -rf build_linux
cmake -B build_linux -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DICMG_USE_ONNX=ON \
    -DICMG_USE_TREESITTER=ON

# 3. Build.
cmake --build build_linux --parallel

# 4. Verify with ctest. Refuses to package on any failure.
ctest --test-dir build_linux --output-on-failure

# 5. Stage binary + ORT runtime libs.
PKG_DIR="/tmp/icmg-pkg-$VERSION-linux"
rm -rf "$PKG_DIR"
mkdir -p "$PKG_DIR"
cp build_linux/icmg "$PKG_DIR/"
chmod +x "$PKG_DIR/icmg"
# Bundle ORT shared libs (preserves symlinks with -P).
cp -RP third_party/onnxruntime/lib/*.so* "$PKG_DIR/" 2>/dev/null || true

# 6. Tarball + sha256.
ARCHIVE="/tmp/icmg-$VERSION-linux-x64.tar.gz"
tar -czf "$ARCHIVE" -C "$PKG_DIR" .
sha256sum "$ARCHIVE" \
    | awk -v ver="$VERSION" '{print $1 "  icmg-" ver "-linux-x64.tar.gz"}' \
    > "$ARCHIVE.sha256"

echo
echo "==> Done."
echo "    Archive: $ARCHIVE"
echo "    SHA256:  $(cat "$ARCHIVE.sha256")"
echo
echo "Upload to public release:"
echo "    gh release upload v$VERSION \\"
echo "      $ARCHIVE $ARCHIVE.sha256 \\"
echo "      --repo ncmonx/icm-graph"
