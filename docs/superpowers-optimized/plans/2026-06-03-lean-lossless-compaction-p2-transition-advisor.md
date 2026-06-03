# Lean & Lossless Compaction — P2 (C4 transition + C5 advisor) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers-optimized:executing-plans (INLINE — Claude subagent dispatch FORBIDDEN per project rule). Steps use checkbox (`- [ ]`).

**Goal:** Make compaction lossless (snapshot the governor working-set before compaction, rebuild a hard-capped fresh anchor after) and un-surprising (a Stop-hook advisor nudges `/compact` at idle when fill is high).

**Architecture:** Two pure header cores — `ws_snapshot.hpp` (`snapshotManifest`/`rebuildFromManifest` with F2 hard cap) and `compact_advisor.hpp` (`idleCompactAdvice` band-rate-limited) — unit-tested against `tests/test_main.hpp`. A new project-DB migration `0040_working_set_snapshot` persists manifests. `icmg govern snapshot|rebuild` subcommands store/restore; a new Stop hook calls `icmg govern advise` to emit the nudge.

**Tech Stack:** C++17 header cores, SQLite via `core::Db`, `ICMG_REGISTER_COMMAND`, bash Stop hook, CMake `add_icmg_test`.

**Assumptions:**
- Assumes hooks STILL cannot trigger `/compact` (verified June 2026, #58538 open) — C5 is advisory ONLY (prints a nudge; the user runs `/compact`). Will NOT auto-compact.
- Assumes `core::Db` exposes `exec(sql)` and a prepared-statement/query API used by existing stores — Task 5 adapts to the real `Db` surface read at implementation time.
- Assumes `icmg context-budget` (or a callable helper) yields a current fill percentage — if only the command exists (not a header fn), Task 6 shells `icmg context-budget` and parses the percent, or the hook passes fill via env. Documented in Task 6.
- Scope is P2 ONLY (C4 + C5). C2/C7/perplexity are later plans.

---

## File Structure

| File | Responsibility |
|------|----------------|
| `src/core/ws_snapshot.hpp` (create) | Pure `Manifest` type + `snapshotManifest(WorkingSet)` + `rebuildFromManifest(Manifest, hardCapTokens)` (F2). |
| `src/core/compact_advisor.hpp` (create) | Pure `idleCompactAdvice(fillPct, lastFiredBand, thresholdPct)` band-rate-limited nudge. |
| `tests/core/test_ws_snapshot.cpp` (create) | Unit tests for snapshot round-trip + hard cap. |
| `tests/core/test_compact_advisor.cpp` (create) | Unit tests for advisor band logic. |
| `migrations/0040_working_set_snapshot.sql` (create) | On-disk migration copy. |
| `src/core/embedded_migrations.hpp` (modify) | Add `{40, R"SQL(...)SQL"}` entry. |
| `src/cli/commands/govern_cmd.cpp` (modify) | Add `snapshot` / `rebuild` / `advise` subcommands. |
| `.claude/hooks/icmg-stop-compact-advise.sh` (create) | Stop hook → `icmg govern advise`. |
| `CMakeLists.txt` (modify) | Two `add_icmg_test` lines. |

---

### Task 1: Pure snapshot/rebuild core (C4 + F2)

**Files:**
- Create: `src/core/ws_snapshot.hpp`
- Test: `tests/core/test_ws_snapshot.cpp`

**Does NOT cover:** persistence (Task 5) — this is the pure transform only. `rebuildFromManifest` re-emits ONLY the pinned ids up to `hardCapTokens` (F2 anti-thrash); non-pinned manifest entries are dropped on rebuild (they are reconstructed live by the governor, not re-injected from the snapshot).

- [ ] **Step 1: Write failing test**

```cpp
// tests/core/test_ws_snapshot.cpp
// v2.0.0 C4: lossless compaction transition — snapshot the working-set manifest before
// compaction, rebuild a HARD-CAPPED pinned-only anchor after (F2 anti-thrash). Pure.
#include "../test_main.hpp"
#include "../../src/core/ws_snapshot.hpp"
#include "../../src/core/working_set.hpp"
#include <string>
#include <vector>

using namespace icmg::core;

static Source mk(const std::string& id, int tokens, bool pinned) {
    Source s; s.id = id; s.text = id; s.tokens = tokens; s.pinned = pinned; return s;
}

TEST("snapshotManifest: captures all ids + pinned subset") {
    WorkingSet ws;
    ws.items = {mk("a",10,false), mk("PIN",20,true), mk("b",5,false)};
    auto m = snapshotManifest(ws);
    ASSERT_EQ(m.nodeIds.size(), (size_t)3);
    ASSERT_EQ(m.pinnedIds.size(), (size_t)1);
    ASSERT_EQ(m.pinnedIds[0], std::string("PIN"));
}

TEST("rebuildFromManifest: emits pinned only, within hard cap") {
    Manifest m;
    m.nodeIds = {"a","PIN1","b","PIN2"};
    m.pinnedIds = {"PIN1","PIN2"};
    // each pinned costs 100 tokens here; cap 150 fits exactly 1.
    std::vector<Source> live = {mk("PIN1",100,true), mk("PIN2",100,true), mk("a",10,false)};
    auto ws = rebuildFromManifest(m, 150, live);
    ASSERT_EQ(ws.items.size(), (size_t)1);
    ASSERT_TRUE(ws.items[0].pinned);
}

TEST("rebuildFromManifest: drops non-pinned even if in manifest") {
    Manifest m; m.nodeIds = {"a","b"}; m.pinnedIds = {};
    std::vector<Source> live = {mk("a",10,false), mk("b",10,false)};
    auto ws = rebuildFromManifest(m, 1000, live);
    ASSERT_EQ(ws.items.size(), (size_t)0);  // nothing pinned -> nothing re-anchored
}

TEST("rebuildFromManifest: missing live source skipped (fresh-fetch model)") {
    Manifest m; m.nodeIds = {"GONE"}; m.pinnedIds = {"GONE"};
    std::vector<Source> live = {mk("other",10,true)};
    auto ws = rebuildFromManifest(m, 1000, live);
    ASSERT_EQ(ws.items.size(), (size_t)0);  // GONE not in live -> skipped, no stale text
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `pwsh -File build.ps1 -Reconfigure -Target test` then `& 'C:\icmg-build\build-msvc-full\icmg_test.exe' ws_snapshot`
Expected: FAIL — `src/core/ws_snapshot.hpp` missing (compile error).

- [ ] **Step 3: Implement minimal change**

```cpp
// src/core/ws_snapshot.hpp
#pragma once
// v2.0.0 C4: lossless compaction transition. snapshotManifest records the working-set's
// node ids + the pinned subset before compaction. rebuildFromManifest re-anchors ONLY the
// pinned ids, fetched FRESH from the live source list, capped at hardCapTokens (F2: small
// cap prevents the CC thrashing-error loop where a too-large re-injection refills the
// window after each summary). Non-pinned context is NOT restored from the snapshot — the
// governor reconstructs it live next turn. Pure; persistence lives elsewhere.
#include "working_set.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace icmg::core {

struct Manifest {
    std::vector<std::string> nodeIds;
    std::vector<std::string> pinnedIds;
    std::int64_t ts = 0;
};

inline Manifest snapshotManifest(const WorkingSet& ws) {
    Manifest m;
    for (const auto& s : ws.items) {
        m.nodeIds.push_back(s.id);
        if (s.pinned) m.pinnedIds.push_back(s.id);
    }
    return m;
}

// Re-anchor pinned ids only, fetched fresh from `live`, greedily within hardCapTokens.
// Ids absent from `live` are skipped (no stale text). Non-pinned manifest ids ignored.
inline WorkingSet rebuildFromManifest(const Manifest& m, int hardCapTokens,
                                      const std::vector<Source>& live) {
    WorkingSet ws;
    int used = 0;
    for (const auto& pid : m.pinnedIds) {
        for (const auto& s : live) {
            if (s.id != pid) continue;
            if (used + s.tokens <= hardCapTokens) {
                ws.items.push_back(s);
                ws.totalTokens += s.tokens;
                used += s.tokens;
            }
            break;  // matched this pinned id
        }
    }
    return ws;
}

}  // namespace icmg::core
```

- [ ] **Step 4: Run test to verify it passes**

Run: `& 'C:\icmg-build\build-msvc-full\icmg_test.exe' ws_snapshot`
Expected: PASS (4/4).

- [ ] **Step 5: Commit**

```bash
git add src/core/ws_snapshot.hpp tests/core/test_ws_snapshot.cpp
git commit -m "v2.0.0 C4: lossless transition pure core (snapshotManifest + rebuildFromManifest hard-cap F2), 4 TDD"
```

---

### Task 2: Pure idle-compact advisor core (C5)

**Files:**
- Create: `src/core/compact_advisor.hpp`
- Test: `tests/core/test_compact_advisor.cpp`

**Does NOT cover:** triggering `/compact` (hooks cannot — #58538). Emits an advisory string only. Band rate-limit: fire once when fill crosses into a new 10% band at/above threshold; do NOT refire within the same band.

- [ ] **Step 1: Write failing test**

```cpp
// tests/core/test_compact_advisor.cpp
// v2.0.0 C5: idle-compact advisor — nudge /compact at a natural idle moment when context
// fill is high, rate-limited by 10% band so it fires once per band, not every turn. Pure.
#include "../test_main.hpp"
#include "../../src/core/compact_advisor.hpp"
#include <string>

