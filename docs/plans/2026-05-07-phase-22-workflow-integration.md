# Phase 22 — Workflow Integration (KG Reasoning + GSD Lifecycle + Superpowers Gates)

**Goal:** Make icmg the persistent backend for workflow discipline — transitive impact analysis (KG), goal-backward phase verification (GSD), and skill-driven gates (Superpowers). Replace markdown-only skill state with queryable structured data.

**Why:** Phases 17-21 cut tokens. Phase 22 cuts **context loss** between sessions and enforces process rigor without manual ceremony. Combines best of three frameworks (KG + GSD + Superpowers) into native icmg commands and hooks.

**Tech:** SQLite extensions for transitive closure, new tables for verification/issues/designs, hook scripts as install templates, MCP tool layer.

**Estimate:** 8-10 days.

**Assumptions:**
- Phase 17 (zones) and Phase 18 (symbol nodes) are landed — transitive impact requires symbol-level edges to be useful.
- Hooks are user-installed templates, not built-in — keep icmg headless.
- All workflow state reuses memory_nodes table where possible (topic conventions); only add new tables when topic-typing is insufficient.

---

## Task 1 — Transitive impact queries

**Files:**
- Modify: `src/graph/graph_store.cpp` — add `closure(node_id, edge_types, max_depth)` method.
- Modify: `src/cli/commands/graph_cmd.cpp` — new `graph-impact`, `graph-callers --transitive`, `graph-callees --transitive`.

**Algorithm:**
```cpp
// BFS forward closure with cycle detection
std::vector<int64_t> closure(int64_t start, const std::set<std::string>& edge_types,
                              int max_depth) {
    std::set<int64_t> visited;
    std::queue<std::pair<int64_t,int>> q;  // (id, depth)
    q.push({start, 0});
    while (!q.empty()) {
        auto [cur, d] = q.front(); q.pop();
        if (visited.count(cur) || d >= max_depth) continue;
        visited.insert(cur);
        // SELECT dst FROM graph_edges WHERE src=? AND edge_type IN (...)
        for (int64_t nb : neighbors(cur, edge_types)) {
            if (!visited.count(nb)) q.push({nb, d+1});
        }
    }
    visited.erase(start);
    return {visited.begin(), visited.end()};
}
```

**CLI:**
```bash
icmg graph impact <symbol_or_file> [--depth N] [--types calls,uses,imports]
# → list of all nodes reachable forward (impact radius)

icmg graph reverse-impact <symbol> [--depth N]
# → who would break if X changes (reverse closure)
```

**Verify:** `icmg graph impact ProcessOrder --depth 5` returns transitive callers; manual count matches expected.

---

## Task 2 — Typed edge expansion

**Files:**
- Modify: `src/graph/symbol_extractor/csharp_symbol_extractor.cpp` — emit `extends`, `implements` edges from `class A : B, IC`.
- Modify: `src/graph/symbol_extractor/sql_symbol_extractor.cpp` — emit `queries_table`, `references_field`.
- Modify: `src/graph/graph_store.hpp` — document edge type taxonomy.

**Edge taxonomy (final):**
| Type | Source kind | Target kind | Weight | Meaning |
|------|-------------|-------------|--------|---------|
| `imports` | file | file | 1.0 | namespace/include reference |
| `uses` | file | file | 1.5 | class-name cross-ref (Phase 3 strategy 4) |
| `companion` | file | file | 2.0 | designer/resx triples |
| `calls` | symbol | symbol | 1.5 | fn → fn invocation (Phase 18) |
| `extends` | class | class | 2.0 | inheritance |
| `implements` | class | interface | 1.5 | interface impl |
| `queries_table` | sp/fn | table | 1.5 | SQL SELECT/UPDATE/INSERT |
| `references_field` | sp/fn | field | 1.0 | column reference |

---

## Task 3 — `icmg known-issue` (mirror Superpowers error-recovery)

**Files:**
- Create: `src/cli/commands/known_issue_cmd.cpp`
- Reuse: `memory_nodes` with topic prefix `errors-resolved`.

**Subcommands:**
```bash
icmg known-issue add <error-pattern> --fix <description> [--zone Z]
icmg known-issue match <error-text>           # find matching past resolutions
icmg known-issue list [--zone Z]
icmg known-issue stats                        # most-recurring errors
```

**Hook integration (PreToolUse on Bash):** when command exits with error, auto-call `icmg known-issue match "$stderr"` → if hit, inject past fix as `additionalContext` for Claude.

**Verify:** Store an error → trigger same error in Bash → hook injects fix.

