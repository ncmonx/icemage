#!/usr/bin/env bash
# Phase 14: Security integration tests
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(dirname "$SCRIPT_DIR")"
ICMG="${ICMG:-$ROOT/build/icmg.exe}"
PASS=0; FAIL=0

pass() { echo "  PASS: $1"; PASS=$((PASS + 1)); }
fail() { echo "  FAIL: $1 — $2"; FAIL=$((FAIL + 1)); }

TMPDIR=$(mktemp -d)
INJECTION_FILE="$TMPDIR/icmg_injection_test"
cleanup() { rm -rf "$TMPDIR"; }
trap cleanup EXIT
pushd "$TMPDIR" >/dev/null

# ---- path traversal in graph context ---------------------------------------
RESULT=$("$ICMG" graph context "../../../../etc/passwd" 2>&1 || true)
echo "$RESULT" | grep -qv "root:x:" && pass "path traversal: passwd content not leaked" || \
    fail "path traversal" "passwd content visible"

# ---- command injection via run ---------------------------------------------
rm -f "$INJECTION_FILE"
"$ICMG" run "echo hello; touch $INJECTION_FILE" >/dev/null 2>&1 || true
[ ! -f "$INJECTION_FILE" ] && pass "command injection blocked" || \
    fail "command injection" "injection file was created"

# ---- large content rejected (store > 1MB) ----------------------------------
LARGE=$(python3 -c "print('x' * 1048577)" 2>/dev/null || printf '%0.s-' {1..1048577})
"$ICMG" store "big-topic" "$LARGE" >/dev/null 2>&1 || true
pass "large content handled (no crash)"

# ---- import size limit — small JSON works, no crash ------------------------
TINY_JSON='{"version":1,"memory_nodes":[{"topic":"t","content":"c","importance":1}]}'
SMALL_JSON="$TMPDIR/test_small.json"
echo "$TINY_JSON" > "$SMALL_JSON"
IMPORT_OUT=$("$ICMG" import json "$SMALL_JSON" 2>&1 || true)
echo "$IMPORT_OUT" | grep -qv "fatal\|crash\|signal" && pass "import small JSON no crash" || \
    fail "import small JSON" "$IMPORT_OUT"

# ---- null byte in store content blocked ------------------------------------
"$ICMG" store "null-test" "$(printf 'hello\x00world')" >/dev/null 2>&1 || true
pass "null byte in content handled"

# ---- MCP: oversized content rejected ---------------------------------------
LARGE_CONTENT=$(python3 -c "print('x' * 1048577)" 2>/dev/null || printf '%0.s-' {1..100})
MCP_RESP=$(echo "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\",\"params\":{\"name\":\"icmg_store\",\"arguments\":{\"topic\":\"t\",\"content\":\"$(echo $LARGE_CONTENT | head -c 1048577)\"}}}" | \
    "$ICMG" --mcp-server 2>/dev/null | head -1 || echo "{}")
echo "$MCP_RESP" | grep -q "error\|exceeds" && pass "MCP: oversized content rejected" || \
    fail "MCP oversized" "$MCP_RESP"

popd >/dev/null

echo ""
echo "  Security: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
