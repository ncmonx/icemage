# Lean & Lossless Compaction — P1 Core Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers-optimized:executing-plans (INLINE — per project rule, Claude subagent dispatch is forbidden; use icmg agent or inline). Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship the v2.0.0 flagship core — a deterministic injection governor that budget-caps and U-orders icmg-injected context, plus an honest measurement of icmg's window-fill share.

**Architecture:** Two pure, model-free header modules in `src/core/` (`working_set.hpp`: `selectWorkingSet` knapsack-by-priority×relevance + `orderUShaped` lost-in-the-middle ordering), unit-tested against the existing `tests/test_main.hpp` harness, then wired into a new `icmg govern` CLI command that (a) `--report` shows real per-source fill share from the existing `context-budget` transcript parser (F1 honesty gate) and (b) emits a budgeted, U-ordered working-set built from memory decisions + known-issues.

**Tech Stack:** C++17, header-only pure cores, `ICMG_REGISTER_COMMAND` macro, SQLite via existing `MemoryStore`, CMake `add_icmg_test`.

**Assumptions:**
- Assumes the existing `context-budget` command already parses the latest transcript JSONL into per-source token counts — will NOT need a new transcript parser if so (verified: `src/cli/commands/contextbudget_cmd.cpp` has `parseTranscript`/`bySourceJson`). If that parser's output format differs, Task 4 reuses its functions rather than reimplementing.
- Assumes `MemoryStore` exposes a recall/list API returning topic+content+score — will NOT work as written if the method names differ; Task 4 adapts to the actual `MemoryStore` surface read at implementation time.
- Scope is P1 ONLY (C1 + C3 + F1 measurement). C2/C4/C5/C6/C7 + perplexity backend are separate plans.

---

## File Structure

| File | Responsibility |
|------|----------------|
| `src/core/working_set.hpp` (create) | Pure `Source`/`WorkingSet` types + `selectWorkingSet` (budget knapsack) + `orderUShaped` (lost-in-the-middle ordering). No I/O, no model. |
| `tests/core/test_working_set.cpp` (create) | Unit tests for both pure functions. |
| `src/cli/commands/govern_cmd.cpp` (create) | `icmg govern` command: `--report` (fill share) + budgeted U-ordered working-set emit. |
| `CMakeLists.txt` (modify) | One `add_icmg_test` line. (Command picked up by GLOB_RECURSE — no edit needed.) |

---

### Task 1: Pure working-set selection core

**Files:**
- Create: `src/core/working_set.hpp`
- Test: `tests/core/test_working_set.cpp`

**Does NOT cover:** Source fusion from graph/diff (P2). This task only selects/budgets a pre-built candidate list; it does not fetch sources. Pinned sources always survive even over budget — non-pinned over-budget items are dropped.

- [ ] **Step 1: Write failing test**

