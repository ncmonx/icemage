# Pending Backlog Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers-optimized:executing-plans to implement this plan task-by-task.

**Goal:** Implement #1084, #1116, #1130 T1-T2, #1111 T1 — the feasible unfinished backlog items from the BELUM dikerjakan list.

**Architecture:** Four independent changes: (1) DB PRAGMA additions in `applyPragmas()`; (2) new SessionStart hook script injected by `icmg init`; (3) new `icmg session` command writing `~/.icmg/active-work.json` + `icmg wake-up` reading it; (4) `icmg fail sync-denials` subcommand that auto-converts strict-denial log entries to fail memory, called from the Stop hook.

**Tech Stack:** C++17, SQLite3, nlohmann/json, bash scripts, CMake

**Assumptions:**
- `~/.icmg/strict-denials.jsonl` exists when violations have occurred (written by bash-rewrite hook)
- `icmg wake-up` already outputs to stdout — wrapping with jq for SessionStart hookSpecificOutput is sufficient
- `page_size` PRAGMA is a no-op on existing DBs; only `mmap_size` has immediate effect — acceptable
- #1091 T11/T13/T14 deferred: T13/T14 blocked on ONNX status; T11 (progressive disclosure) needs UX design session
- #1130 T3-T12 deferred: heartbeat + full cross-session conflict detection planned for v0.53.0
- #1111 T2-T5 deferred: salience scoring + session violation counter need hook infrastructure redesign
- #1129 deferred: no plan spec found in memory — requires user to provide requirements
- #1094 zstd deferred: requires SQLite custom extension or schema migration for compressed content column — significant scope

---

## Files Modified / Created

| File | Change |
| --- | --- |
| `src/core/db.cpp` | Add `page_size` + `mmap_size` PRAGMAs to `applyPragmas()` |
| `src/cli/commands/init_cmd.cpp` | Add `WAKEUP_SESSION_SH` constant + write file + SessionStart hook entry + Stop hook `sync-denials` |
| `src/cli/commands/wakeup_cmd.cpp` | Read `~/.icmg/active-work.json`, inject "Active sessions" section |
| `src/cli/commands/fail_cmd.cpp` | Add `sync-denials` subcommand |
| `src/cli/commands/session_cmd.cpp` | **Create** — new `icmg session` command (claim/clear/list) |

---

### Task 1: #1116 — DB mmap_size + page_size pragma

**Files:**
- Modify: `src/core/db.cpp`

**Does NOT cover:** zstd content-column compression (separate migration required); WAL already done.

- [ ] **Step 1: Modify `applyPragmas()` in `src/core/db.cpp`**

After line 105 (`run("PRAGMA cache_size=-8000")`), add:

```cpp
    run("PRAGMA page_size=4096");       // effective only on new DBs; no-op on existing
    run("PRAGMA mmap_size=268435456");  // 256 MB mmap — read pages skip syscall
```

- [ ] **Step 2: Build**

```bash
cmake --build build --parallel
```
Expected: zero errors

- [ ] **Step 3: Verify pragma applied**

```bash
./build/icmg.exe doctor
```
Expected: no DB errors

- [ ] **Step 4: Commit**

```bash
git add src/core/db.cpp
git commit -m "perf(db): add mmap_size=256MB + page_size=4096 PRAGMAs"
```

---

### Task 2: #1084 — Wake-up SessionStart inject

**Files:**
- Modify: `src/cli/commands/init_cmd.cpp`

**Does NOT cover:** `--hooks-only` flag and `icmg upgrade` auto-call (planned for separate task).

- [ ] **Step 1: Add `WAKEUP_SESSION_SH` script constant**

After the `CONTEXT_SESSION_SH` constant (around line 240), insert:

```cpp
// #1084: SessionStart wake-up injection.
static const char* WAKEUP_SESSION_SH = R"BASH(#!/usr/bin/env bash
# Auto-installed by `icmg init`. Fires on SessionStart.
# Injects icmg wake-up briefing at start of every AI session.
set -uo pipefail
CONTENT=$(icmg wake-up 2>/dev/null) || true
[[ -z "$CONTENT" ]] && exit 0
jq -n --arg m "$CONTENT" '{hookSpecificOutput:{hookEventName:"SessionStart",additionalContext:$m}}'
)BASH";
```

- [ ] **Step 2: Write script to disk**

Find the block writing `icmg-context-session.sh` (around line 679). Add immediately after:

```cpp
n += writeFile(root / ".claude" / "hooks" / "icmg-wakeup-session.sh", WAKEUP_SESSION_SH, force);
```