---

## Task 4 — `icmg verify --record`

**Files:**
- Create: `src/cli/commands/verify_cmd.cpp`
- Schema: `migrations/0011_verification.sql` — `verifications(id, phase, command, exit_code, output_hash, recorded_at)`

**Command:**
```bash
icmg verify --command "ctest" --command "cmake --build build" --phase 22
# Runs each command, records exit + output hash
icmg verify show --phase 22
# List recorded verifications for phase
icmg verify gate --phase 22
# Exit 0 if all recorded verifications pass; non-zero blocks downstream actions
```

**Hook integration (PreToolUse on `git push`):** call `icmg verify gate --phase $current` → block push if not verified. Replaces craftsman pre-push-verify hook with project-aware version.

---

## Task 5 — `icmg phase` lifecycle commands (GSD-lite)

**Files:**
- Create: `src/cli/commands/phase_cmd.cpp`
- Schema: `migrations/0012_phases.sql` — `phases(id, num, name, goal, status, started_at, completed_at, commit_sha)`

**Subcommands:**
```bash
icmg phase list                              # show all phases + status
icmg phase show <num>                        # full detail
icmg phase start <num>                       # mark in-progress, snapshot context
icmg phase research <num>                    # generate RESEARCH.md template
icmg phase plan <num>                        # link existing PLAN.md
icmg phase verify <num>                      # goal-backward check:
                                              #   - tasks done? (PROGRESS.md scan)
                                              #   - verify gate pass?
                                              #   - new memory entries match goal?
                                              #   - tests still green?
icmg phase ship <num>                        # tag commit, mark completed
```

**Goal-backward verifier logic:**
```
1. Parse phase plan → extract goal sentence + task checklist.
2. Verify all tasks marked [x].
3. Run `icmg verify gate --phase N` — must pass.
4. Recall memory with goal text → confirm relevant entries exist.
5. Run `git diff <phase-start>..HEAD --stat` → expected files touched.
6. Emit GO/NO-GO verdict with evidence.
```

---

## Task 6 — `icmg log` (queryable session-log backend)

**Files:**
- Create: `src/cli/commands/log_cmd.cpp`
- Reuse: `memory_nodes` topic prefix `log-saved`.

**Subcommands:**
```bash
icmg log save                              # interactive: prompts for goal/decisions/rejected/open
icmg log save --from-stdin                 # parse markdown [saved] block from stdin
icmg log search <query>                    # BM25 over saved entries
icmg log show <id>
icmg log recent [--limit N]
icmg log export > session-log.md           # regenerate markdown from DB
icmg log import session-log.md             # one-time migration
```

**Win:** session-log.md becomes a queryable view, not the source of truth. Cross-project search later (Phase 21 `--all-projects`) becomes trivial.

---

## Task 7 — `icmg design --gate` (Superpowers brainstorming gate)

**Files:**
- Create: `src/cli/commands/design_cmd.cpp`
- Schema: `migrations/0013_designs.sql` — `designs(id, feature, doc_path, approved_at, approved_by)`

**Subcommands:**
```bash
icmg design register <feature> <doc_path>    # register approved design
icmg design approve <feature>                # mark approved
icmg design check <feature>                  # exit 0 if approved, non-zero else
icmg design list
```

**Hook integration (PreToolUse on Edit/Write):** if file in `src/` changed for new feature without `icmg design check <feature>` passing → deny with reason "design doc required, run brainstorming skill first".

---

## Task 8 — Skill-mirroring topic conventions

**Files:**
- Modify: `src/cli/commands/store_cmd.cpp` — add `--topic-template` shortcut flags.

**Conventions:**
| Skill / GSD concept | icmg topic prefix |
|---------------------|-------------------|
| Superpowers `[saved]` entry | `log-saved` |
| Superpowers `error-recovery` | `errors-resolved` (existing) |
| Superpowers brainstorming decision | `decisions-<project>` (existing) |
| Superpowers verification evidence | `verifications-<project>` |
| GSD phase research notes | `phase-research-<num>` |
| GSD phase verification | `phase-verify-<num>` |
| KG impact analysis result | `impact-<symbol>` |

Standardized prefixes → `icmg recall --topic <prefix>` becomes a precise filter.

---

## Task 9 — MCP tool exposure

**Files:**
- Create: `src/mcp/tools/impact_tool.cpp`
- Create: `src/mcp/tools/known_issue_tool.cpp`
- Create: `src/mcp/tools/verify_tool.cpp`
- Create: `src/mcp/tools/phase_tool.cpp`
- Create: `src/mcp/tools/design_tool.cpp`
- Create: `src/mcp/tools/log_tool.cpp`

