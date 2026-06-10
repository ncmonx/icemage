#!/bin/sh
# icmg one-line installer (Linux / macOS).
#   curl -fsSL https://raw.githubusercontent.com/ncmonx/icemage/main/scripts/install.sh | sh
# Env overrides: ICMG_VERSION (e.g. 2.1.0), ICMG_BIN_DIR (default ~/.local/bin).
set -eu

REPO="ncmonx/icemage"
BINDIR="${ICMG_BIN_DIR:-$HOME/.local/bin}"

os=$(uname -s)
arch=$(uname -m)
case "$os" in
  Linux)  plat="linux-x64" ;;
  Darwin)
    case "$arch" in
      arm64) plat="macos-arm64" ;;
      *) echo "icmg: unsupported macOS arch '$arch' (only macos-arm64 is published)"; exit 1 ;;
    esac ;;
  *) echo "icmg: unsupported OS '$os' (on Windows use scripts/install.ps1)"; exit 1 ;;
esac

ver="${ICMG_VERSION:-}"
if [ -z "$ver" ]; then
  ver=$(curl -fsSL "https://api.github.com/repos/$REPO/releases/latest" \
        | grep -m1 '"tag_name"' | sed -E 's/.*"v?([^"]+)".*/\1/')
fi
[ -n "$ver" ] || { echo "icmg: could not resolve latest version"; exit 1; }

asset="icmg-${ver}-${plat}.tar.gz"
url="https://github.com/$REPO/releases/download/v${ver}/${asset}"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

echo "icmg: downloading $asset ..."
curl -fsSL "$url" -o "$tmp/$asset"

# Verify checksum when the sidecar + a sha tool are available (best-effort).
if curl -fsSL "$url.sha256" -o "$tmp/$asset.sha256" 2>/dev/null; then
  expected=$(awk '{print $1}' "$tmp/$asset.sha256")
  if command -v sha256sum >/dev/null 2>&1; then
    actual=$(sha256sum "$tmp/$asset" | awk '{print $1}')
  elif command -v shasum >/dev/null 2>&1; then
    actual=$(shasum -a 256 "$tmp/$asset" | awk '{print $1}')
  else
    actual=""
  fi
  if [ -n "$actual" ] && [ "$actual" != "$expected" ]; then
    echo "icmg: SHA256 mismatch (expected $expected, got $actual)"; exit 1
  fi
fi

mkdir -p "$BINDIR"
tar -xzf "$tmp/$asset" -C "$BINDIR"
chmod +x "$BINDIR/icmg" 2>/dev/null || true

echo "icmg: installed $ver to $BINDIR"
case ":$PATH:" in
  *":$BINDIR:"*) ;;
  *) echo "icmg: add to PATH ->  export PATH=\"$BINDIR:\$PATH\"" ;;
esac
"$BINDIR/icmg" --version 2>/dev/null || true