using namespace icmg::core;

TEST("idleCompactAdvice: below threshold -> no fire") {
    auto n = idleCompactAdvice(60, -1, 75);
    ASSERT_TRUE(!n.fire);
}

TEST("idleCompactAdvice: at/above threshold, new band -> fire") {
    auto n = idleCompactAdvice(78, -1, 75);
    ASSERT_TRUE(n.fire);
    ASSERT_TRUE(n.band == 7);            // floor(78/10)
}

TEST("idleCompactAdvice: same band already fired -> no refire") {
    auto n = idleCompactAdvice(79, 7, 75);  // lastFiredBand=7, still band 7
    ASSERT_TRUE(!n.fire);
}

TEST("idleCompactAdvice: next band up -> fire again") {
    auto n = idleCompactAdvice(88, 7, 75);  // band 8 > lastFired 7
    ASSERT_TRUE(n.fire);
    ASSERT_TRUE(n.band == 8);
}

TEST("idleCompactAdvice: message mentions compact + percent") {
    auto n = idleCompactAdvice(82, -1, 75);
    ASSERT_TRUE(n.message.find("compact") != std::string::npos);
    ASSERT_TRUE(n.message.find("82") != std::string::npos);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: build + `& 'C:\icmg-build\build-msvc-full\icmg_test.exe' compact_advisor`
Expected: FAIL — `compact_advisor.hpp` missing.

- [ ] **Step 3: Implement minimal change**

```cpp
// src/core/compact_advisor.hpp
#pragma once
// v2.0.0 C5: idle-compact advisor. When context fill >= threshold AND has crossed into a
// new 10% band since the last nudge, emit an advisory to run /compact now (a natural idle
// moment, not the forced mid-task wall). Hooks cannot trigger /compact (#58538), so this
// is ADVISORY ONLY. Band rate-limit avoids per-turn nagging. Pure.
#include <string>

namespace icmg::core {

struct Nudge {
    bool fire = false;
    int  band = -1;
    std::string message;
};

inline Nudge idleCompactAdvice(int fillPct, int lastFiredBand, int thresholdPct) {
    Nudge n;
    if (fillPct < thresholdPct) return n;       // not high enough
    int band = fillPct / 10;
    if (band <= lastFiredBand) return n;         // already nudged in this (or lower) band
    n.fire = true;
    n.band = band;
    n.message = "[icmg] context " + std::to_string(fillPct) +
                "% — good idle moment to /compact now (lossless: pinned rules re-anchored).";
    return n;
}

}  // namespace icmg::core
```

- [ ] **Step 4: Run test to verify it passes**

Run: `& 'C:\icmg-build\build-msvc-full\icmg_test.exe' compact_advisor`
Expected: PASS (5/5).

- [ ] **Step 5: Commit**

```bash
git add src/core/compact_advisor.hpp tests/core/test_compact_advisor.cpp
git commit -m "v2.0.0 C5: idle-compact advisor pure core (band-rate-limited nudge), 5 TDD"
```

---

### Task 3: Migration 0040 working_set_snapshot

**Files:**
- Create: `migrations/0040_working_set_snapshot.sql`
- Modify: `src/core/embedded_migrations.hpp`

**Does NOT cover:** read/write logic (Task 5). Schema only.

- [ ] **Step 1: Create on-disk migration file**

```sql
-- migrations/0040_working_set_snapshot.sql
-- v2.0.0 C4: per-session working-set manifest captured at PreCompact, rebuilt at
-- PostCompact (pinned-only, hard-capped). Project DB.
CREATE TABLE IF NOT EXISTS working_set_snapshot (
    session_id   TEXT    NOT NULL,
    ts           INTEGER NOT NULL,
    manifest_json TEXT   NOT NULL,
    pinned_json  TEXT    NOT NULL,
    PRIMARY KEY (session_id, ts)
);
CREATE INDEX IF NOT EXISTS idx_wss_session ON working_set_snapshot(session_id);
```

- [ ] **Step 2: Add embedded entry**

In `src/core/embedded_migrations.hpp`, locate the `{39, R"SQL(...)SQL"},` graph_fts entry's closing `)SQL"},` and add immediately after it (before the next migration or the closing `};`):

```cpp
        {40, R"SQL(
-- 0040_working_set_snapshot (v2.0.0 C4)
CREATE TABLE IF NOT EXISTS working_set_snapshot (
    session_id    TEXT    NOT NULL,
    ts            INTEGER NOT NULL,
    manifest_json TEXT    NOT NULL,
    pinned_json   TEXT    NOT NULL,
    PRIMARY KEY (session_id, ts)
);
CREATE INDEX IF NOT EXISTS idx_wss_session ON working_set_snapshot(session_id);
)SQL"},
```

- [ ] **Step 3: Build + verify migration applies (no error on fresh DB)**

Run: `pwsh -File build.ps1 -Reconfigure -Target icmg` then in project dir `& 'C:\icmg-build\build-msvc-full\icmg.exe' doctor` (startup applies migrations)
Expected: no migration error; exit 0.

- [ ] **Step 4: Commit**

```bash
git add migrations/0040_working_set_snapshot.sql src/core/embedded_migrations.hpp
git commit -m "v2.0.0 C4: migration 0040 working_set_snapshot (project DB)"
```

---

### Task 4: Wire both tests into CMake

**Files:**
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Add registrations after `test_working_set` (line ~721)**

```cmake
add_icmg_test(test_ws_snapshot tests/core/test_ws_snapshot.cpp)  # v2.0.0 C4 transition
add_icmg_test(test_compact_advisor tests/core/test_compact_advisor.cpp)  # v2.0.0 C5 advisor
```

- [ ] **Step 2: Reconfigure + build + run the new tests**

Run: `pwsh -File build.ps1 -Reconfigure -Target both` then
`& 'C:\icmg-build\build-msvc-full\icmg_test.exe' ws_snapshot` and `... compact_advisor`
Expected: both PASS (4/4 + 5/5).

- [ ] **Step 3: Commit**

```bash
git add CMakeLists.txt
git commit -m "v2.0.0: register test_ws_snapshot + test_compact_advisor"
```

---

### Task 5: Snapshot storage + govern subcommands (snapshot / rebuild / advise)

**Files:**
- Modify: `src/cli/commands/govern_cmd.cpp`

**Does NOT cover:** the Stop hook script (Task 6). Adds three subcommands. `advise` takes `--fill <pct>` and reads/writes the last-fired band from the snapshot table's most recent row for the session (or a small state file) so band rate-limit persists across turns.

- [ ] **Step 1: Read the live Db API**

Run: `icmg graph symbol Db` and `icmg context src/core/db.hpp --lines 1-60`
Purpose: confirm `exec`/prepared-statement methods + how existing stores write rows (e.g. `compact_bg_cmd` / `agent_leases`). Adapt Step 2 to the real surface.

- [ ] **Step 2: Implement the subcommands**

Add to `GovernCommand::run`, dispatching on `args[0]` when it is `snapshot`/`rebuild`/`advise` (else fall through to existing `--report`/`--budget`). Use the verified `core::Db` write/query API (from Step 1) to INSERT into / SELECT from `working_set_snapshot`. `snapshot`: build candidates (existing code), `selectWorkingSet`, `snapshotManifest`, serialize ids+pinned to JSON, INSERT row keyed by `ICMG_SESSION_ID` (env) + now-ts. `rebuild`: SELECT latest manifest for session, `rebuildFromManifest(m, 2500, liveCandidates)`, print the re-anchored pinned ids. `advise`: parse `--fill <pct>`, SELECT last band for session, call `idleCompactAdvice`, on fire print `n.message` and persist the new band.

```cpp
// sketch — adapt JSON + Db calls to the real surfaces verified in Step 1.
if (!args.empty() && (args[0] == "snapshot" || args[0] == "rebuild" || args[0] == "advise")) {
    const std::string sub = args[0];
    auto& cfg = core::Config::instance();
    core::Db db(cfg.projectDbPath("."));
    const char* sidEnv = std::getenv("ICMG_SESSION_ID");
    std::string sid = sidEnv ? sidEnv : "default";
    // ... build candidates as in --budget path ...
    if (sub == "snapshot") {
        auto ws = core::selectWorkingSet(candidates, 4000);
        auto m  = core::snapshotManifest(ws);
        // serialize m.nodeIds / m.pinnedIds to JSON, INSERT working_set_snapshot row.
        std::cout << "[govern snapshot] " << m.nodeIds.size() << " ids ("
                  << m.pinnedIds.size() << " pinned) saved for session " << sid << "\n";
        return 0;
    }
    if (sub == "rebuild") {
        // SELECT latest manifest_json/pinned_json for sid, parse into Manifest m.
        core::Manifest m; /* fill from row */
        auto ws = core::rebuildFromManifest(m, 2500, candidates);  // F2 hard cap 2500
        std::cout << "[govern rebuild] re-anchored " << ws.items.size()
                  << " pinned (~" << ws.totalTokens << " tok, cap 2500):\n";
        for (auto& s : ws.items) std::cout << "  - " << s.id << "\n";
        return 0;
    }
    if (sub == "advise") {
        int fill = 0;
        for (size_t i = 1; i + 1 < args.size(); ++i)
            if (args[i] == "--fill") fill = std::stoi(args[i+1]);
        int lastBand = -1; /* SELECT last persisted band for sid, else -1 */
        auto n = core::idleCompactAdvice(fill, lastBand, 75);
        if (n.fire) {
            std::cout << n.message << "\n";
            // persist n.band for sid (small state row/file) to enforce band rate-limit.
        }
        return 0;
    }
}
```

- [ ] **Step 3: Build + smoke**

Run: `pwsh -File build.ps1 -Target icmg` then in project dir:
`& 'C:\icmg-build\build-msvc-full\icmg.exe' govern snapshot`,
`& '...\icmg.exe' govern rebuild`,
`& '...\icmg.exe' govern advise --fill 82` (expect nudge), then `advise --fill 84` (expect silent — same band).
Expected: snapshot saves N ids; rebuild prints pinned-only ≤2500 tok; advise fires once per band.

- [ ] **Step 4: Record verification**

Run: `icmg verify --command "icmg govern advise --fill 82"`

- [ ] **Step 5: Commit**

```bash
git add src/cli/commands/govern_cmd.cpp
git commit -m "v2.0.0 C4+C5: govern snapshot/rebuild/advise subcommands (persist manifest + band-limited nudge)"
```

---

### Task 6: Stop-hook advisor wiring

**Files:**
- Create: `.claude/hooks/icmg-stop-compact-advise.sh`

**Does NOT cover:** auto-`/compact` (impossible). The hook computes fill via `icmg context-budget` (parse percent) or an env the harness provides, then calls `icmg govern advise --fill <pct>`; any printed nudge is surfaced to the user. Opt-out: `ICMG_NO_COMPACT_ADVISE=1`.

- [ ] **Step 1: Create the hook (ASCII-only, fail-soft)**

```bash
#!/usr/bin/env bash
# v2.0.0 C5: Stop-hook idle-compact advisor. Fires at turn end (idle). When context fill
# is high, surface a nudge to /compact now (advisory only — hooks cannot trigger compaction
# as of June 2026, anthropics/claude-code#58538). Band rate-limited inside `icmg govern
# advise`. Opt-out: ICMG_NO_COMPACT_ADVISE=1.
set -uo pipefail
[ -n "${ICMG_NO_COMPACT_ADVISE:-}" ] && exit 0
command -v icmg >/dev/null 2>&1 || exit 0
# Derive fill percent from context-budget (best-effort; skip if unavailable).
FILL=$(icmg context-budget --percent 2>/dev/null | tr -dc '0-9' | head -c 3)
[ -z "$FILL" ] && exit 0
MSG=$(icmg govern advise --fill "$FILL" 2>/dev/null)
[ -z "$MSG" ] && exit 0
printf '%s' "$MSG" | icmg hookio emit Stop --ctx-stdin
```

- [ ] **Step 2: Verify hook runs fail-soft (no context-budget percent flag yet = silent)**

Run: `bash .claude/hooks/icmg-stop-compact-advise.sh < /dev/null; echo "rc=$?"`
Expected: `rc=0`, no output when fill unavailable (fail-soft). If `icmg context-budget` lacks `--percent`, that flag is added in a follow-up (out of scope here) — the hook stays fail-soft until then.

- [ ] **Step 3: Commit**

```bash
git add .claude/hooks/icmg-stop-compact-advise.sh
git commit -m "v2.0.0 C5: Stop-hook idle-compact advisor (advisory nudge, fail-soft, opt-out)"
```

---

### Task 7: Post-change icmg sync + full gate

**Files:** none (memory/graph)

- [ ] **Step 1: Five syncs**

```bash
icmg graph update
icmg store --topic "decisions-governor" "P2 shipped: C4 lossless transition (ws_snapshot.hpp snapshotManifest/rebuildFromManifest hard-cap 2500 + migration 0040 working_set_snapshot + govern snapshot/rebuild) + C5 idle-compact advisor (compact_advisor.hpp band-rate-limited + govern advise + Stop hook). Advisory only (hooks cannot trigger /compact #58538)."
icmg zone add "src/core/ws_snapshot.hpp" --zone governor
icmg zone add "src/core/compact_advisor.hpp" --zone governor
icmg wflog add "v2.0.0 P2: C4 transition + C5 advisor shipped, 9 TDD, migration 0040"
```

- [ ] **Step 2: Full ctest gate**

Run: `& 'C:\icmg-build\build-msvc-full\icmg_test.exe'` and count `[FAIL]`.
Expected: 0 FAIL; PASS count = prior 1339 + 9 (4 snapshot + 5 advisor) = 1348.

---

## Self-Review

**Spec coverage (P2 subset):**
- C4 lossless transition → Task 1 (pure) + Task 3 (migration) + Task 5 (snapshot/rebuild subcommands). ✓
- F2 hard cap → `rebuildFromManifest(..., hardCapTokens)` + `rebuild` uses 2500. ✓
- C5 idle-compact advisor → Task 2 (pure) + Task 5 (`advise`) + Task 6 (Stop hook). ✓
- Advisory-only constraint (#58538) → stated in Task 2/5/6 "Does NOT cover". ✓

**Placeholder scan:** Task 5 JSON-serialization + exact `Db` calls are flagged as Step-1-verified adaptations (sketch provided), not silent TODOs — the real Db surface must be read live. Acceptable.

**Type consistency:** `Manifest{nodeIds,pinnedIds,ts}` identical Tasks 1/3/5. `Nudge{fire,band,message}` identical Tasks 2/5. `rebuildFromManifest(Manifest,int,vector<Source>)` signature consistent. Test target names match files. Migration number 0040 consistent (file + embedded entry).

---

## Follow-up plans (NOT in this plan)
- **P3:** C2 cross-turn dedup + C7 document intake-trim (extend `ingest`).
- **P4:** perplexity backend (extends `compress_select.hpp`).
- **Non-governor:** IDE-hang timeout fix (6 unbounded hooks → `"timeout":10`); plan:icmg-agent-live-progress.