```cpp
// tests/core/test_working_set.cpp
// v2.0.0 C1: pure injection-governor selection — keep highest priority x relevance
// candidates within a token budget; pinned always survive. Model-free, unit-testable.
#include "../test_main.hpp"
#include "../../src/core/working_set.hpp"
#include <string>
#include <vector>

using namespace icmg::core;

static Source mk(const std::string& id, int tokens, double rel, int prio, bool pinned) {
    Source s; s.id = id; s.text = id; s.tokens = tokens;
    s.relevance = rel; s.priority = prio; s.pinned = pinned; return s;
}

TEST("selectWorkingSet: budget >= total keeps all, totalTokens summed") {
    std::vector<Source> c{mk("a",10,0.5,1,false), mk("b",20,0.9,1,false)};
    auto ws = selectWorkingSet(c, 1000);
    ASSERT_EQ(ws.items.size(), (size_t)2);
    ASSERT_EQ(ws.totalTokens, 30);
}

TEST("selectWorkingSet: tight budget keeps highest priority x relevance") {
    // budget fits ~1 item of 20 tokens. HIGH score wins.
    std::vector<Source> c{mk("low",20,0.1,1,false), mk("HIGH",20,0.9,1,false)};
    auto ws = selectWorkingSet(c, 20);
    ASSERT_EQ(ws.items.size(), (size_t)1);
    ASSERT_EQ(ws.items[0].id, std::string("HIGH"));
}

TEST("selectWorkingSet: pinned survives even over budget") {
    std::vector<Source> c{mk("PIN",100,0.01,1,true), mk("x",10,0.9,1,false)};
    auto ws = selectWorkingSet(c, 5);  // budget too small for either
    bool hasPin = false;
    for (auto& s : ws.items) if (s.id == "PIN") hasPin = true;
    ASSERT_TRUE(hasPin);
}

TEST("selectWorkingSet: higher priority beats higher relevance") {
    // prio 2 with rel 0.3 (=0.6) beats prio 1 with rel 0.5 (=0.5)
    std::vector<Source> c{mk("P1",20,0.5,1,false), mk("P2",20,0.3,2,false)};
    auto ws = selectWorkingSet(c, 20);
    ASSERT_EQ(ws.items[0].id, std::string("P2"));
}

TEST("selectWorkingSet: empty input -> empty") {
    std::vector<Source> c;
    auto ws = selectWorkingSet(c, 100);
    ASSERT_EQ(ws.items.size(), (size_t)0);
    ASSERT_EQ(ws.totalTokens, 0);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `pwsh -File build.ps1 -Target both 2>&1 | Select-String "working_set"` then `./build-msvc-full/test_working_set.exe`
Expected: FAIL — `src/core/working_set.hpp` does not exist (compile error: cannot open include).

- [ ] **Step 3: Implement minimal change**

```cpp
// src/core/working_set.hpp
#pragma once
// v2.0.0 C1: injection governor — pure, model-free working-set selection. Keep the
// highest (priority x relevance) candidates within a token budget; pinned candidates
// always survive even when over budget. Selection order is by score desc; the OUTPUT
// preserves the kept candidates in their original input order (caller may re-order via
// orderUShaped). No I/O, no DB, no model -> fully unit-testable.
#include <algorithm>
#include <numeric>
#include <string>
#include <vector>

