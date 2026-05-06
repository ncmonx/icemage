#!/usr/bin/env bash
# Phase 14: RTK integration tests
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

# ---- run filters output (git log should be <= raw) -------------------------
pushd "$ROOT" >/dev/null
RAW_LINES=$(git log --oneline -50 2>/dev/null | wc -l || echo 0)
FILTERED_LINES=$("$ICMG" run git log --oneline -50 2>/dev/null | wc -l || echo 0)
[ "$FILTERED_LINES" -le "$RAW_LINES" ] && pass "RTK filters output ($FILTERED_LINES <= $RAW_LINES)" || \
    fail "RTK filter" "filtered=$FILTERED_LINES > raw=$RAW_LINES"
popd >/dev/null

# ---- JSON output valid (in TMPDIR to avoid polluting project DB) -----------
pushd "$ROOT" >/dev/null
JSON=$("$ICMG" run --json git log --oneline -5 2>/dev/null || echo "{}")
echo "$JSON" | grep -q "exit_code\|output\|command" && pass "RTK JSON output" || fail "RTK JSON" "$JSON"

# ---- frequency tracking after runs -----------------------------------------
"$ICMG" run git log --oneline -3 >/dev/null 2>&1 || true
"$ICMG" run git log --oneline -3 >/dev/null 2>&1 || true
SUGGEST=$("$ICMG" cmd suggest git --json 2>/dev/null || echo "{}")
echo "$SUGGEST" | grep -q "git" && pass "cmd suggest after runs" || fail "cmd suggest" "$SUGGEST"
popd >/dev/null

# ---- cmd suggest all -------------------------------------------------------
pushd "$ROOT" >/dev/null
ALL=$("$ICMG" cmd suggest --json 2>/dev/null || echo "{}")
echo "$ALL" | grep -q "commands\|command" && pass "cmd suggest all" || fail "cmd suggest all" "$ALL"
popd >/dev/null

popd >/dev/null

echo ""
echo "  RTK: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
