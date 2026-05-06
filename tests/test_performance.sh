#!/usr/bin/env bash
# Phase 14: Performance benchmark tests
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(dirname "$SCRIPT_DIR")"
ICMG="${ICMG:-$ROOT/build/icmg.exe}"
PASS=0; FAIL=0

pass() { echo "  PASS: $1"; PASS=$((PASS + 1)); }
fail() { echo "  FAIL: $1 — $2"; FAIL=$((FAIL + 1)); }

TMPDIR=$(mktemp -d)
cleanup() { rm -rf "$TMPDIR"; }
trap cleanup EXIT
pushd "$TMPDIR" >/dev/null

ms_now() {
    # Returns milliseconds since epoch
    local t
    t=$(date +%s%3N 2>/dev/null)
    if [ ${#t} -ge 13 ]; then
        echo "$t"
    else
        echo "$(date +%s)000"
    fi
}

time_cmd() {
    local start end
    start=$(ms_now)
    eval "$@" >/dev/null 2>&1 || true
    end=$(ms_now)
    echo $((end - start))
}

# ---- graph scan: should be < 5000ms ----------------------------------------
MS=$(time_cmd "$ICMG" graph scan "$ROOT/src/" --depth 20)
[ "$MS" -lt 5000 ] && pass "graph scan < 5s (${MS}ms)" || fail "graph scan too slow" "${MS}ms"

# ---- recall: should be < 500ms on small DB ---------------------------------
for i in 1 2 3 4 5; do
    "$ICMG" store "topic-$i" "content number $i about databases" >/dev/null 2>&1
done
MS=$(time_cmd "$ICMG" recall "database content" --limit 10)
[ "$MS" -lt 500 ] && pass "recall < 500ms (${MS}ms)" || fail "recall too slow" "${MS}ms"

# ---- cmd suggest: should be < 100ms ----------------------------------------
MS=$(time_cmd "$ICMG" cmd suggest git --json)
[ "$MS" -lt 100 ] && pass "cmd suggest < 100ms (${MS}ms)" || fail "cmd suggest slow" "${MS}ms"

popd >/dev/null

echo ""
echo "  Performance: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