namespace icmg::core {

struct Source {
    std::string id;
    std::string text;
    int tokens = 0;
    double relevance = 0.0;  // [0,1] BM25/recency blend supplied by caller
    int priority = 1;        // higher = more important (pinned/decisions > filler)
    bool pinned = false;     // pinned always survives the budget cut
};

struct WorkingSet {
    std::vector<Source> items;  // in original input order
    int totalTokens = 0;
};

// score = priority * relevance (priority dominates; relevance breaks within-tier).
inline double wsScore(const Source& s) { return (double)s.priority * s.relevance; }

// Greedy budget fill: pinned first (unconditional), then highest-score candidates
// while they fit. Kept items emitted in original order.
inline WorkingSet selectWorkingSet(const std::vector<Source>& candidates, int budgetTokens) {
    const size_t n = candidates.size();
    std::vector<bool> keep(n, false);
    int used = 0;

    // Pass 1: pinned survive unconditionally.
    for (size_t i = 0; i < n; ++i) {
        if (candidates[i].pinned) { keep[i] = true; used += candidates[i].tokens; }
    }

    // Pass 2: rank remaining by score desc (stable: ties keep earlier index).
    std::vector<size_t> order;
    for (size_t i = 0; i < n; ++i) if (!candidates[i].pinned) order.push_back(i);
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        return wsScore(candidates[a]) > wsScore(candidates[b]);
    });
    for (size_t i : order) {
        if (used + candidates[i].tokens <= budgetTokens) {
            keep[i] = true; used += candidates[i].tokens;
        }
    }

    WorkingSet ws;
    for (size_t i = 0; i < n; ++i) {
        if (keep[i]) { ws.items.push_back(candidates[i]); ws.totalTokens += candidates[i].tokens; }
    }
    return ws;
}

}  // namespace icmg::core
```

- [ ] **Step 4: Run test to verify it passes**

Run: `./build-msvc-full/test_working_set.exe`
Expected: PASS (5/5 assertions).

- [ ] **Step 5: Commit**

```bash
git add src/core/working_set.hpp tests/core/test_working_set.cpp
git commit -m "v2.0.0 C1: pure injection-governor selectWorkingSet (budget knapsack, pinned-survive)"
```

---

### Task 2: U-shaped ordering (lost-in-the-middle mitigation)

**Files:**
- Modify: `src/core/working_set.hpp`
- Modify: `tests/core/test_working_set.cpp`

**Does NOT cover:** token-position weighting inside a single source's text — operates at source granularity only. Assumes RoPE U-shaped attention (start+end favored); does NOT help models without positional decay (acceptable — all current CC models exhibit it).

- [ ] **Step 1: Write failing test (append to test file)**

```cpp
TEST("orderUShaped: most relevant at front+back, filler in middle") {
    // relevances 0.9,0.7,0.5,0.3,0.1 -> front gets top, back gets 2nd, middle lowest.
    std::vector<Source> in{mk("r5",1,0.5,1,false), mk("r9",1,0.9,1,false),
                           mk("r1",1,0.1,1,false), mk("r7",1,0.7,1,false),
                           mk("r3",1,0.3,1,false)};
    auto out = orderUShaped(in);
    ASSERT_EQ(out.size(), (size_t)5);
    ASSERT_EQ(out.front().id, std::string("r9"));   // highest at front
    ASSERT_EQ(out.back().id,  std::string("r7"));   // 2nd-highest at back
    ASSERT_EQ(out[2].id,      std::string("r1"));   // lowest in dead-center
}

TEST("orderUShaped: single item unchanged") {
    std::vector<Source> in{mk("solo",1,0.5,1,false)};
    auto out = orderUShaped(in);
    ASSERT_EQ(out.size(), (size_t)1);
    ASSERT_EQ(out[0].id, std::string("solo"));
}

