# Token Efficiency — P3 + F11/F12/F14 Consolidated Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers-optimized:executing-plans (INLINE — Claude subagent dispatch FORBIDDEN per project rule). Steps use checkbox (`- [ ]`).

**Goal:** Ship remaining token-efficiency wins after P1–P2: cross-turn dedup (C2), document intake-trim (C7), perplexity scoring (P4), shrink-prompt (F11), gist (F12), and differential context (F14). Compound target: **40-60% additional reduction** on top of existing Tkil + P1 gains.

**Architecture:** Pure header cores in `src/core/`, unit-tested against `tests/test_main.hpp`, wired into `icmg govern`, `icmg ingest`, and new `icmg gist` / `icmg shrink-prompt` commands. No model dependency — all deterministic or opt-in BPE.

**Prerequisites:** P1 (C1 + C3 + govern cmd) shipped. P2 (C4 + C5) should be done or near-done before starting P3.

---

## File Structure

| File | Responsibility |
|------|----------------|
| `src/core/cross_turn_dedup.hpp` (C2) | Word-set Jaccard dedup: skip near-duplicate context slices |
| `src/core/ingest_compress.hpp` (C7) | Document intake-trim: extract→compress→structural_trim pipeline |
| `src/core/compress_select_perplex.hpp` (P4) | Perplexity-weighted salience scoring (opt-in, model-free fallback) |
| `src/core/shrink_prompt.hpp` (F11) | 20-30% input/turn trim: drop low-signal sentences |
| `src/core/gist_core.hpp` (F12) | Pure function: file→priority lines→structural trim→output |
| `src/core/diff_context.hpp` (F14) | Differential context: session A→B, only send changed priority sources |

Corresponding test files: `tests/core/test_*.cpp`
CLI commands: `src/cli/commands/{ingest_cmd,gist_cmd,shrink_cmd}.cpp` + `govern_cmd.cpp` modifications

---

### Task 1: Cross-turn dedup (C2 [P3])

**Files:**
- Create: `src/core/cross_turn_dedup.hpp`
- Test: `tests/core/test_cross_turn_dedup.cpp`
- Modify: `src/cli/commands/govern_cmd.cpp` (wire dedup into working-set build)

**Does NOT cover:** Cross-session dedup (F14 handles that). This task deduplicates within a single session/turn sequence.

- [ ] **Step 1: Write failing test**
  - Word-set Jaccard similarity: two spans with >80% word overlap → skip second
  - Boundary: no false-positive on short strings (<5 words)
  - Boundary: empty window → always pass through
  - Boundary: exact duplicate → always skip
- [ ] **Step 2: Implement `cross_turn_dedup.hpp`**
  - Pure function: `dedupWindow(candidates, window, threshold=0.8) → deduped`
  - Jaccard = |intersection| / |union| of word-sets
  - Window = sliding buffer of last N context slices (persisted to file across turns)
  - Window file: `~/.icmg/dedup-window-<sid>.json` (append-only, auto-rotate at 100 entries)
- [ ] **Step 3: Wire into `govern_cmd.cpp`**
  - After candidate fetch, before `selectWorkingSet()` → run dedup
  - Flag: `icmg govern --no-dedup` to bypass

---

### Task 2: Document intake-trim (C7 [P3])

**Extends:** `icmg ingest` (currently OCR-focused). Add generic text-document intake with compression pipeline.

- [ ] **Step 1: Write failing test**
  - .md with code blocks → extract text blocks, structural_trim
  - .txt with boilerplate → drop signature/separator lines
  - .pdf/.docx → extract text → compress (placeholder: extract fn exists in future)
- [ ] **Step 2: Implement `ingest_compress.hpp`**
  - `processDocument(path) → compressed_lines`
  - Pipeline: read → extract text (per-ext) → `structural_trim.hpp` → header preservation
  - Priority: keep headers, code fences, list markers; drop boilerplate
- [ ] **Step 3: Wire into `ingest_cmd.cpp`**
  - `icmg ingest <file>` auto-detects type and runs pipeline
  - `icmg ingest --raw <file>` bypasses compression
  - Migration: none — pure file-level, no DB change

---

### Task 3: shrink-prompt (F11)

**Files:**
- Create: `src/core/shrink_prompt.hpp`
- Test: `tests/core/test_shrink_prompt.cpp`
- Create: `src/cli/commands/shrink_cmd.cpp`

**Goal:** 20-30% reduction on input/turn by dropping low-signal sentences.

- [ ] **Step 1: Write failing test**
  - Sentence scoring: length < 10 chars AND no identifiers/numbers → low signal
  - Boundary: code snippets NEVER dropped (contains identifiers)
  - Boundary: questions/imperatives preserved
- [ ] **Step 2: Implement `shrink_prompt.hpp`**
  - Pure fn: `shrinkPrompt(text, budget_pct=0.8) → trimmed`
  - Heuristic scoring per sentence:
    - +2 if contains identifier (word with underscore/camelCase)
    - +1 if contains number/symbol
    - -1 if starts with filler ("so", "basically", "anyway", "well")
    - -2 if punctuation-only or < 8 chars
  - Drop lowest-scored sentences until within budget
  - Never drop first/last 2 sentences (context preservation)
