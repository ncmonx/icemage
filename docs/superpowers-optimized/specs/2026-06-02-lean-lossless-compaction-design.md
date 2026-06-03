# v2.0.0 — Lean & Lossless Compaction (Context Governor)

**Date:** 2026-06-02
**Status:** Approved (design); pending spec review → writing-plans.
**Origin:** User pain — Claude Code native `/compact` blocks the session mid-task
("harus berhenti dulu menunggu compact selesai"). Internet research +
icmg-internals diagnosis below.

---

## 1. Problem & honest diagnosis

User wants compaction to "run in the background" so the session never stalls.

**Hard walls (verified, OUT of icmg's control):**

1. **CC native compaction is harness-locked + synchronous.** The next turn needs
   the summarized history, so it cannot be truly async. icmg cannot replace or
   background it.
2. **No hook can trigger `/compact` programmatically** (June 2026). Feature
   requests open, not implemented: anthropics/claude-code
   [#58538](https://github.com/anthropics/claude-code/issues/58538),
   [#39275](https://github.com/anthropics/claude-code/issues/39275),
   [#38925](https://github.com/anthropics/claude-code/issues/38925).
   → auto idle-compact is NOT buildable; only an **advisory nudge** is.
3. **icmg cannot evict CC conversation turns** (user/assistant messages) — only
   the harness can.

**Environment confirmed:** CC `2.1.160` (compaction already "instant" since
2.0.64). `CLAUDE_AUTOCOMPACT_PCT_OVERRIDE` already tuned by user; pain persists.

**Honest reframe (user-approved):** we cannot make compaction async. We CAN make
it **rare + lossless + un-surprising**, which delivers the same felt outcome —
rarely stops, never loses context, never ambushes mid-task.

**Research basis:**
- DAG-based structurally-lossless trimming, deterministic, recency/frequency
  scoring, preserves referenced nodes (arxiv 2602.22402; 20% mean / 86% max).
- Lost-in-the-middle / U-shaped RoPE attention — relevant content at extrema,
  filler in middle (ICLR 2025; still real on 1M-token models 2026).
- CC compaction clears tool outputs FIRST, then summarizes; key snippets + user
  requests preserved, early instructions may drop (CC docs).

---

## 2. Scope

| # | Component | Pain lever | Reuses |
|---|-----------|-----------|--------|
| C1 | **Injection Governor** `icmg govern` | window fills slower → compaction rarer | context-budget, bundle, memory BM25 |
| C2 | **Cross-turn dedup** | don't re-inject what's already in window | inject_dedup, turn_cache, imem Jaccard |
| C3 | **U-shaped ordering** | lost-in-the-middle retention | pure |
| C4 | **Lossless transition** | compact loses nothing → compact early without fear | precompact_output, postcompact hook |
| C5 | **Idle-compact advisor** (Stop hook) | nudge at idle → no mid-task ambush | context-budget, Stop hook |
| C6 | **Tool-output trim** | leaner outputs → compaction later/lighter | cap-output, Tkil filters |
| C7 | **Document intake-trim** | big docs compressed before entering window | `ingest` (OCR sidecar pattern), compress engine |

### C7 — Document intake-trim (scope + honest limit)
Extend `icmg ingest` beyond OCR to text documents (pdf / docx / txt / md):
extract text → shared compress engine (structurally-lossless + opt-in
perplexity) → compact media-node + 7d content-hash cache (same pattern as OCR).
Saves tokens on large docs before they reach the model.

**Hard limit (documented, not solvable here):** files attached directly to the
CC prompt UI **cannot be intercepted** — Anthropic exposes no pre-attachment
hook (open item since 2026-05-11). C7 works only when the user routes a doc
**by path** through icmg (`icmg ingest report.pdf`, or references a file path
icmg can read). The win is real for path-routed docs; UI-attached docs remain a
documented workaround ("pre-process via icmg ingest").

**Voice intake = still DEFERRED** (whisper sidecar = new heavy dep; revisit v2.1).

### Non-goals (explicit)
- ✗ Async/background CC compaction (harness-locked).
- ✗ Auto-trigger `/compact` from a hook (#58538 open; C5 is advisory-only).
- ✗ Evicting CC conversation turns.
- ✗ Voice/audio intake-trim (deferred — whisper sidecar heavy dep; v2.1).
- ✗ Intercepting UI-attached files (no pre-attachment hook; C7 is path-routed only).
- ✗ TUI dashboard (separate milestone).
- ✗ Perplexity as default (opt-in extension of existing `compress_select.hpp`).

---

## 3. Architecture & data flow

```
 per-prompt (hot, <50ms)            PreCompact (cold)         Stop (idle)
 ─────────────────────              ─────────────────         ──────────
 icmg govern budget B               snapshot manifest          fill >= thr band?
   ├ rank sources                     (node-ids + pinned)         └ emit nudge
   │  (pinned/graph/mem/diff/issue)   → working_set_snapshot         "/compact safe"
   ├ C2 dedup-gate vs window        PostCompact (SessionStart src=compact)
   ├ knapsack fill -> B               rebuild from manifest (HARD CAP, fresh)
   ├ C3 U-order (relevant@edges)      + re-anchor rules/decisions (existing hook)
   └ emit injected context           + maintain CLAUDE.md "Compact Instructions"
```

Shared compress engine (used by C1 fill + C6): **structurally-lossless**
(deterministic, drop low-value structured spans, keep graph-referenced) with an
**opt-in perplexity backend** (extends `compress_select.hpp`, warm llama only).

---

## 4. Interfaces (pure, testable)

```cpp
// C1 — governor selection (pure; no I/O)
struct Source { string id; string text; int tokens; double relevance; int priority; bool pinned; };
struct WorkingSet { vector<Source> items; int totalTokens; };
WorkingSet selectWorkingSet(const vector<Source>& candidates, int budgetTokens);

// C3 — lost-in-the-middle ordering (pure)
vector<Source> orderUShaped(vector<Source> items);   // highest relevance at front+back

// C2 — dedup gate (pure)
bool dedupGate(const Source& s, const vector<string>& windowShingles, double jaccardThr);

// C4 — snapshot/rebuild round-trip (pure transform; storage separate)
struct Manifest { vector<string> nodeIds; vector<string> pinnedIds; int64_t ts; };
Manifest snapshotManifest(const WorkingSet& ws);
WorkingSet rebuildFromManifest(const Manifest& m, int hardCapTokens);   // F2: cap

// C5 — advisor decision (pure)
struct Nudge { bool fire; string message; };
Nudge idleCompactAdvice(int fillPct, int lastFiredBand, int thresholdPct);

// shared — compress backend dispatch
string compress(const string& text, int budget, CompressBackend backend);  // tkil|perplexity
```

All six selection/ordering/gate/round-trip functions are pure → unit-testable
with mock inputs, no DB, no model.

---

## 5. Storage

One numbered migration, project DB:

```
00XX_working_set_snapshot:
  session_id TEXT, ts INTEGER, manifest_json TEXT, pinned_json TEXT
  PRIMARY KEY (session_id, ts)
```

No edits to existing migrations (append-only rule).

---

## 6. Critical-failure mitigations (from adversarial check)

- **F1 (C1 footprint may be small share of fill):** ship measurement first.
  Governor reads `context-budget` per-source breakdown; if icmg-injected share
  is small, the "rarer compaction" benefit is honestly bounded and the headline
  value shifts to C4/C5. `icmg govern --report` surfaces the real share. No
  inflated claims.
- **F2 (C4 rebuild can refill window → thrash):** `rebuildFromManifest` takes a
  HARD `hardCapTokens` cap (default ~2-3K). Post-compact re-injection is pinned
  essentials only, never the full working-set. Prevents the CC thrashing-error
  loop.
- **F3 (C5 nudge noise):** rate-limit by fill-band crossing (e.g. fire once per
  10% band), not per turn. Opt-out env.
- **F4 (C6 DAG claim weak for raw tool output):** C6 = extend existing Tkil/cap
  filters for structured outputs (build/test/log); do NOT claim graph-DAG
  trimming on arbitrary text. Lower, honest scope.
- **F5 (C2 false-positive):** conservative Jaccard threshold; pinned always
  bypass dedup.
- **F6 (hot-path latency):** selection pure + fast (<50ms target); snapshot only
  on PreCompact (cold). Verify with a timing test.

---

## 7. Testing strategy (TDD per component)

- `selectWorkingSet`: priority order, budget cap exact, tie-break, pinned-first.
- `orderUShaped`: extrema placement, odd/even counts, single item.
- `dedupGate`: dup / near-dup / unique; pinned bypass.
- `snapshotManifest` ↔ `rebuildFromManifest`: round-trip, hard-cap enforcement.
- `idleCompactAdvice`: band rate-limit, threshold edges, no-refire same band.
- `compress` dispatch: tkil default, perplexity fallback when no model.
- Latency test: governor hot-path < 50ms on representative inputs.
- C7 doc extract: pdf/docx/txt/md → text; cache hit on repeat content-hash;
  compress-engine round-trip preserves key identifiers; UI-attach limit documented (no test — out of scope).

Target: each component ≥1 ctest target. Expected ctest delta: +18–24.

---

## 8. Phasing

- **P1 (flagship core, zero-model):** C1 governor + C3 U-order + C6 tool-trim
  + measurement (F1). ← the v2.0.0 marker.
- **P2 (lossless + advisor):** C4 transition (with F2 cap) + C5 advisor.
- **P3 (dedup + docs):** C2 cross-turn dedup + C7 document intake-trim.
- **P4 (opt-in):** perplexity backend wiring (extends compress_select.hpp);
  C7 uses this backend when present.

Each phase TDD-first, full ctest gate at end. Ship as v2.0.0 once P1+P2 land
(P3/P4 may follow as v2.0.x if needed).

---

## 9. Rollout / migration notes

- New migration is additive (append-only); no schema churn to existing tables.
- New hooks (C5 Stop advisor) installed via `icmg init --force` template; C4
  extends the existing `icmg-postcompact-memory.sh`.
- All new behavior opt-out via env (consistent with icmg conventions).
- Upstream tracking: link #58538 in release notes as the real async-compaction
  fix that, once shipped by Anthropic, lets C5 become auto-trigger.