- [ ] **Step 3: Add to SessionStart hooks array**

Find lines 846–854 (the `cfg["hooks"]["SessionStart"]` array). Add a third entry:

```cpp
cfg["hooks"]["SessionStart"] = json::array({
    {
        {"hooks", json::array({
            {{"type", "command"},
             {"command", "[ -f .claude/hooks/icmg-caveman-prompt.sh ] && bash .claude/hooks/icmg-caveman-prompt.sh || exit 0"}},
            {{"type", "command"},
             {"command", "[ -f .claude/hooks/icmg-context-session.sh ] && bash .claude/hooks/icmg-context-session.sh || exit 0"}},
            {{"type", "command"},
             {"command", "[ -f .claude/hooks/icmg-wakeup-session.sh ] && bash .claude/hooks/icmg-wakeup-session.sh || exit 0"}}
        })}
    }
});
```

- [ ] **Step 4: Build + verify**

```bash
cmake --build build --parallel
./build/icmg.exe init --force --no-scan --no-backup --no-maintain --no-mirror --no-sentinel --no-auto-upgrade
```
Expected: `.claude/hooks/icmg-wakeup-session.sh` created; SessionStart array in `.claude/settings.local.json` has 3 entries.

- [ ] **Step 5: Commit**

```bash
git add src/cli/commands/init_cmd.cpp
git commit -m "feat(init): inject icmg wake-up into SessionStart hook (#1084)"
```

---

### Task 3: #1130 T1 — `icmg session` command + active-work.json

**Files:**
- Create: `src/cli/commands/session_cmd.cpp`
- Modify: `src/cli/commands/wakeup_cmd.cpp`

**Does NOT cover:** T2 (file-level claim), T3 (heartbeat auto-expire), T4-T12. Active-work.json uses JSON file at `~/.icmg/active-work.json`; no DB migration needed.

**active-work.json schema:**
```json
{
  "sessions": [
    {
      "pid": 12345,
      "task": "Implement cross-session awareness",
      "started_at": 1778693478,
      "last_seen": 1778693478
    }
  ]
}
```

- [ ] **Step 1: Create `src/cli/commands/session_cmd.cpp`**

```cpp
// #1130 T1: `icmg session` — cross-session task awareness.
// Writes/reads ~/.icmg/active-work.json so icmg wake-up can show active tasks.

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/path_utils.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <ctime>
#ifdef _WIN32
#  include <windows.h>
#endif

namespace fs = std::filesystem;
using nlohmann::json;

namespace icmg::cli {

static fs::path activeWorkPath() {
    auto& cfg = core::Config::instance();
    fs::path global = fs::path(cfg.globalDbDir());
    return global / "active-work.json";
}

static json loadActiveWork() {
    auto p = activeWorkPath();
    if (!fs::exists(p)) return {{"sessions", json::array()}};
    try {
        std::ifstream f(p);
        return json::parse(f);
    } catch (...) {
        return {{"sessions", json::array()}};
    }
}

static void saveActiveWork(const json& j) {
    auto p = activeWorkPath();
    fs::create_directories(p.parent_path());
    std::ofstream f(p);
    f << j.dump(2);
}

static int64_t getPid() {
#ifdef _WIN32
    return (int64_t)GetCurrentProcessId();
#else
    return (int64_t)getpid();
#endif
}

class SessionCommand : public BaseCommand {
public:
    std::string name()        const override { return "session"; }
    std::string description() const override { return "Cross-session task awareness (claim/clear/list)"; }

    void usage() const override {
        std::cout <<
            "Usage: icmg session <subcommand> [args]\n\n"
            "Subcommands:\n"
            "  claim <task>   Register active task in ~/.icmg/active-work.json\n"
            "  clear          Remove this process's entry\n"
            "  list           Show all active sessions\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || hasFlag(args, "--help")) { usage(); return 0; }
        const std::string& sub = args[0];

        if (sub == "claim") {
            if (args.size() < 2) {
                std::cerr << "icmg session claim: task description required\n";
                return 1;
            }
            std::string task;
            for (size_t i = 1; i < args.size(); ++i) {
                if (i > 1) task += " ";
                task += args[i];
            }
            return runClaim(task);
        }
        if (sub == "clear")  return runClear();
        if (sub == "list")   return runList();
        std::cerr << "icmg session: unknown subcommand '" << sub << "'\n";
        return 1;
    }

private:
    int runClaim(const std::string& task) {
        auto j = loadActiveWork();
        int64_t pid = getPid();
        int64_t now = (int64_t)std::time(nullptr);

        // Remove stale entry for this PID if present.
        auto& sessions = j["sessions"];
        sessions.erase(std::remove_if(sessions.begin(), sessions.end(),
            [&](const json& s){ return s.value("pid", 0LL) == pid; }),
            sessions.end());

        sessions.push_back({
            {"pid",        pid},
            {"task",       task},
            {"started_at", now},
            {"last_seen",  now}
        });
        saveActiveWork(j);
        std::cout << "icmg session: claimed task '" << task << "' (pid=" << pid << ")\n";
        return 0;
    }

    int runClear() {
        auto j = loadActiveWork();
        int64_t pid = getPid();
        auto& sessions = j["sessions"];
        auto before = sessions.size();
        sessions.erase(std::remove_if(sessions.begin(), sessions.end(),
            [&](const json& s){ return s.value("pid", 0LL) == pid; }),
            sessions.end());
        saveActiveWork(j);
        std::cout << "icmg session: cleared " << (before - sessions.size()) << " entry\n";
        return 0;
    }

    int runList() {
        auto j = loadActiveWork();
        const auto& sessions = j["sessions"];
        if (sessions.empty()) {
            std::cout << "(no active sessions)\n";
            return 0;
        }
        for (const auto& s : sessions) {
            std::cout << "  pid=" << s.value("pid", 0LL)
                      << "  task=" << s.value("task", "")
                      << "\n";
        }
        return 0;
    }
};

ICMG_REGISTER_COMMAND("session", SessionCommand);

} // namespace icmg::cli
```

