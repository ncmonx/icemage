# Auto-Consolidate Memory Zones — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers-optimized:executing-plans to implement this plan task-by-task (project rule: sub-agents only via icmg, never Claude subagent → inline executing-plans). Steps use checkbox (`- [ ]`) syntax.

**Goal:** Auto-run `memory consolidate` for a zone (opt-in, background, cooldown-gated) when it crosses a size threshold, and replace the every-store `>7` hint with a rate-limited one.

**Architecture:** Pure decision helpers (`imem/auto_consolidate.hpp`) decide *whether* to act; a per-zone file marker under `.icmg/` enforces the cooldown (no DB migration); a small `core::spawnDetached` fires the consolidate process non-blocking; `store_cmd.cpp` wires it after a successful store, choosing auto-run (if enabled) XOR rate-limited hint.

**Tech Stack:** C++17, CMake (`add_icmg_test` mono-test), test harness `tests/test_main.hpp` (`TEST`/`ASSERT_*`), `core::Config` (`getBool`/`getInt`), `core::selfExePath`, Win `CreateProcessA` / POSIX `fork`+`execvp`.

**Assumptions:**
- Assumes `consolidate` is safe to re-run (idempotent — re-collapsing already-collapsed near-dupes is a no-op) — verified: it soft-deletes losers by cosine≥0.92 / Jaccard≥0.85.
- Assumes `.icmg/` dir exists (it holds the project DB) — marker writes target `parent_path(projectDbPath))`.
- Assumes default OFF (`memory.auto_consolidate=false`) → zero behavior change except the hint becomes rate-limited. Will NOT auto-run unless the user opts in.
- Assumes `core::selfExePath()` returns the icmg binary path (used by llm_warm_cmd/update_cmd for self-spawn).

---

## File Structure

- Create `src/imem/auto_consolidate.hpp` — pure decision helpers (`shouldAutoConsolidate`, `shouldShowHint`, `zoneMarkerName`). Header-only.
- Create `src/imem/auto_consolidate.cpp` — marker file I/O (`readMarkerTs`, `writeMarkerTs`) — small, isolated.
- Create `src/core/spawn_detached.hpp` + `src/core/spawn_detached.cpp` — `spawnDetached(argv)` fire-and-forget.
- Modify `src/cli/commands/store_cmd.cpp` — rewire the hint block.
- Create tests: `tests/imem/test_auto_consolidate.cpp`.
- Modify `CMakeLists.txt` — 1 `add_icmg_test` line (pre-approved this session).

---

### Task 1: Pure decision helpers

**Files:**
- Create: `src/imem/auto_consolidate.hpp`
- Test: `tests/imem/test_auto_consolidate.cpp`
- Modify: `CMakeLists.txt`

**Does NOT cover:** pure threshold/cooldown math only — no file I/O, no config read, no spawn. Excludes the "is auto enabled" decision (that is a config read in Task 4).

- [ ] **Step 1: Write failing test**

```cpp
// tests/imem/test_auto_consolidate.cpp
#include "../test_main.hpp"
#include "../../src/imem/auto_consolidate.hpp"

using namespace icmg::imem;

TEST("auto_consolidate: below threshold -> no act") {
    ASSERT_FALSE(shouldAutoConsolidate(999, 1000, /*last*/0, /*now*/1000000, /*cd*/86400));
    ASSERT_FALSE(shouldShowHint(999, 1000, 0, 1000000, 86400));
}

TEST("auto_consolidate: at threshold + cooldown elapsed -> act") {
    ASSERT_TRUE(shouldAutoConsolidate(1000, 1000, 0, 1000000, 86400));
    ASSERT_TRUE(shouldShowHint(1000, 1000, 0, 1000000, 86400));
}

TEST("auto_consolidate: above threshold but within cooldown -> no act") {
    long long now = 1000000, last = now - 100; // 100s ago, cd 86400
    ASSERT_FALSE(shouldAutoConsolidate(5000, 1000, last, now, 86400));
    ASSERT_FALSE(shouldShowHint(5000, 1000, last, now, 86400));
}

TEST("auto_consolidate: threshold <= 0 disables") {
    ASSERT_FALSE(shouldAutoConsolidate(99999, 0, 0, 1000000, 86400));
    ASSERT_FALSE(shouldShowHint(99999, 0, 0, 1000000, 86400));
}

TEST("auto_consolidate: zoneMarkerName sanitizes unsafe chars") {
    ASSERT_EQ(zoneMarkerName("default"), std::string("consolidate-default.ts"));
    ASSERT_EQ(zoneMarkerName("a/b c.d"), std::string("consolidate-a_b_c_d.ts"));
    ASSERT_EQ(zoneMarkerName("_keep"), std::string("consolidate-_keep.ts"));
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `pwsh -File build.ps1 -Target test; & 'C:\icmg-build\build-msvc-full\icmg_test.exe' "auto_consolidate"`
Expected: FAIL to compile — `auto_consolidate.hpp` not found.

- [ ] **Step 3: Implement minimal change**

```cpp
// src/imem/auto_consolidate.hpp
// 2026-06-06: auto-consolidate decision helpers (pure). Feature #6.
#pragma once
#include <string>
#include <cctype>