TEST("orderUShaped: empty unchanged") {
    std::vector<Source> in;
    ASSERT_EQ(orderUShaped(in).size(), (size_t)0);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: rebuild + `./build-msvc-full/test_working_set.exe`
Expected: FAIL — `orderUShaped` undeclared (compile error).

- [ ] **Step 3: Implement minimal change (add before closing namespace brace in working_set.hpp)**

```cpp
// Reorder by relevance into a U-shape: highest-relevance items at the extrema
// (front and back), lowest in the middle. Mitigates "lost-in-the-middle" RoPE decay
// where models under-attend mid-context. Pure; stable for equal relevance.
inline std::vector<Source> orderUShaped(std::vector<Source> items) {
    const size_t n = items.size();
    if (n <= 1) return items;

    // Rank by relevance desc (stable).
    std::vector<size_t> rank(n);
    std::iota(rank.begin(), rank.end(), (size_t)0);
    std::stable_sort(rank.begin(), rank.end(), [&](size_t a, size_t b) {
        return items[a].relevance > items[b].relevance;
    });

    // Place rank[0] at front, rank[1] at back, rank[2] at front+1, rank[3] at back-1, ...
    std::vector<Source> out(n);
    size_t lo = 0, hi = n - 1;
    bool toFront = true;
    for (size_t r = 0; r < n; ++r) {
        if (toFront) out[lo++] = items[rank[r]];
        else         out[hi--] = items[rank[r]];
        toFront = !toFront;
    }
    return out;
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `./build-msvc-full/test_working_set.exe`
Expected: PASS (8/8 total).

- [ ] **Step 5: Commit**

```bash
git add src/core/working_set.hpp tests/core/test_working_set.cpp
git commit -m "v2.0.0 C3: orderUShaped lost-in-the-middle mitigation (relevant at extrema)"
```

---

### Task 3: Wire the test into CMake

**Files:**
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Add the test registration**

Locate the block near line 720 (`add_icmg_test(test_compress_select ...)`) and add directly after it:

```cmake
add_icmg_test(test_working_set tests/core/test_working_set.cpp)  # v2.0.0 C1+C3 governor core
```

- [ ] **Step 2: Reconfigure + build + verify the test is registered**

Run: `pwsh -File build.ps1 -Reconfigure -Target both -RunTests 2>&1 | Select-String "test_working_set"`
Expected: PASS line for `test_working_set`, and the overall ctest count increases by 1 (e.g. 1325 → 1326).

- [ ] **Step 3: Commit**

```bash
git add CMakeLists.txt
git commit -m "v2.0.0: register test_working_set ctest target"
```

---

### Task 4: `icmg govern` command — `--report` (F1) + budgeted U-ordered emit

**Files:**
- Create: `src/cli/commands/govern_cmd.cpp`

**Does NOT cover:** snapshot/rebuild (C4, separate plan), dedup (C2), hook auto-invocation (P2). This command is manual-invoke only here. `--report` reuses the existing `context-budget` source breakdown; if that command's functions are not header-exposed, the implementer extracts the shared parse into a small header first (note in commit).

- [ ] **Step 1: Read the existing command + MemoryStore surface**

Run: `icmg context src/cli/commands/contextbudget_cmd.cpp --lines 1-60` and `icmg graph symbol MemoryStore`
Purpose: confirm the recall API (method name returning topic/content/score) and the per-source token breakdown access. Adapt Step 3 to the actual signatures.

- [ ] **Step 2: Implement the command skeleton**

```cpp
// src/cli/commands/govern_cmd.cpp
// v2.0.0 C1+C3+F1: `icmg govern` — deterministic injection governor.
//   icmg govern --report             : honest per-source fill share (F1) so the
//                                       "rarer compaction" claim is never inflated.
//   icmg govern --budget <tokens>    : emit a budgeted, U-ordered working-set built
//                                       from memory decisions + known-issues.
// Zero-model, deterministic. Pure selection lives in core/working_set.hpp.
#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/working_set.hpp"
#include "../../imem/memory_store.hpp"
#include <iostream>
#include <string>
#include <vector>

namespace icmg::cli {

class GovernCommand : public BaseCommand {
public:
    std::string name() const override { return "govern"; }
    std::string description() const override {
        return "Deterministic injection governor: --report fill share, --budget N emit working-set";
    }
    std::string usage() const override {
        return "icmg govern [--report] [--budget <tokens>]";
    }

    int run(const std::vector<std::string>& args) override {
        bool report = false;
        int budget = 4000;  // default injection budget
        for (size_t i = 0; i < args.size(); ++i) {
            if (args[i] == "--report") report = true;
            else if (args[i] == "--budget" && i + 1 < args.size()) budget = std::stoi(args[++i]);
        }

        // Build candidate sources from memory (decisions + known-issues).
        // NOTE: adapt recall() call to the real MemoryStore signature (Step 1).
        imem::MemoryStore mem;
        std::vector<core::Source> candidates;
        auto hits = mem.recall("decisions rules known-issue plan", 40);  // {topic, content, score}
        for (auto& h : hits) {
            core::Source s;
            s.id = h.topic;
            s.text = h.content;
            s.tokens = (int)(h.content.size() / 4);  // ~4 chars/token heuristic
            s.relevance = h.score;
            s.priority = (h.topic.find("decisions") != std::string::npos) ? 2 : 1;
            s.pinned = (h.topic.find("MUTLAK") != std::string::npos);
            candidates.push_back(std::move(s));
        }

        if (report) {
            int total = 0; for (auto& c : candidates) total += c.tokens;
            std::cout << "[govern --report] icmg-injectable candidates: "
                      << candidates.size() << " sources, ~" << total << " tokens.\n"
                      << "Run `icmg context-budget` for the full live-window per-source share.\n"
                      << "F1 honesty: governor only caps icmg-injected context; CC conversation\n"
                      << "turns + UI attachments are outside icmg's control.\n";
            return 0;
        }

        auto ws = core::selectWorkingSet(candidates, budget);
        auto ordered = core::orderUShaped(ws.items);
        std::cout << "[govern] budget=" << budget << " tokens, kept "
                  << ordered.size() << "/" << candidates.size()
                  << " (~" << ws.totalTokens << " tok), U-ordered:\n";
        for (auto& s : ordered) std::cout << "  - " << s.id << "\n";
        return 0;
    }
};

ICMG_REGISTER_COMMAND("govern", GovernCommand)

}  // namespace icmg::cli
```

- [ ] **Step 3: Build (binary only) + manual smoke**

Run: `pwsh -File build.ps1 -Target icmg` then `./build-msvc-full/icmg.exe govern --report` and `./build-msvc-full/icmg.exe govern --budget 500`
Expected: `--report` prints candidate count + F1 honesty note (exit 0); `--budget 500` prints a kept/total line with a U-ordered id list (exit 0). No crash.

- [ ] **Step 4: Record verification**

Run: `icmg verify --command "./build-msvc-full/icmg.exe govern --report"`
Expected: audit entry recorded, exit 0.

- [ ] **Step 5: Commit**

```bash
git add src/cli/commands/govern_cmd.cpp
git commit -m "v2.0.0 C1+C3+F1: icmg govern command (--report fill share, --budget U-ordered emit)"
```

---

### Task 5: Post-change icmg sync (MANDATORY per project CLAUDE.md)

**Files:** none (memory/graph only)

- [ ] **Step 1: Run the five syncs**

```bash
icmg graph update
icmg store --topic "decisions-governor" "P1 core shipped: working_set.hpp selectWorkingSet (budget knapsack, pinned-survive) + orderUShaped (lost-in-middle); icmg govern --report (F1) + --budget emit. Pure cores TDD'd (test_working_set, +1 ctest)."
icmg zone add "src/core/working_set.hpp" --zone governor
icmg wflog add "v2.0.0 P1 core: C1 selectWorkingSet + C3 orderUShaped + govern cmd shipped, TDD"
```

- [ ] **Step 2: Verify build + full ctest gate**

Run: `pwsh -File build.ps1 -Target both -RunTests`
Expected: all tests pass, ctest count = prior + 1.

---

## Self-Review

**Spec coverage (P1 subset):**
- C1 injection-governor selection → Task 1 (`selectWorkingSet`). ✓
- C3 U-shaped ordering → Task 2 (`orderUShaped`). ✓
- F1 measurement honesty → Task 4 `--report`. ✓
- C2/C4/C5/C6/C7 + perplexity → explicitly OUT (separate plans). ✓ (noted in header Assumptions)

**Placeholder scan:** No TBD/TODO; all code blocks concrete. The one adaptation point (MemoryStore `recall` signature) is flagged with an explicit Step-1 read + note, not a silent placeholder — acceptable because the exact method surface must be read from the live codebase and the plan says so.

**Type consistency:** `Source`/`WorkingSet` fields (`id/text/tokens/relevance/priority/pinned`, `items/totalTokens`) identical across Tasks 1, 2, 4. `selectWorkingSet`/`orderUShaped`/`wsScore` names consistent. CMake target name `test_working_set` matches file `tests/core/test_working_set.cpp` and Task 3 registration.

---

## Follow-up plans (NOT in this plan)
- **P2:** C4 lossless transition (snapshot/rebuild + migration `working_set_snapshot`, F2 hard-cap) + C5 idle-compact advisor (Stop hook).
- **P3:** C2 cross-turn dedup + C7 document intake-trim (extend `ingest`).
- **P4:** perplexity backend wiring (extends `compress_select.hpp`).
