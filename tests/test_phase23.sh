#!/usr/bin/env bash
# Phase 23 integration tests — agent --dry-run, chat --no-llm, embed --status,
# budget --html, recall --semantic fallback.
# Drives the built `icmg` binary in a temp project; verifies stdout/files.
set -uo pipefail

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

# ===== embed --status (no Python required) ================================
STATUS=$("$ICMG" embed --status 2>&1 || true)
echo "$STATUS" | grep -qE "Embeddings: memory=" \
    && pass "embed --status reports counts" \
    || fail "embed --status reports counts" "$STATUS"

# ===== agent --dry-run (no LLM call) ======================================
DRY=$("$ICMG" agent --dry-run "test task xyz" 2>&1 || true)
echo "$DRY" | grep -qE "## Task" \
    && pass "agent --dry-run emits prompt" \
    || fail "agent --dry-run emits prompt" "${DRY:0:200}"

echo "$DRY" | grep -qF "test task xyz" \
    && pass "agent --dry-run includes task text" \
    || fail "agent --dry-run includes task text" "${DRY:0:200}"

# ===== recall --semantic fallback (no embeddings, no Python) ==============
"$ICMG" store "decisions-test" "Use BM25 freq log(2+f) for recallability" --force >/dev/null 2>&1 || true
SEM=$("$ICMG" recall "BM25" --semantic --alpha 1.0 2>&1 || true)
echo "$SEM" | grep -qiE "BM25|freq|log" \
    && pass "recall --semantic alpha=1 (BM25 path)" \
    || fail "recall --semantic alpha=1 (BM25 path)" "${SEM:0:200}"

SEM2=$("$ICMG" recall "frequency" --semantic --alpha 0.5 2>&1 || true)
# Should not crash (graceful fallback when no embedder).
echo "$SEM2" | grep -qiE "BM25|freq|No results" \
    && pass "recall --semantic alpha=0.5 graceful no-embedder" \
    || fail "recall --semantic alpha=0.5 graceful no-embedder" "${SEM2:0:200}"

# ===== budget --html writes file =========================================
"$ICMG" budget --html --out "$TMPDIR/b.html" >/dev/null 2>&1 || true
if [ -f "$TMPDIR/b.html" ]; then
    grep -qE "<html|<table|<head|icmg" "$TMPDIR/b.html" \
        && pass "budget --html writes valid html" \
        || fail "budget --html writes valid html" "missing markers"
else
    fail "budget --html writes valid html" "file not created"
fi

# ===== chat --no-llm sandbox (single turn via heredoc) ====================
# Send one user line + \quit; expect bundled prompt or graceful exit.
CHAT_OUT=$(printf 'hello world\n\\quit\n' | "$ICMG" chat --no-llm --no-pack 2>&1 || true)
echo "$CHAT_OUT" | grep -qiE "icmg chat|hello|exit" \
    && pass "chat --no-llm runs interactive loop" \
    || fail "chat --no-llm runs interactive loop" "${CHAT_OUT:0:200}"

popd >/dev/null

echo ""
echo "  Phase 23: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