- [ ] **Step 2: Inject active-work into `icmg wake-up` output**

In `src/cli/commands/wakeup_cmd.cpp`, add after the "Recall queries" section (before final `std::cout` call):

First add the include at the top (after existing includes):
```cpp
#include "../../core/path_utils.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
```

Then add a helper and inject into output (in the `run()` method, after the "Recall queries" block):

```cpp
        // #1130 T1: active sessions from ~/.icmg/active-work.json
        {
            auto& cfg2 = core::Config::instance();
            fs::path awp = fs::path(cfg2.globalDbDir()) / "active-work.json";
            if (fs::exists(awp)) {
                try {
                    std::ifstream f(awp);
                    auto j = json::parse(f);
                    const auto& sessions = j["sessions"];
                    if (!sessions.empty()) {
                        out << "\nActive sessions:\n";
                        for (const auto& s : sessions) {
                            out << "  pid=" << s.value("pid", 0LL)
                                << "  " << s.value("task", "") << "\n";
                        }
                    }
                } catch (...) {}
            }
        }
```

- [ ] **Step 3: Build**

```bash
cmake --build build --parallel
```
Expected: compiles cleanly.

- [ ] **Step 4: Smoke test**

```bash
./build/icmg.exe session claim "Testing cross-session awareness"
./build/icmg.exe session list
./build/icmg.exe wake-up | grep -A3 "Active sessions"
./build/icmg.exe session clear
./build/icmg.exe wake-up | grep "Active sessions" || echo "OK: no active sessions shown"
```

- [ ] **Step 5: Commit**

```bash
git add src/cli/commands/session_cmd.cpp src/cli/commands/wakeup_cmd.cpp
git commit -m "feat(session): icmg session claim/clear/list + wake-up active-work injection (#1130 T1)"
```

---

### Task 4: #1111 T1 — Auto fail-store on strict-denial violations

**Files:**
- Modify: `src/cli/commands/fail_cmd.cpp`
- Modify: `src/cli/commands/init_cmd.cpp` (Stop hook)

**Does NOT cover:** T2 (verification gate mechanical), T3 (adaptive repetition), T4 (salience scoring), T5 (violation counter). Only auto-converts `strict-denials.jsonl` entries to `icmg fail` memory.

- [ ] **Step 1: Add `sync-denials` subcommand to `fail_cmd.cpp`**

Read existing fail_cmd.cpp structure first, then add `sync-denials` subcommand:

```cpp
// In the run() dispatch block, add:
if (sub == "sync-denials") return runSyncDenials();
```

Add the implementation method:

