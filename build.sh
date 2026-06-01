#!/usr/bin/env bash
# icemage build script for Linux/macOS (Unix mirror of build.ps1).
#
# Usage:
#   ./build.sh                    # build icmg (default)
#   ./build.sh --target both      # build icmg + icmg_test
#   ./build.sh --target both --run-tests
#   ./build.sh --reconfigure      # wipe top-level CMakeCache, keep third_party
#   ./build.sh --run-tests --test-filter test_curl_bin
#
# Backends default ON (ONNX + tree-sitter). llama/Vulkan is opt-in on Unix:
#   ICMG_USE_LLAMA=ON ./build.sh
set -euo pipefail

TARGET="icmg"          # icmg | test | both
RECONFIGURE=0
RUN_TESTS=0
TEST_FILTER=".*"
BUILD_DIR="build"
JOBS="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"

while [ $# -gt 0 ]; do
  case "$1" in
    --target)        TARGET="$2"; shift 2 ;;
    --reconfigure)   RECONFIGURE=1; shift ;;
    --run-tests)     RUN_TESTS=1; shift ;;
    --test-filter)   TEST_FILTER="$2"; shift 2 ;;
    --build-dir)     BUILD_DIR="$2"; shift 2 ;;
    --jobs)          JOBS="$2"; shift 2 ;;
    -h|--help)       sed -n '2,15p' "$0"; exit 0 ;;
    *) echo "unknown arg: $1" >&2; exit 2 ;;
  esac
done

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

VERSION="$(grep -oE 'ICMG_VERSION = "[0-9.]+"' src/core/version.hpp | grep -oE '[0-9.]+' || echo '?')"
echo "=== icemage v${VERSION} [$(uname -s)] target=${TARGET} jobs=${JOBS} ==="

# Reconfigure = remove only the top-level cache (never third_party builds).
if [ "$RECONFIGURE" -eq 1 ] && [ -f "${BUILD_DIR}/CMakeCache.txt" ]; then
  echo "--- reconfigure: removing ${BUILD_DIR}/CMakeCache.txt ---"
  rm -f "${BUILD_DIR}/CMakeCache.txt"
fi

# Configure (uses the generic Unix preset if available; else inline flags).
if [ ! -f "${BUILD_DIR}/CMakeCache.txt" ]; then
  if cmake --preset linux-native -B "$BUILD_DIR" 2>/dev/null; then
    echo "--- configured via preset linux-native ---"
  else
    echo "--- configured via inline flags (no preset) ---"
    cmake -B "$BUILD_DIR" -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DICMG_USE_ONNX="${ICMG_USE_ONNX:-ON}" \
      -DICMG_USE_TREESITTER="${ICMG_USE_TREESITTER:-ON}" \
      -DICMG_USE_LLAMA="${ICMG_USE_LLAMA:-OFF}"
  fi
fi

# Build targets.
case "$TARGET" in
  icmg) cmake --build "$BUILD_DIR" --target icmg --parallel "$JOBS" ;;
  test) cmake --build "$BUILD_DIR" --target icmg_test --parallel "$JOBS" ;;
  both) cmake --build "$BUILD_DIR" --parallel "$JOBS" ;;
  *) echo "invalid --target: $TARGET" >&2; exit 2 ;;
esac

echo "--- build OK: ${BUILD_DIR}/icmg ---"

# Tests.
if [ "$RUN_TESTS" -eq 1 ]; then
  echo "--- ctest (filter: ${TEST_FILTER}) ---"
  ctest --test-dir "$BUILD_DIR" --output-on-failure -R "$TEST_FILTER"
fi
