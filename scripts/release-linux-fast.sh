#!/usr/bin/env bash
# v1.27.1: Fast Linux release builder — copies source to ext4 native FS
# before build to bypass /mnt/d 9P filesystem bottleneck.
#
# Speedup measured: /mnt/d direct build ~40min → ext4 native ~3-5min.
# Reasons:
#   - 9P FS sequential I/O: 63 MB/s vs ext4 1.1 GB/s (17×)
#   - Small file ops penalty: ~50× on 9P
#   - PCH can be ENABLED (no /mnt/d thrash)
#   - Full parallel possible (no WSL service hang from heavy 9P load)
#
# Usage:
#   wsl -e bash scripts/release-linux-fast.sh
#   # Output: ./icmg-<ver>-linux-x64.tar.gz + .sha256 ready to upload.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

# 1. Detect version.
VERSION=$(grep -E '^project\(icmg VERSION' CMakeLists.txt | sed -E 's/.*VERSION ([0-9.]+).*/\1/')
[[ -z "$VERSION" ]] && { echo "release-linux-fast: version not detected" >&2; exit 1; }
echo "==> Building icmg v$VERSION for Linux x64 (fast path: ext4 native)..."

# 2. rsync source to ext4 staging. Exclude build dirs + git + binary cruft
#    so the copy stays small (~50MB vs full ~3GB repo).
STAGE="/tmp/icmg-build-$VERSION"
# v1.41.x build-speed: keep $STAGE for incremental builds — rsync syncs only
# changed files, ninja recompiles only changed TUs. Force-wipe via FRESH=1.
if [[ -n "${FRESH:-}" ]]; then
    echo "==> FRESH=1 set — wiping $STAGE"
    rm -rf "$STAGE"
fi
mkdir -p "$STAGE"
echo "==> Staging source → $STAGE (ext4 native; delta sync)..."
time rsync -a --delete-excluded \
    --exclude='build/' \
    --exclude='build-linux/' \
    --exclude='build_linux/' \
    --exclude='.git/' \
    --exclude='*.zip' \
    --exclude='*.tar.gz' \
    --exclude='.icmg/' \
    --exclude='node_modules/' \
    "$REPO_ROOT/" "$STAGE/"

# 3. Configure + build in ext4. PCH ON. Full parallel.
cd "$STAGE"
echo "==> Configure (PCH ON, ext4)..."
cmake -B build-linux -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DICMG_USE_ONNX=ON \
    -DICMG_USE_TREESITTER=ON \
    -DICMG_NO_PCH=OFF

echo "==> Build (full parallel = $(nproc))..."
time cmake --build build-linux --parallel "$(nproc)"

# 4. Test.
echo "==> ctest..."
time ctest --test-dir build-linux --output-on-failure -j "$(nproc)"

# 5. Stage binary.
PKG_DIR="$STAGE/dist"
rm -rf "$PKG_DIR"; mkdir -p "$PKG_DIR"
cp build-linux/icmg "$PKG_DIR/"
chmod +x "$PKG_DIR/icmg"
cp -RP third_party/onnxruntime/lib/*.so* "$PKG_DIR/" 2>/dev/null || true

# 6. Tar + sha256 → write back to REPO_ROOT (visible from Windows side).
ARCHIVE="$REPO_ROOT/icmg-$VERSION-linux-x64.tar.gz"
tar -czf "$ARCHIVE" -C "$PKG_DIR" .
sha256sum "$ARCHIVE" \
    | awk -v ver="$VERSION" '{print $1 "  icmg-" ver "-linux-x64.tar.gz"}' \
    > "$ARCHIVE.sha256"

echo
echo "==> Done."
echo "    Archive: $ARCHIVE"
echo "    SHA256:  $(cat "$ARCHIVE.sha256")"
echo "    Staged at: $STAGE (kept for incremental rebuilds; rm -rf to clean)"
echo
echo "Upload to public release:"
echo "    gh release upload v$VERSION \\"
echo "      \"$ARCHIVE\" \"$ARCHIVE.sha256\" \\"
echo "      --repo ncmonx/icm-graph --clobber"