```cpp
int runSyncDenials() {
    // Read ~/.icmg/strict-denials.jsonl, convert new entries to fail memory.
    // Uses ~/.icmg/sync-denials-offset.txt to track last-processed line.
    auto& cfg = core::Config::instance();
    fs::path denials_path = fs::path(cfg.globalDbDir()) / "strict-denials.jsonl";
    fs::path offset_path  = fs::path(cfg.globalDbDir()) / "sync-denials-offset.txt";

    if (!fs::exists(denials_path)) return 0;

    // Read last offset.
    int64_t last_offset = 0;
    if (fs::exists(offset_path)) {
        std::ifstream f(offset_path);
        f >> last_offset;
    }

    std::ifstream f(denials_path);
    f.seekg(last_offset);
    std::string line;
    int stored = 0;
    int64_t new_offset = last_offset;
    core::Db db(cfg.projectDbPath("."));

    while (std::getline(f, line)) {
        new_offset = (int64_t)f.tellg();
        if (line.empty()) continue;
        try {
            auto j = json::parse(line);
            std::string hook   = j.value("hook", "");
            std::string target = j.value("target", "");
            std::string reason = j.value("reason", "");
            if (target.empty() || reason.empty()) continue;

            // Store as fail memory: pattern = "hook:target", fix = bypass instruction.
            std::string pattern = hook + ": " + target;
            std::string fix     = "Use icmg equivalent. Reason: " + reason;
            db.run(
                "INSERT OR IGNORE INTO memory_nodes(topic,content,importance,created_at,last_used) "
                "VALUES('fail-auto',?,2,strftime('%s','now'),strftime('%s','now'))",
                {pattern + " — " + fix}
            );
            ++stored;
        } catch (...) {}
    }

    // Persist new offset.
    if (stored > 0) {
        std::ofstream of(offset_path);
        of << new_offset;
        std::cout << "icmg fail sync-denials: stored " << stored << " violation(s)\n";
    }
    return 0;
}
```

**Note:** `fail_cmd.cpp` uses `icmg::cli` namespace and has a `BaseCommand` subclass. Add `sync-denials` to its `usage()` and dispatch block. Check exact dispatch pattern from existing subcommands (store/recall/list/stats).

- [ ] **Step 2: Add `sync-denials` call to Stop hook in `init_cmd.cpp`**

Find the Stop hook block (around line 888–899). Add a new command entry:

```cpp
{{"type", "command"},
 {"command", "icmg fail sync-denials 2>/dev/null || true"}},
```

Place it after the `icmg distill auto` call but before the wflog-stop call.

- [ ] **Step 3: Build**

```bash
cmake --build build --parallel
```

- [ ] **Step 4: Smoke test**

```bash
# Trigger a violation by running a raw grep (hook will deny + write to strict-denials.jsonl)
# Then manually run sync:
./build/icmg.exe fail sync-denials
./build/icmg.exe fail list | head -10
```

- [ ] **Step 5: Commit**

```bash
git add src/cli/commands/fail_cmd.cpp src/cli/commands/init_cmd.cpp
git commit -m "feat(fail): auto-convert strict-denial violations to fail memory on Stop (#1111 T1)"
```

---

### Task 5: Final build + version bump + full test suite

- [ ] **Step 1: Run full test suite**

```bash
ctest --test-dir build --output-on-failure
```
Expected: all existing tests pass.

- [ ] **Step 2: Verify `icmg init` produces correct hooks**

```bash
./build/icmg.exe init --force --no-scan --no-backup --no-maintain --no-mirror --no-sentinel --no-auto-upgrade
cat .claude/settings.local.json | python -m json.tool | grep -A5 "SessionStart"
ls .claude/hooks/icmg-wakeup-session.sh
```

- [ ] **Step 3: Commit version bump** (after all tasks pass)

Bump version in `CMakeLists.txt` and `src/main.cpp` to `0.52.0`.

```bash
git add CMakeLists.txt src/main.cpp
git commit -m "feat(v0.52.0): wake-up SessionStart, mmap pragma, session claim, fail sync-denials"
```

---

## Deferred Items

| Item | Reason |
| --- | --- |
| #1091 T11 (progressive disclosure) | Needs UX design — expand/collapse API not yet defined |
| #1091 T13/T14 (ONNX cosine) | Blocked: need to confirm ONNX runtime availability status |
| #1111 T2-T5 | Needs hook infrastructure redesign (salience scoring, adaptive freq) |
| #1129 (README rules enforcement) | No plan spec found in memory — needs user requirements |
| #1130 T2 (file claim) | Depends on T1 landing; plan separately for v0.53.0 |
| #1130 T3-T12 | Depends on T1+T2; heartbeat + conflict detection separate phase |
| #1094 zstd (DB compression) | Requires schema migration + compression/decompression layer — large scope |
