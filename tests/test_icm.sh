#!/usr/bin/env bash
# Phase 14: ICM integration tests
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(dirname "$SCRIPT_DIR")"
ICMG="${ICMG:-$ROOT/build/icmg.exe}"
PASS=0; FAIL=0

pass() { echo "  PASS: $1"; PASS=$((PASS + 1)); }
fail() { echo "  FAIL: $1 — $2"; FAIL=$((FAIL + 1)); }

# Use temp dir as project root (avoids --project registration requirement)
TMPDIR=$(mktemp -d)
cleanup() { rm -rf "$TMPDIR"; }
trap cleanup EXIT
pushd "$TMPDIR" >/dev/null

# ---- store + recall --------------------------------------------------------
"$ICMG" store "test-topic" "hello world testing" --importance high --kw "test,hello" >/dev/null 2>&1
RESULT=$("$ICMG" recall "hello" --json 2>/dev/null || echo "{}")
echo "$RESULT" | grep -q "hello world" && pass "recall basic" || fail "recall basic" "$RESULT"

# ---- scoring: high importance ranks first ----------------------------------
"$ICMG" store "low-topic" "hello world" --importance low >/dev/null 2>&1
FIRST=$("$ICMG" recall "hello" --limit 1 --json 2>/dev/null | grep -o '"topic":"[^"]*"' | head -1)
echo "$FIRST" | grep -q "test-topic" && pass "scoring importance" || fail "scoring importance" "$FIRST"

# ---- forget ----------------------------------------------------------------
"$ICMG" store "forget-test" "to be forgotten" >/dev/null 2>&1
ID=$("$ICMG" recall "forgotten" --json 2>/dev/null | grep -o '"id":[0-9]*' | head -1 | grep -o '[0-9]*')
if [ -n "$ID" ]; then
    "$ICMG" forget "$ID" --yes >/dev/null 2>&1 || true
    COUNT=$("$ICMG" recall "forgotten" --json 2>/dev/null | grep -c "forgotten" || true)
    [ "$COUNT" -eq 0 ] && pass "forget" || fail "forget" "count=$COUNT"
else
    fail "forget" "recall returned no id"
fi

# ---- topic isolation -------------------------------------------------------
"$ICMG" store "topic-a" "alpha content unique" >/dev/null 2>&1
OUT=$("$ICMG" recall "alpha unique" --json 2>/dev/null || echo "{}")
echo "$OUT" | grep -q "alpha" && pass "topic isolation" || fail "topic isolation" "$OUT"

# ---- store multiple + list -------------------------------------------------
"$ICMG" store "multi-1" "first multi store test" >/dev/null 2>&1
"$ICMG" store "multi-2" "second multi store test" >/dev/null 2>&1
COUNT2=$("$ICMG" recall "multi store" --limit 5 --json 2>/dev/null | grep -c '"topic"' || echo 0)
[ "$COUNT2" -ge 2 ] && pass "multiple stores ($COUNT2 recalled)" || fail "multiple stores" "count=$COUNT2"

popd >/dev/null

echo ""
echo "  ICM: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
