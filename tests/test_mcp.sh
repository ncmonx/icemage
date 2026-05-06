#!/usr/bin/env bash
# Phase 14: MCP server integration tests
set -euo pipefail

ICMG="${ICMG:-./build/icmg.exe}"
PASS=0; FAIL=0
DB="$(mktemp /tmp/icmg_mcp_$$.XXXXXX)"

pass() { echo "  PASS: $1"; PASS=$((PASS + 1)); }
fail() { echo "  FAIL: $1 — $2"; FAIL=$((FAIL + 1)); }

cleanup() { rm -f "$DB"; }
trap cleanup EXIT

# Send a single JSON-RPC request to the MCP server and capture response.
# The server reads line-by-line; send one line then EOF.
mcp_call() {
    local req="$1"
    echo "$req" | "$ICMG" --mcp-server 2>/dev/null | head -1
}

# ---- initialize ------------------------------------------------------------
INIT=$(mcp_call '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}')
echo "$INIT" | grep -q "serverInfo" && pass "MCP initialize" || fail "MCP initialize" "$INIT"

# ---- tools/list ------------------------------------------------------------
TOOLS=$(mcp_call '{"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}')
echo "$TOOLS" | grep -q "icmg_recall" && pass "MCP tools/list has icmg_recall" || fail "MCP tools/list" "$TOOLS"
echo "$TOOLS" | grep -q "icmg_stats" && pass "MCP tools/list has icmg_stats" || fail "MCP tools/list stats" "$TOOLS"

# ---- tools/call: icmg_recall (empty DB = empty result) ---------------------
RECALL=$(mcp_call '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"icmg_recall","arguments":{"query":"hello","limit":5}}}')
echo "$RECALL" | grep -q "result\|content" && pass "MCP tools/call recall" || fail "MCP tools/call recall" "$RECALL"

# ---- tools/call: icmg_stats ------------------------------------------------
STATS=$(mcp_call '{"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"icmg_stats","arguments":{}}}')
echo "$STATS" | grep -q "memory_nodes\|result" && pass "MCP tools/call stats" || fail "MCP tools/call stats" "$STATS"

# ---- unknown method → error ------------------------------------------------
ERR=$(mcp_call '{"jsonrpc":"2.0","id":5,"method":"unknown/method","params":{}}')
echo "$ERR" | grep -q "error\|-32601" && pass "MCP unknown method → error" || fail "MCP unknown method" "$ERR"

# ---- unknown tool → error --------------------------------------------------
ERR2=$(mcp_call '{"jsonrpc":"2.0","id":6,"method":"tools/call","params":{"name":"icmg_nonexistent","arguments":{}}}')
echo "$ERR2" | grep -q "error\|Unknown" && pass "MCP unknown tool → error" || fail "MCP unknown tool" "$ERR2"

# ---- parse error (bad JSON) ------------------------------------------------
ERR3=$(echo "not json at all" | "$ICMG" --mcp-server 2>/dev/null | head -1)
echo "$ERR3" | grep -q "error\|-32700" && pass "MCP parse error" || fail "MCP parse error" "$ERR3"

echo ""
echo "  MCP: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
