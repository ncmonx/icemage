# Phase 19 — Context Bundle Commands

**Goal:** Single-call commands that aggregate file/graph/memory/rules into one compact response — replace 5-10 Read+Grep+Glob calls.

**Why:** Token spike comes from Claude doing exploratory tool chains. Bundle = one tool call returns everything Claude needs to start work.

**Tech:** New CLI commands composing existing stores.

**Estimate:** 2-3 days.

**Assumptions:**
- Phase 17 (zones) and Phase 18 (symbols) are landed. Falls back gracefully if not.
- Output cap: bundle ≤ 4KB by default, configurable via `--max-bytes`.

---

## Task 1 — `icmg context <file>`

**Files:**
- Create: `src/cli/commands/context_cmd.cpp`

**Output (text or `--json`):**
```
File: src/Sync/SyncOrder.cs (245 lines, 8 KB, lang=csharp, zone=sync)
Imports:    Core, Utama, Setting
Used by:    ApiClient.cs, Program.cs
Symbols:    SyncOrder (class L10-240)
            ├── EnsureStockRegistered  L64-98
            ├── ProcessOrder           L101-160
            └── Validate               L163-180
Companions: SyncOrder.Designer.cs, SyncOrder.resx (group_id=...)
Recent:     edited 2h ago, scanned 5m ago
Top context: "Sync orders to ERP. Validates stock before posting."
Related memory:
  [3.2] errors-resolved: SyncOrder.ProcessOrder threw NullRef when stock=0
```

**Flags:** `--depth N` (1-hop default), `--no-symbols`, `--no-memory`, `--max-bytes`.

---

## Task 2 — `icmg pack <task>`

**Files:**
- Create: `src/cli/commands/pack_cmd.cpp`

**Input:** free-form task description.
**Pipeline:**
1. Tokenize task → run `icmg recall <tokens>` (top 5 memory).
2. Extract any file/symbol names mentioned → run `icmg context` on each.
3. Find applicable rules via `icmg rule for <files>`.
4. Bundle into single JSON or markdown blob.

**Output:**
```markdown
# Task Context: fix EnsureStockRegistered NullRef
## Files
- src/Sync/SyncOrder.cs (zone=sync)
  - EnsureStockRegistered:64-98
- src/Core.cs (caller)
  - ProcessOrder:120-145
## Memory (relevant)
- errors-resolved: NullRef when stock=0 → guard in fn entry
- decisions-mjsync: stock validation must run before EnsureStockRegistered
## Rules (applicable)
- src/Sync/**: prefer Float type over 4 Bytes
```

**Designed for:** UserPromptSubmit hook — auto-injected at session start.

---

## Task 3 — `icmg diff-summary`

**Files:**
- Create: `src/cli/commands/diff_summary_cmd.cpp`

Wraps `git diff` output through symbol-aware filter:

```
M src/Sync/SyncOrder.cs
  + EnsureStockRegistered() at L64
  ~ ProcessOrder() — added Unit param
  - DeprecatedHelper()
M src/Core.cs
  + Path field
  ~ Init() — moved validation up
A tests/sync_test.cs
```

**Implementation:** parse git unified diff; for each hunk, locate enclosing symbol via `graph_nodes` line ranges. Group by file. Emit summary lines.

**Flags:** `--ref HEAD~5`, `--full` (passes through raw diff after summary), `--json`.

**Token saving:** typical 500-line diff → 20-line summary (97% reduction).

---

## Task 4 — `icmg explain <error>`

**Files:**
- Create: `src/cli/commands/explain_cmd.cpp`

**Pipeline:**
1. Hash error message → tokenize.
2. `icmg recall <tokens> --topic errors-resolved` (force topic-prefixed search).
3. Return top 1-3 past resolutions verbatim.

**Output:**
```
Past resolutions for similar errors (top 3):

[4.5] 2026-04-12 NullReferenceException in SyncOrder.cs
  Fix: guard `if (stockHelper == null) throw ...` at L60.
  Commit: a1b2c3d
```

---

## Task 5 — `icmg session save | restore`

**Files:**
- Create: `src/cli/commands/session_cmd.cpp`
- Schema: `migrations/0008_sessions.sql` — `sessions(id, name, snapshot_json, created_at)`

**Use:** mid-task pause. `save mytask` snapshots currently-relevant context (last 50 recall queries + active rules + open files). `restore mytask` re-emits the bundle.

---

## Task 6 — Output cap utility

**Files:**
- Create: `src/core/output_cap.hpp`

```cpp
// Truncate to N bytes; spill rest to temp file; return path.
std::string capOutput(const std::string& full, size_t cap, std::string& spill_path);
```

Used by all bundle commands: if output > cap, emit `... [truncated; full at /tmp/icmg-spill-XXXX]`.

---

## Task 7 — MCP integration

**Files:**
- Create: `src/mcp/tools/context_tool.cpp` — wraps `icmg context`.
- Create: `src/mcp/tools/pack_tool.cpp` — wraps `icmg pack`.
- Create: `src/mcp/tools/diff_summary_tool.cpp`.

These become Claude's first-line discovery tools.

---

## Verification Checklist

- [ ] `icmg context src/foo.cs` returns ≤4KB by default.
- [ ] `icmg pack "fix bug X"` returns markdown bundle with ≥1 memory + ≥1 file ref.
- [ ] `icmg diff-summary --ref HEAD~3` produces line-symbol mapping.
- [ ] `icmg explain "NullRef in foo"` matches past `errors-resolved` entries.
- [ ] `icmg session save/restore` round-trips snapshot JSON.
- [ ] `--max-bytes` cap respected; spill file written when exceeded.
- [ ] All MCP tools listed in `icmg_mcp_list`.
- [ ] All existing tests pass.
- [ ] New tests: `test_context_cmd`, `test_pack_cmd`, `test_diff_summary`.

---

## Rollback

Pure additive (new commands). No schema changes except optional sessions table.
