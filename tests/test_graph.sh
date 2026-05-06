#!/usr/bin/env bash
# Phase 14: Graph integration tests
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

# ---- graph scan ------------------------------------------------------------
"$ICMG" graph scan "$ROOT/src/" --depth 10 >/dev/null 2>&1
COUNT=$("$ICMG" graph list --json 2>/dev/null | grep -c '"path"' || echo 0)
[ "$COUNT" -gt 0 ] && pass "graph scan creates nodes ($COUNT nodes)" || fail "graph scan" "count=0"

# ---- graph context ---------------------------------------------------------
CONTEXT=$("$ICMG" graph context "$ROOT/src/main.cpp" --json 2>/dev/null || echo "{}")
echo "$CONTEXT" | grep -q "path\|found\|main" && pass "graph context" || fail "graph context" "$CONTEXT"

# ---- path traversal handled gracefully -------------------------------------
set +e
RESULT=$("$ICMG" graph context "../../../../etc/passwd" 2>&1)
EXIT_CODE=$?
set -e
[ "$EXIT_CODE" -le 1 ] && pass "path traversal: no crash (exit=$EXIT_CODE)" || \
    fail "path traversal" "exit=$EXIT_CODE"
echo "$RESULT" | grep -qv "root:x:" && pass "path traversal: passwd not leaked" || \
    fail "path traversal content" "passwd content visible"

# ---- graph list with lang filter -------------------------------------------
CPP_COUNT=$("$ICMG" graph list --lang cpp --json 2>/dev/null | grep -c '"path"' || echo 0)
[ "$CPP_COUNT" -gt 0 ] && pass "graph list --lang cpp ($CPP_COUNT files)" || fail "graph list lang" "no cpp files"

# ---- graph related (requires nodes) ----------------------------------------
RELATED=$("$ICMG" graph related "$ROOT/src/core/db.cpp" --limit 3 --json 2>/dev/null || echo "{}")
echo "$RELATED" | grep -q "related\|nodes\|path\|\[\]" && pass "graph related" || fail "graph related" "$RELATED"

popd >/dev/null

echo ""
echo "  Graph: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
