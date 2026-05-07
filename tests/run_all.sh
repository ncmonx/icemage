#!/usr/bin/env bash
# Master integration test runner for icmg
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(dirname "$SCRIPT_DIR")"
cd "$ROOT"

export ICMG="${ICMG:-./build/icmg.exe}"

# Build first
echo "=== Building ==="
export PATH="/c/msys64/mingw64/bin:/c/Program Files/CMake/bin:$PATH"
cmake -B build -DCMAKE_BUILD_TYPE=Release >/dev/null 2>&1 || true
cmake --build build -- -j4 >/dev/null 2>&1

if [ ! -f "$ICMG" ]; then
    echo "ERROR: $ICMG not found after build"
    exit 1
fi

echo "Binary: $("$ICMG" --version)"
echo ""

# Unit tests first
echo "=== Unit Tests (ctest) ==="
cd build
ctest --output-on-failure -j4 2>&1
cd ..
echo ""

TOTAL_PASS=0
TOTAL_FAIL=0

run_suite() {
    local name="$1" script="$2"
    echo "=== $name ==="
    if bash "$script" 2>&1; then
        TOTAL_PASS=$((TOTAL_PASS + 1))
        echo ""
    else
        TOTAL_FAIL=$((TOTAL_FAIL + 1))
        echo "  Suite $name FAILED"
        echo ""
    fi
}

run_suite "ICM Memory"           "$SCRIPT_DIR/test_icm.sh"
run_suite "Graph"                "$SCRIPT_DIR/test_graph.sh"
run_suite "RTK"                  "$SCRIPT_DIR/test_rtk.sh"
run_suite "Features"             "$SCRIPT_DIR/test_features.sh"
run_suite "MCP Server"           "$SCRIPT_DIR/test_mcp.sh"
run_suite "Security"             "$SCRIPT_DIR/test_security.sh"
run_suite "Performance"          "$SCRIPT_DIR/test_performance.sh"
run_suite "Phase 23"             "$SCRIPT_DIR/test_phase23.sh"

echo "================================================"
echo "Integration suites: $TOTAL_PASS passed, $TOTAL_FAIL failed"

[ "$TOTAL_FAIL" -eq 0 ] && echo "ALL INTEGRATION TESTS PASSED" || echo "SOME SUITES FAILED"
[ "$TOTAL_FAIL" -eq 0 ]
