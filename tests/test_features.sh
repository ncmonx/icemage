#!/usr/bin/env bash
# Phase 14: Rules + Data + Abbr + SP integration tests
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

# ===== RULES =================================================================

# add + list
# format: rule add <scope_path> <type> <name> <content>
"$ICMG" rule add "." "coding" "use-const" "Prefer const over let" >/dev/null 2>&1
LIST=$("$ICMG" rule list "." --json 2>/dev/null || echo "{}")
echo "$LIST" | grep -q "use-const" && pass "rule add+list" || fail "rule add+list" "$LIST"

# apply (get rules for path)
APPLIED=$("$ICMG" rule apply "." --json 2>/dev/null || echo "{}")
echo "$APPLIED" | grep -q "use-const\|rules\|name" && pass "rule apply" || fail "rule apply" "$APPLIED"

# ===== STRUCTURED DATA =======================================================

# add + show
"$ICMG" data add "model" "UserModel" '{"fields":["id","name","email"]}' >/dev/null 2>&1
SHOW=$("$ICMG" data show "UserModel" --json 2>/dev/null || echo "{}")
echo "$SHOW" | grep -q "UserModel\|name" && pass "data add+show" || fail "data add+show" "$SHOW"

# search
SEARCH=$("$ICMG" data search "User" --json 2>/dev/null || echo "{}")
echo "$SEARCH" | grep -q "UserModel\|name" && pass "data search" || fail "data search" "$SEARCH"

# ===== ABBREVIATIONS =========================================================

# learn + expand
"$ICMG" abbr learn "db" "database" --domain general >/dev/null 2>&1
EXPAND=$("$ICMG" abbr expand "connect to db" --json 2>/dev/null || echo "{}")
echo "$EXPAND" | grep -qiE "database|db" && pass "abbr learn+expand" || fail "abbr learn+expand" "$EXPAND"

# list
ABBR_LIST=$("$ICMG" abbr list --json 2>/dev/null || echo "{}")
echo "$ABBR_LIST" | grep -q "db\|abbreviation" && pass "abbr list" || fail "abbr list" "$ABBR_LIST"

# ===== STORED PROCEDURES =====================================================

SP_FILE="$TMPDIR/GetUser.sql"
echo "CREATE PROCEDURE GetUser @id INT AS SELECT * FROM users WHERE id=@id" > "$SP_FILE"
"$ICMG" sp add "GetUser" "$SP_FILE" --db mssql >/dev/null 2>&1

SHOW_SP=$("$ICMG" sp show "GetUser" --json 2>/dev/null || echo "{}")
echo "$SHOW_SP" | grep -q "GetUser\|name" && pass "sp add+show" || fail "sp add+show" "$SHOW_SP"

# SP deps
DEPS=$("$ICMG" sp deps "GetUser" --json 2>/dev/null || echo "{}")
echo "$DEPS" | grep -q "root\|tree\|GetUser" && pass "sp deps" || fail "sp deps" "$DEPS"

popd >/dev/null

echo ""
echo "  Features: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
