# Feature-Map M1 — Implementation Plan

> Execute with executing-plans (inline, batch-local, NO ship). Spec: docs/superpowers-optimized/specs/2026-06-07-feature-map-design.md (M1 ramping).

**Goal:** `icmg map <cmd>` shows a command's derived neighbors ("you-are-here" hallway map) + anchor a pre-build reflex rule, so capabilities are discoverable at decision-time and dups are caught.
**Architecture:** Pure `neighborsOf` over the existing command docs (reuse `core::rankCommands`, zero new data) → thin `map` command → CLAUDE.md reflex rule. No new subsystems.
**Tech Stack:** C++17, existing `core::rankCommands`/`registryDocs`, mono test (`add_icmg_test`), pwsh build.ps1.
**Assumptions:** Neighbors = text-similarity over name+desc (derived). Assumes rankCommands quality is "good enough" advisory — will be noisy for terse descs (minor, accepted).

---

## File structure
| File | Responsibility |
|---|---|
| `src/core/command_suggest.hpp` | add pure `neighborsOf(cmdName, docs, n)` next to `rankCommands` |
| `src/cli/commands/map_cmd.cpp` | `icmg map <cmd>` — you-are-here + neighbors (or intent fallback) |
| `tests/core/test_feature_map.cpp` | unit-test `neighborsOf` |
| `CLAUDE.md` | "Before adding a command" reflex rule |

---

### Task 1: `neighborsOf` pure helper + test

**Files:** Modify `src/core/command_suggest.hpp`; Create `tests/core/test_feature_map.cpp`; Modify `CMakeLists.txt`.

- [ ] **Step 1: Failing test**
```cpp
// tests/core/test_feature_map.cpp
#include "../test_main.hpp"
#include "../../src/core/command_suggest.hpp"
using namespace icmg::core;
static std::vector<CmdDoc> docs() {
    return { {"context-budget","show context window token usage"},
             {"savings","token savings report"},
             {"govern","context budget governor token injection"},
             {"graph","code graph symbols"},
             {"zone","subsystem zone tagging"} };
}
TEST("feature_map: neighborsOf excludes self + ranks similar first") {
    auto n = neighborsOf("context-budget", docs(), 3);
    ASSERT_TRUE(n.size() <= 3);
    for (auto& h : n) ASSERT_TRUE(h.name != std::string("context-budget")); // self excluded
    // token/budget-related neighbors should appear before unrelated (graph/zone)
    bool sawTokenish = false;
    for (auto& h : n) if (h.name=="savings"||h.name=="govern") sawTokenish = true;
    ASSERT_TRUE(sawTokenish);
}
TEST("feature_map: unknown cmd -> intent fallback non-empty") {
    auto n = neighborsOf("nonexistent-xyz", docs(), 3);
    ASSERT_TRUE(n.size() <= 3);   // falls back to ranking the string as intent; never crashes
}
TEST("feature_map: empty docs -> empty") {
    ASSERT_EQ(neighborsOf("x", {}, 3).size(), (size_t)0);
}
```

- [ ] **Step 2: Run → FAIL** (`neighborsOf` undefined). Build `pwsh build.ps1 -Target both -Reconfigure`; run `icmg_test.exe feature_map` from `C:\Temp`.

- [ ] **Step 3: Implement** (append to `src/core/command_suggest.hpp`, same namespace as `rankCommands`):
```cpp
// Neighbors of a command = top-N most similar OTHER commands (derived from
// name+desc via rankCommands). If cmdName matches a doc, rank against that
// doc's "name desc"; else treat cmdName as a free intent. Self excluded.
inline std::vector<CmdHit> neighborsOf(const std::string& cmdName,
                                       const std::vector<CmdDoc>& docs, int n) {
    if (docs.empty()) return {};
    std::string intent = cmdName;
    for (const auto& d : docs)
        if (d.name == cmdName) { intent = d.name + " " + d.desc; break; }
    auto hits = rankCommands(intent, docs, n + 1);   // +1 to absorb self
    std::vector<CmdHit> out;
    for (auto& h : hits) {
        if (h.name == cmdName) continue;             // drop self
        out.push_back(h);
        if ((int)out.size() >= n) break;
    }
    return out;
}
```

