#!/usr/bin/env bash
# v1.46.0: Native WSL workspace clone for faster Linux builds.
# Eliminates /mnt/d/ 9P FS bottleneck (~30-60s rsync stat per build).
#
# Usage (run inside WSL):
#   bash /mnt/d/Data\ Kerja/Personal/AI/icm-graph/scripts/setup-wsl-native.sh
#
# Creates ~/icm-graph/ ext4-native clone. release-linux-fast.sh auto-detects
# this path + skips rsync staging entirely.
set -euo pipefail

WIN_REPO="/mnt/d/Data Kerja/Personal/AI/icm-graph"
NATIVE_DIR="$HOME/icm-graph"

if [[ ! -d "$WIN_REPO/.git" ]]; then
    echo "error: Windows-side repo not found at $WIN_REPO" >&2
    exit 1
fi

if [[ -d "$NATIVE_DIR/.git" ]]; then
    echo "==> Native clone exists at $NATIVE_DIR — updating via rsync delta"
    rsync_rc=0
    rsync -a --delete-excluded \
        --exclude='build/' --exclude='build-linux/' --exclude='build-msvc/' \
        --exclude='build-msvc-full/' --exclude='build-clang/' \
        --exclude='.git/' --exclude='*.zip' --exclude='*.tar.gz' \
        --exclude='.icmg/' --exclude='node_modules/' --exclude='*.bak' \
        "$WIN_REPO/" "$NATIVE_DIR/" || rsync_rc=$?
    [[ $rsync_rc -le 24 ]] || exit $rsync_rc
    echo "==> Sync done"
else
    echo "==> Cloning Windows-side repo to native ext4: $NATIVE_DIR"
    mkdir -p "$NATIVE_DIR"
    rsync_rc=0
    rsync -a --delete-excluded \
        --exclude='build/' --exclude='build-linux/' --exclude='build-msvc/' \
        --exclude='build-msvc-full/' --exclude='build-clang/' \
        --exclude='*.zip' --exclude='*.tar.gz' \
        --exclude='.icmg/' --exclude='node_modules/' --exclude='*.bak' \
        "$WIN_REPO/" "$NATIVE_DIR/" || rsync_rc=$?
    [[ $rsync_rc -le 24 ]] || exit $rsync_rc
    echo "==> Initial sync done"
fi

# Marker file for release-linux-fast.sh detection
touch "$NATIVE_DIR/.icmg-native-wsl"

cat <<EOF

Native WSL workspace ready at: $NATIVE_DIR

To build from native (skip rsync 9P bottleneck):
    cd $NATIVE_DIR && SKIP_TESTS=1 bash scripts/release-linux-fast.sh <ver>

To re-sync after Win-side edits:
    bash $WIN_REPO/scripts/setup-wsl-native.sh

Speed gain: 5-10x faster builds (rsync stat-pass eliminated).
EOF