namespace icmg::imem {

// True iff count >= threshold AND cooldown elapsed since last action.
inline bool shouldAutoConsolidate(int zone_count, int threshold,
                                  long long last_ts, long long now_ts,
                                  long long cooldown_s) {
    if (threshold <= 0) return false;
    if (zone_count < threshold) return false;
    return (now_ts - last_ts) >= cooldown_s;
}

// Same gate; used for the rate-limited hint when auto-consolidate is disabled.
inline bool shouldShowHint(int zone_count, int threshold,
                           long long last_ts, long long now_ts,
                           long long cooldown_s) {
    return shouldAutoConsolidate(zone_count, threshold, last_ts, now_ts, cooldown_s);
}

// Filesystem-safe marker filename for a zone (alnum + '_' kept; others -> '_').
inline std::string zoneMarkerName(const std::string& zone) {
    std::string s = "consolidate-";
    for (char c : zone)
        s += (std::isalnum((unsigned char)c) || c == '_') ? c : '_';
    s += ".ts";
    return s;
}

} // namespace icmg::imem
```

- [ ] **Step 4: Run test to verify it passes**

Run: `pwsh -File build.ps1 -Target test; & 'C:\icmg-build\build-msvc-full\icmg_test.exe' "auto_consolidate"`
Expected: PASS (5 tests).

CMakeLists line (near other `tests/imem/` or `tests/core/` entries, ~line 637):

```cmake
add_icmg_test(test_auto_consolidate tests/imem/test_auto_consolidate.cpp)
```

- [ ] **Step 5: Commit**

```bash
git add src/imem/auto_consolidate.hpp tests/imem/test_auto_consolidate.cpp CMakeLists.txt
git commit -m "feat(memory): auto-consolidate pure decision helpers (threshold+cooldown)"
```

---

### Task 2: Marker file I/O

**Files:**
- Create: `src/imem/auto_consolidate.cpp`
- Modify: `src/imem/auto_consolidate.hpp` (declare the two I/O functions)
- Test: `tests/imem/test_auto_consolidate.cpp` (extend)

**Does NOT cover:** does not decide *when* to write (caller's job in Task 4). Read of a missing/corrupt file returns 0 (cooldown treated elapsed).

- [ ] **Step 1: Write failing test**

Append to `tests/imem/test_auto_consolidate.cpp`:

```cpp
#include <filesystem>
TEST("auto_consolidate: marker round-trip + missing/corrupt -> 0") {
    namespace fs = std::filesystem;
    fs::path p = fs::temp_directory_path() / "icmg_consolidate_marker_test.ts";
    std::error_code ec; fs::remove(p, ec);
    ASSERT_EQ(readMarkerTs(p.string()), 0LL);          // missing -> 0
    writeMarkerTs(p.string(), 1730000000LL);
    ASSERT_EQ(readMarkerTs(p.string()), 1730000000LL); // round-trip
    { std::ofstream f(p); f << "not-a-number"; }
    ASSERT_EQ(readMarkerTs(p.string()), 0LL);          // corrupt -> 0
    fs::remove(p, ec);
}
```

Add `#include <fstream>` to the test if not present.

- [ ] **Step 2: Run test to verify it fails**

Run: `pwsh -File build.ps1 -Target test; & 'C:\icmg-build\build-msvc-full\icmg_test.exe' "marker round-trip"`
Expected: FAIL to compile — `readMarkerTs`/`writeMarkerTs` undeclared.

- [ ] **Step 3: Implement minimal change**

Add to `src/imem/auto_consolidate.hpp` (declarations, after the inline helpers):

```cpp
// Marker I/O (defined in auto_consolidate.cpp). Stores last-action epoch seconds.
long long readMarkerTs(const std::string& path);   // missing/corrupt -> 0
void      writeMarkerTs(const std::string& path, long long ts);
```