- [ ] **Step 3: Wire into hook**
  - Stop hook auto-shrinks user input if > 2KB
  - `icmg shrink-prompt < input.txt > output.txt` for manual use

---

### Task 4: icmg gist (F12)

**Files:**
- Create: `src/core/gist_core.hpp`
- Test: `tests/core/test_gist_core.cpp`
- Create: `src/cli/commands/gist_cmd.cpp`

**Goal:** LLM-free file summarization: pick highest-signal lines within budget. Targets 70%+ reduction.

- [ ] **Step 1: Write failing test**
  - 500-line file → 50-line gist preserves all identifier/header lines
  - Boundary: file < budget → pass-through
  - Boundary: empty file → empty output
- [ ] **Step 2: Implement `gist_core.hpp`**
  - Pure fn: `makeGist(lines, budget_lines=50) → gist_lines`
  - Per-line scoring (reuses infoScore heuristic from `compress_select.hpp`):
    - Headers/function signatures: highest priority
    - Lines with keywords (TODO, FIXME, NOTE): high priority
    - Lines with identifiers/code: medium
    - Blank/comment-only: lowest
  - Preserves line order within kept set
  - Optional: emit line-number map for traceability
- [ ] **Step 3: Wire into `gist_cmd.cpp`**
  - `icmg gist <file>` or `cat file | icmg gist`
  - `--lines N` for budget control
  - `--map` to show which lines were kept

---

### Task 5: Differential context (F14)

**Files:**
- Create: `src/core/diff_context.hpp`
- Test: `tests/core/test_diff_context.cpp`
- Modify: context-injection path in `context_cmd.cpp` or session start

**Goal:** 40-60% cut on repeat sessions by only sending changed priority sources.

- [ ] **Step 1: Write failing test**
  - Session A ships sources [A,B,C] → Session B only changed B → inject [B] only
  - Boundary: new session (no prior state) → inject all sources
  - Boundary: all changed → inject all
- [ ] **Step 2: Implement `diff_context.hpp`**
  - State file: `~/.icmg/diff-state-<sid>.json` (source_name → hash)
  - Pure fn: `diffSources(candidates, prior_state) → (changed_sources, new_state)`
  - Hash = fast content hash (FNV-1a or xxHash) of serialized source content
  - Pinned sources always re-injected (differential bypass)
- [ ] **Step 3: Wire into context injection**
  - Before `selectWorkingSet()`, run diff against prior state
  - Save new state after injection
  - `icmg govern --full` to force full re-inject

---

### Task 6: Perplexity-weighted salience (P4)

**Files:**
- Create: `src/core/compress_select_perplex.hpp`
- Test: `tests/core/test_compress_select_perplex.cpp`

**Goal:** Extend `compress_select.hpp` with opt-in perplexity backend for higher-quality compression.

- [ ] **Step 1: Write failing test**
  - Perplexity scoring matches infoScore ordering on known corpus
  - Boundary: no model available → fallback to infoScore
  - Boundary: model error → graceful fallback
- [ ] **Step 2: Implement `compress_select_perplex.hpp`**
  - Interface: `perplexityScore(text) → float` (higher = more surprising = more info)
  - Backend options (ordered):
    1. BPE tokenizer + unigram log-prob (model-free, deterministic)
    2. External LLM via pipe (opt-in, `ICMG_PERPLEXITY_CMD` env)
  - Falls back to existing `infoScore()` from `compress_select.hpp` if no backend available
  - Caching: LRU cache of last 100 scores (key = text hash)
- [ ] **Step 3: Wire into `compress_select.hpp`**
  - `selectByBudget()` gains optional `--mode perplexity` flag
  - Default remains infoScore for speed; perplexity mode is opt-in

---

## Dependency graph

```
P1 (shipped) ──→ P2 (in progress) ──→ P3 ──→ F14
                                            │
                                            └──→ F11
                                            │
                                            └──→ F12
                                            │
                                            └──→ P4 (perplexity)
```

P3 (C2 + C7) independent of F11/F12/F14 — can run in parallel.
P4 depends on F12 for optional LLM-assisted perplexity.

## Estimated impact per task

| Task | Reduction | Complexity | Priority |
|------|-----------|------------|----------|
| C2 Cross-turn dedup | 15-25% | Low | High |
| C7 Intake-trim | 10-20% on doc loads | Medium | Medium |
| F11 shrink-prompt | 20-30% on input | Low | Medium |
| F12 icmg gist | 70%+ on large files | Low | High |
| F14 Differential context | 40-60% on repeat sessions | Medium | High |
| P4 Perplexity scoring | 5-15% quality bump | Medium | Low |

**Compound estimate:** With P1+P2+P3+F11+F12+F14 → **75-90% total reduction** from baseline.