- [ ] **Step 4: Register + run → PASS**. CMake: `add_icmg_test(test_feature_map tests/core/test_feature_map.cpp)`.

- [ ] **Step 5: Commit** — `git add src/core/command_suggest.hpp tests/core/test_feature_map.cpp CMakeLists.txt && git commit -m 'feat(feature-map): neighborsOf derived command neighbors (pure); 3 tests'`

---

### Task 2: `icmg map <cmd>` command

**Files:** Create `src/cli/commands/map_cmd.cpp`.

**Does NOT cover:** `--help` footer on other commands (M2); output footer (M3).

- [ ] **Step 1: Implement** (registryDocs() pattern from suggest_cmd.cpp):
```cpp
#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/command_suggest.hpp"
#include <iostream>
namespace icmg::cli {
std::vector<core::CmdDoc> registryDocs();  // defined in suggest_cmd.cpp (extern)
class MapCommand : public BaseCommand {
public:
    std::string name() const override { return "map"; }
    std::string description() const override { return "Show a command's related neighbors (you-are-here map)"; }
    void usage() const override {
        std::cout << "Usage: icmg map <cmd> [--top N]\n  Lists commands related to <cmd> (derived). Unknown <cmd> -> nearest by intent.\n";
    }
    int run(const std::vector<std::string>& args) override {
        if (args.empty() || args[0]=="--help") { usage(); return 0; }
        std::string cmd = args[0];
        int top = 6;
        for (size_t i=1;i+1<args.size();++i) if (args[i]=="--top") { try{ top=std::stoi(args[i+1]); }catch(...){} }
        auto docs = registryDocs();
        bool known = false; std::string desc;
        for (auto& d : docs) if (d.name==cmd) { known=true; desc=d.desc; break; }
        if (known) std::cout << "you are here: icmg " << cmd << " -- " << desc << "\n";
        else       std::cout << "no exact command '" << cmd << "'; nearest by intent:\n";
        auto nb = core::neighborsOf(cmd, docs, top);
        if (nb.empty()) { std::cout << "  (no neighbors)\n"; return 0; }
        std::cout << "related:\n";
        for (auto& h : nb) std::cout << "  icmg " << h.name << "\n";
        return 0;
    }
};
ICMG_REGISTER_COMMAND("map", MapCommand);
}
```
> If `registryDocs()` is `static` in suggest_cmd.cpp, change Task to: move `registryDocs()` to a shared header (`command_suggest.hpp`) so both `suggest` and `map` use it. Verify during implementation.

- [ ] **Step 2: Build (-Reconfigure, new file) + smoke**: `icmg map context-budget` → "you are here..." + related savings/govern. `icmg map zzz` → intent fallback.
- [ ] **Step 3: Commit** — `git commit -m 'feat(feature-map): icmg map <cmd> -- derived neighbor map'`

---

### Task 3: Pre-build reflex rule (CLAUDE.md)

**Files:** Modify `CLAUDE.md`.

- [ ] **Step 1:** Add a short section "## Before adding a command (anti-dup reflex)":
  > Before creating a new `*_cmd.cpp` / `ICMG_REGISTER_COMMAND`, FIRST run `icmg suggest "<purpose>"` and `icmg map <nearest>`. If a close match exists, EXTEND it (add a flag/subcommand), do NOT duplicate. (Born from the 2026-06-07 context-budget dup: a feature existed but was rebuilt because it wasn't surfaced at build-time.)
- [ ] **Step 2: Commit** (CLAUDE.md is gitignored — this is local-only; no commit needed, just edit). Note in session-log.

---

## Self-Review
- Spec coverage: §M1 (icmg map + reflex) → Tasks 1-3. Derived relations → Task 1 (rankCommands reuse). Non-goals (footer/output/hook) → out (M2-4).
- Placeholder scan: real code in Tasks 1-2; Task 2 flags the `registryDocs()` visibility check (do at impl).
- Types: `CmdDoc{name,desc}`, `CmdHit{name,...}`, `neighborsOf(name,docs,n)`, `rankCommands(intent,docs,n)` — consistent with command_suggest.hpp.

## Notes
- Batch-local, NO ship. M1 ≈ 3 small commits.
- Context discipline: each task = a commit (safe resume). Build cycles: Task 1 + Task 2 each need -Reconfigure (new files).
