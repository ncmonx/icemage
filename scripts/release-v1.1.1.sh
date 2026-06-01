#!/usr/bin/env bash
# v1.1.1 release — run from MSYS2 MinGW64 shell (NOT Claude Code sandbox).
# Sandbox silently kills c++.exe spawned via ninja/cmake → must run manually.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

VER=1.1.1
PKG="icmg-${VER}-win-x64"
ZIP="${PKG}.zip"

echo "== branch check"
[ "$(git rev-parse --abbrev-ref HEAD)" = "release/v1.1.1" ] \
  || { echo "checkout release/v1.1.1 first"; exit 1; }

echo "== clean configure (Ninja + mingw64)"
rm -rf build
cmake -B build -G Ninja \
  -DCMAKE_MAKE_PROGRAM=C:/msys64/mingw64/bin/ninja.exe \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=C:/msys64/mingw64/bin/cc.exe \
  -DCMAKE_CXX_COMPILER=C:/msys64/mingw64/bin/c++.exe \
  -DICMG_USE_ONNX=ON -DICMG_USE_TREESITTER=ON

echo "== build all (binary + tests)"
cmake --build build --parallel

echo "== ctest"
ctest --test-dir build --output-on-failure

echo "== version check"
./build/icmg.exe --version | grep -q " ${VER}$" \
  || { echo "version mismatch"; exit 1; }

echo "== stage package"
STAGE="$(mktemp -d)/${PKG}"
mkdir -p "$STAGE"
cp build/icmg.exe "$STAGE/"
for dll in \
  /c/msys64/mingw64/bin/libtree-sitter-0.26.dll \
  /c/msys64/mingw64/bin/libwinpthread-1.dll \
  /c/msys64/mingw64/bin/libzstd.dll \
  /c/msys64/mingw64/bin/wasmtime.dll \
  third_party/onnxruntime/lib/onnxruntime.dll \
  third_party/onnxruntime/lib/onnxruntime_providers_shared.dll ; do
  cp "$dll" "$STAGE/"
done

echo "== zip + sha256"
( cd "$(dirname "$STAGE")" && powershell.exe -NoProfile -Command \
  "Compress-Archive -Path '${PKG}\\*' -DestinationPath '${PKG}.zip' -Force" )
mv "$(dirname "$STAGE")/${ZIP}" "./${ZIP}"
HASH=$(powershell.exe -NoProfile -Command "(Get-FileHash '${ZIP}' -Algorithm SHA256).Hash.ToLower()" | tr -d '\r')
printf "%s  %s" "$HASH" "$ZIP" > "${ZIP}.sha256"

echo "== push branch + tag"
git push private release/v1.1.1
git tag -f "v${VER}"
git push origin "v${VER}"

echo "== GH release"
gh release create "v${VER}" \
  --repo ncmonx/icm-graph \
  --title "v${VER}" \
  --notes "Service auto-activate + legacy schtasks cleanup. See CHANGELOG.md." \
  "${ZIP}" "${ZIP}.sha256"

echo "== done: https://github.com/ncmonx/icm-graph/releases/tag/v${VER}"