**Standard MCP shape:** each tool wraps the CLI subcommand, returns JSON. Total new tools: ~15. List exposed in `icmg --mcp-server` `tools/list` response.

---

## Task 10 — Hook templates bundle

**Files:**
- Create: `examples/hooks/icmg-known-issue-recall.sh` (PreToolUse, Bash matcher)
- Create: `examples/hooks/icmg-verify-gate.sh` (PreToolUse, `git push` matcher)
- Create: `examples/hooks/icmg-design-gate.sh` (PreToolUse, Edit/Write matcher)
- Create: `examples/hooks/icmg-log-on-stop.sh` (Stop hook → suggest `icmg log save`)
- Create: `docs/hooks-recipes.md` — full installation guide

**Each hook:**
- Reads stdin JSON
- Calls relevant `icmg` subcommand
- Returns appropriate `permissionDecision` / `additionalContext` JSON
- Documented with copy-paste settings.local.json snippet

---

## Verification Checklist

- [ ] `icmg graph impact <fn> --depth 5` returns transitive set; matches manual trace.
- [ ] `icmg known-issue match "NullRef"` finds prior resolution; hook injects context.
- [ ] `icmg verify --record` stores evidence; `verify gate` blocks on missing.
- [ ] `icmg phase verify <n>` produces GO/NO-GO with evidence list.
- [ ] `icmg log save --from-stdin` round-trips a `[saved]` markdown block.
- [ ] `icmg design check <feat>` blocks PreToolUse when not approved.
- [ ] All 15 existing unit tests pass.
- [ ] New tests: `test_closure`, `test_known_issue`, `test_verify_record`, `test_phase_verify`, `test_log_cmd`, `test_design_gate`.
- [ ] MCP server `tools/list` includes all new tools.

---

## Rollback Plan

All new tables additive. Hook templates are user-installed — disable by removing from settings.json. Topic prefixes are conventions; old data unaffected.

---

## Risk Mitigation

| Risk | Mitigation |
|---|---|
| Closure query slow on large graph | Cache impact sets; invalidate on edge update |
| Hook gates block legitimate work | All gates have `--bypass` flag with audit log entry |
| Topic prefix collision with user data | Reserve `log-`, `phase-`, `errors-`, `verifications-`, `impact-` prefixes; document |
| Phase state divergence (PROGRESS.md vs DB) | One-way sync: DB is source of truth, regenerate PROGRESS.md |
| MCP tool explosion (15+ new tools) | Group under namespace prefix `icmg_workflow_*`; user can disable subset |

---

## Cross-Cutting: Topic Reserved-Prefix Registry

**Files:**
- Create: `src/imem/reserved_topics.hpp`

```cpp
constexpr const char* RESERVED_PREFIXES[] = {
    "log-saved",
    "log-", 
    "phase-research-",
    "phase-verify-",
    "phase-",
    "errors-resolved",
    "errors-",
    "verifications-",
    "decisions-",
    "preferences",
    "impact-",
    "design-",
    "context-"
};
```

`MemoryStore::store()` warns (not blocks) when user-supplied topic uses reserved prefix without correct schema.

---

## Expected Cumulative Impact (with Phases 17-22)

| Metric | Before | After 17-21 | After +22 |
|--------|--------|-------------|-----------|
| Tokens per session | 50K | 12K | 8K |
| Context retained across reset | 30% | 60% | 90% |
| Time-to-productive after reset | 10min | 3min | <1min |
| Refactor blast-radius miss rate | 30% | 30% | <5% |
| Skill-rule violations (raw grep, etc.) | frequent | rare | near-zero |
| Audit-trail completeness | ~0% | partial | full |
| Phase-completion confidence | "feels done" | tasks-checked | goal-verified |

**Phase 22 closes the gap from "fast" to "rigorous."**

---

## Sub-Phase Breakdown (for parallel execution)

If executing with `subagent-driven-development`:

| Wave | Tasks | Duration |
|------|-------|----------|
| 1 | T1 (closure), T2 (typed edges) | 2d |
| 2 | T3 (known-issue), T4 (verify), T6 (log) — independent | 2d parallel |
| 3 | T5 (phase), T7 (design) — depend on T4 | 2d |
| 4 | T8 (topics), T9 (MCP), T10 (hooks) — wrap-up | 2d |

**Total wall-clock: 8 days with parallelism, 10 days sequential.**
