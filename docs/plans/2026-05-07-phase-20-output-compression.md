# Phase 20 — Output Compression & Auto-Summarization

**Goal:** Hard guardrails on tool output size; auto-summarize large files/diffs before they reach Claude.

**Why:** Even with bundle commands, Claude still hits Read on 1000-line files. Compression hooks intercept and shrink without losing semantic content.

**Tech:** Pure C++ (existing icmg) + optional small local model for tree-summarize.

**Estimate:** 2-4 days.

**Assumptions:**
- All compression is opt-in via flag or config; defaults remain compatible.
- Tree-summarize uses heuristic outline (no LLM dependency in tier-1).

---

## Task 1 — `icmg summarize <file>` — heuristic outline

**Files:**
- Create: `src/cli/commands/summarize_cmd.cpp`

**Algorithm:**
1. If symbols indexed (Phase 18): emit symbol tree with signatures only.
2. Else: extract top-level constructs via existing language extractor (classes, fns, top comments).
3. Always include first 5 lines (file header) + last 5 lines (often module exports).

**Output for 800-line file:**
```
# src/Core.cs (800 lines, csharp)
[L1-12]   <file comment block>
class Core (L15-790)
  Path: string
  Init() — L40-78
  Validate(stock) — L82-145
  ...
```

**Flags:** `--max-lines N` (default 60), `--bodies` (include first 3 lines of each fn), `--full-symbols`.

---

## Task 2 — Read interception via PreToolUse hook

**Files:**
- Create: `examples/hooks/icmg-shrink-read.sh`
- Document in: `docs/hooks-recipes.md`

**Hook command:**
```bash
file=$(jq -r '.tool_input.file_path')
sz=$(wc -c < "$file" 2>/dev/null || echo 0)
if [[ $sz -gt 30000 ]]; then
  summary=$(icmg summarize "$file" --max-lines 80)
  jq -n --arg s "$summary" '{hookSpecificOutput:{hookEventName:"PreToolUse",permissionDecision:"deny",permissionDecisionReason:"File large; summary returned. Use Read with offset/limit for specific sections.\n\n\($s)"}}'
fi
```

User opts in by adding to `.claude/settings.local.json`. Saves 80-90% on large file reads.

---

## Task 3 — Diff compression

**Files:**
- Modify: `src/tkil/filters/git_filter.cpp`

Currently filters `git log/diff/status/show` to changed lines + 3-line context. Extend:
- For diffs > 500 lines, prepend symbol-summary header (reuse Phase 19 `diff-summary`).
- For diffs > 2000 lines, switch to summary-only mode automatically.

---

## Task 4 — PostToolUse spill hook

**Files:**
- Create: `examples/hooks/icmg-cap-output.sh`

When ANY Bash output > 8KB:
```bash
out=$(jq -r '.tool_response.stdout')
if [[ ${#out} -gt 8192 ]]; then
  hash=$(echo "$out" | sha1sum | cut -c1-8)
  spill="/tmp/icmg-spill-$hash.txt"
  echo "$out" > "$spill"
  head_part=$(echo "$out" | head -c 4096)
  tail_part=$(echo "$out" | tail -c 2048)
  msg="$head_part\n... [truncated, ${#out} bytes total, full at $spill] ...\n$tail_part"
  jq -n --arg m "$msg" '{hookSpecificOutput:{hookEventName:"PostToolUse",additionalContext:$m}}'
fi
```

---

## Task 5 — Smart abbreviation expansion in store

**Files:**
- Modify: `src/icm/memory_store.cpp::store()`

When inserting content, run through abbreviation engine in *reverse* (long→short): replace common phrases with abbr keys. On `recall`, re-expand. Halves storage for repetitive memory.

**Optional:** behind config flag `icm.compress_on_store=true`.

---

## Task 6 — `icmg budget` — token tracker

**Files:**
- Create: `src/cli/commands/budget_cmd.cpp`
- Schema: `migrations/0009_token_budget.sql` — `tool_invocations(timestamp, tool_name, est_tokens, was_filtered)`

**Tracks:** every `icmg run`, every command via RTK. Estimates tokens (chars/4).

**Output:**
```
=== Token budget (last 24h) ===
Tool          Calls   Est tokens   Filtered savings
icmg run      42      18.2K        43% (-13.7K vs raw)
Bash (raw)    11       8.4K         0%
Read          —        —            (not tracked at icmg level)

Suggestion: 11 raw Bash calls would have saved ~3.5K via icmg run.
```

---

## Task 7 — Auto-consolidation Stop hook

**Files:**
- Create: `examples/hooks/icmg-auto-consolidate.sh`

On Claude Stop, if session ran ≥20 tool calls:
```bash
icmg memory consolidate --dry-run > /tmp/icmg-cons-suggest.txt
# emit systemMessage with suggestion count
```

User accepts → run `icmg memory consolidate` to merge near-duplicates, prune low-frequency stale.

(Requires `consolidate` command from MCP server side; add to icm if missing.)

---

## Task 8 — Optional: tree-summarize via LLM (tier-2)

**Files:**
- Create: `src/llm/local_summarizer.hpp` — interface
- Create: `src/llm/ollama_adapter.cpp` — calls local Ollama HTTP API

For huge files (>5000 lines) where heuristic outline misses semantic intent. Behind flag `--llm-summarize`. Falls back to heuristic if Ollama unreachable.

---

## Verification Checklist

- [ ] `icmg summarize big.cs` returns ≤80 lines.
- [ ] Heuristic outline preserves all top-level classes/fns.
- [ ] Read-shrink hook cancels Read on large files; summary surfaces in deny reason.
- [ ] Spill hook writes /tmp file for >8KB output; head+tail returned to model.
- [ ] `icmg budget` displays per-tool token estimate.
- [ ] Auto-consolidate dry-run emits sane suggestions.
- [ ] All existing tests pass.
- [ ] New tests: `test_summarize`, `test_output_cap`.

---

## Rollback

All hooks user-installed (not built-in). Disable by removing from settings.json. Schema `tool_invocations` is additive.