```cpp
// src/imem/auto_consolidate.cpp
#include "auto_consolidate.hpp"
#include <fstream>
#include <string>

namespace icmg::imem {

long long readMarkerTs(const std::string& path) {
    std::ifstream f(path);
    if (!f) return 0;
    long long ts = 0;
    if (!(f >> ts)) return 0;   // corrupt / empty -> 0
    return ts < 0 ? 0 : ts;
}

void writeMarkerTs(const std::string& path, long long ts) {
    std::ofstream f(path, std::ios::trunc);
    if (f) f << ts;
}

} // namespace icmg::imem
```

- [ ] **Step 4: Run test to verify it passes**

Run: `pwsh -File build.ps1 -Target test; & 'C:\icmg-build\build-msvc-full\icmg_test.exe' "marker round-trip"`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/imem/auto_consolidate.hpp src/imem/auto_consolidate.cpp tests/imem/test_auto_consolidate.cpp
git commit -m "feat(memory): auto-consolidate cooldown marker file I/O"
```

---

### Task 3: `core::spawnDetached` helper

**Files:**
- Create: `src/core/spawn_detached.hpp`, `src/core/spawn_detached.cpp`

**Does NOT cover:** fire-and-forget only — does NOT wait, capture output, or report child exit. Caller must not depend on completion. No unit test (process boundary) — smoke-verified in Task 4.

- [ ] **Step 1: (no unit test — process spawn)**

Rationale documented: spawning a detached process cannot be asserted in-process deterministically. Verified by Task 4 smoke (telemetry / marker observed after a real store).

- [ ] **Step 2: Implement**

```cpp
// src/core/spawn_detached.hpp
// 2026-06-06: fire-and-forget detached process launch (no wait, no capture).
#pragma once
#include <string>
#include <vector>

namespace icmg::core {
// Launch argv[0] with argv[1..] detached. Returns true if spawn was issued.
bool spawnDetached(const std::vector<std::string>& argv);
}
```

```cpp
// src/core/spawn_detached.cpp
#include "spawn_detached.hpp"
#ifdef _WIN32
  #include <windows.h>
#else
  #include <unistd.h>
  #include <sys/types.h>
  #include <vector>
#endif

namespace icmg::core {

bool spawnDetached(const std::vector<std::string>& argv) {
    if (argv.empty()) return false;
#ifdef _WIN32
    // Build a quoted command line.
    std::string cmd;
    for (size_t i = 0; i < argv.size(); ++i) {
        if (i) cmd += ' ';
        cmd += '"' + argv[i] + '"';
    }
    std::vector<char> buf(cmd.begin(), cmd.end());
    buf.push_back(0);
    STARTUPINFOA si{}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessA(
        nullptr, buf.data(), nullptr, nullptr, FALSE,
        DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP | CREATE_NO_WINDOW,
        nullptr, nullptr, &si, &pi);
    if (!ok) return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
#else
    pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        setsid();
        std::vector<char*> cargv;
        for (auto& a : argv) cargv.push_back(const_cast<char*>(a.c_str()));
        cargv.push_back(nullptr);
        execvp(cargv[0], cargv.data());
        _exit(127); // exec failed
    }
    return true; // parent
#endif
}

} // namespace icmg::core
```

- [ ] **Step 3: Build (compiles into icmg_lib via GLOB)**

Run: `pwsh -File build.ps1 -Target test`
Expected: builds clean (no new test; verified by link success).

- [ ] **Step 4: Commit**

```bash
git add src/core/spawn_detached.hpp src/core/spawn_detached.cpp
git commit -m "feat(core): spawnDetached fire-and-forget process launch"
```

---

### Task 4: Rewire store_cmd hint block

**Files:**
- Modify: `src/cli/commands/store_cmd.cpp`

**Does NOT cover:** auto-run requires `memory.auto_consolidate=true` (default false) — with auto OFF, only the rate-limited hint changes; nothing spawns. Excludes: zones below threshold (no marker write, no hint, no spawn — silent, as today below 7 was). Excludes non-`store` write paths (memoir/atomize write elsewhere — out of scope).

- [ ] **Step 1: Write failing test**

The decision logic is already unit-tested (Task 1). This task is integration glue; verify via the existing helper tests + a manual smoke (Step 4). No new unit test (spawn + config + DB are process/IO bound). Add an assertion-free smoke note instead.

- [ ] **Step 2: (build-gated)**

Run: `pwsh -File build.ps1 -Target test`
Expected: compiles after Step 3.

- [ ] **Step 3: Implement — replace the `zone_count > 7` block**

In `src/cli/commands/store_cmd.cpp`, add includes near the top:

```cpp
#include "../../imem/auto_consolidate.hpp"
#include "../../core/spawn_detached.hpp"
#include "../../core/exec_utils.hpp"   // selfExePath (if not already included)
#include <filesystem>
```

Replace lines ~88-94 (the `bool show_hint = zone_count > 7;` … hint-build block) with:

```cpp
            // 2026-06-06 (#6): threshold + cooldown gated. Auto-run consolidate
            // when opted in; otherwise a rate-limited hint. Replaces the old
            // unconditional `zone_count > 7` nudge that spammed every store.
            std::string z = node.zone.empty() ? std::string("default") : node.zone;
            int  threshold = cfg.getInt("memory.auto_consolidate_threshold", 1000);
            long long cooldown = cfg.getInt("memory.auto_consolidate_cooldown_s", 86400);
            long long now_s = (long long)std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            std::string marker = (std::filesystem::path(cfg.projectDbPath("."))
                                      .parent_path() / imem::zoneMarkerName(z)).string();
            long long last_ts = imem::readMarkerTs(marker);

            bool show_hint = false;
            std::string hint;
            if (cfg.getBool("memory.auto_consolidate", false)) {
                if (imem::shouldAutoConsolidate(zone_count, threshold, last_ts, now_s, cooldown)) {
                    imem::writeMarkerTs(marker, now_s);   // before spawn (herd guard)
                    core::spawnDetached({ core::selfExePath(), "memory", "consolidate",
                                          "--zone", z });
                    show_hint = true;
                    hint = "[auto-consolidate] zone '" + z + "' (" + std::to_string(zone_count)
                         + ") -> background consolidate started";
                }
            } else {
                if (imem::shouldShowHint(zone_count, threshold, last_ts, now_s, cooldown)) {
                    imem::writeMarkerTs(marker, now_s);   // rate-limit the hint
                    show_hint = true;
                    hint = "zone '" + z + "' has " + std::to_string(zone_count)
                         + " entries; consider 'icmg memory consolidate --zone " + z + "'";
                }
            }
```

The existing output code (json `warnings` + text `[hint]`, lines ~96-107) is unchanged —
it already keys off `show_hint` / `hint`.

Note: confirm `<chrono>` is included in store_cmd (line ~60 already uses system_clock → yes).

- [ ] **Step 4: Build + gate + smoke**

Run: `pwsh -File build.ps1 -Target test`
then: `ctest --test-dir 'C:\icmg-build\build-msvc-full' -R icmg_test --output-on-failure`
Expected: 1553+5 pass / 7 hookio-fail (pre-existing). No regression.

Smoke (manual):
```
# default OFF -> rate-limited hint (not every store)
icmg store --topic test "x"   # hint appears once; immediate re-store: NO hint (cooldown)
# opt-in:
icmg config set memory.auto_consolidate true   # or edit ~/.icmg/config.json
icmg config set memory.auto_consolidate_threshold 1
icmg store --topic test "y"   # -> "[auto-consolidate] ... background consolidate started"
# verify marker:
type .icmg\consolidate-default.ts   # epoch seconds present
```

- [ ] **Step 5: Commit**

```bash
git add src/cli/commands/store_cmd.cpp
git commit -m "feat(memory): auto-consolidate trigger + rate-limited hint in store"
```

---

## Final Verification

- [ ] Full gate: `ctest --test-dir 'C:\icmg-build\build-msvc-full' -R icmg_test --output-on-failure` — all pass except the 7 pre-existing hookio console-artifacts.
- [ ] Smoke (Task 4) confirms: hint rate-limited when OFF; detached consolidate + marker when ON.
- [ ] Post-change sync: `icmg store --topic decisions-memory "..."` + `icmg wflog add "..."` + `icmg graph update`.

## Self-Review

- **Spec coverage:** pure helpers (T1) ✓ · marker I/O (T2) ✓ · spawnDetached (T3) ✓ · store rewire + config keys + auto-run-XOR-hint (T4) ✓ · hint-fix (T4, replaces >7) ✓ · default OFF (T4 getBool default false) ✓ · soft-delete reuse (spawns existing consolidate) ✓.
- **Type consistency:** `shouldAutoConsolidate`/`shouldShowHint`(int,int,long long,long long,long long), `zoneMarkerName(string)`, `readMarkerTs(string)->long long`, `writeMarkerTs(string,long long)`, `spawnDetached(vector<string>)->bool`, config keys `memory.auto_consolidate`/`_threshold`/`_cooldown_s` — used identically across tasks.
- **Placeholder scan:** T3/T4 have no unit test by design (process/IO boundary) — flagged with rationale + smoke, not a silent gap.
