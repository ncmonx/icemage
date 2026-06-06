# Local-LLM No-Premium Routing — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers-optimized:subagent-driven-development (recommended) or superpowers-optimized:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

> **EXECUTION OUTCOME (2026-06-06, SHIPPED LOCAL):** Tasks 1,2,3,5 done + committed
> (df128472da, feda487025, e07002877e, 3d3c1aabd2). Gate 1553 pass / 7 hookio-fail
> (pre-existing console-artifact, non-regression). **Task 4 SKIPPED** — no real router
> integration points (cron = hygiene prune no-LLM; distill = heuristic regex no-LLM; atomize
> = `atom_llm` bypasses `smart_router`); user approved opt A. **Task 5 real consumer =
> `compact_bg_cmd.cpp`** (not `runners.cpp` as drafted — its smart_router include is
> vestigial). Task 2 also fixed `bundle --rerank` (explicit_local) to avoid regression.

**Goal:** Make the local LLM fire only when no premium LLM (Claude) is available in this execution (or when explicitly requested), and give `icmg agent` a native local advisory backend.

**Architecture:** A new `core/exec_context.hpp` exposes `premiumAvailable()` (default true = Claude present = local OFF), settable via env `ICMG_RUN_MODE=headless` or programmatically. `smart_router::CallContext` gains `premium_available` + `explicit_local`; `routeFor` adds one gate: `premium && !explicit → REGEX`. Four no-premium paths set the signal; `icmg agent` gains an in-process `WarmPool` advisory backend (no external CLI), advisory-only with `--exec` refused on local and prompt-overflow truncation.

**Tech Stack:** C++17, CMake (GLOB src + `add_icmg_test` mono-test macro), llama.cpp backend (`ICMG_USE_LLAMA`), test harness `tests/test_main.hpp` (`TEST` / `ASSERT_*`).

**Assumptions:**
- Assumes `ICMG_USE_LLAMA=ON` build for local paths — will NOT exercise local routes if built without it (routeFor already returns REGEX via `build_has_llama=false`; tests gate on `LlamaRunner::available()`).
- Assumes cron/daemon spawn icmg as a child process and can set env — will NOT mark headless if a path invokes routing in-process without calling `setPremiumAvailable(false)`.
- Assumes `WarmPool::acquire()` / `LlamaRunner::infer()` are the in-process inference entry (confirmed in `bundle_cmd.cpp` rerank).
- Assumes new test files are registered via `add_icmg_test(...)` in `CMakeLists.txt` — **this edit needs explicit user OK at execution (ATURAN MUTLAK rule #2)**; it is the established pattern (line ~629+).

---

## File Structure

- Create `src/core/exec_context.hpp` + `src/core/exec_context.cpp` — run-mode + premium-availability signal. Single responsibility: "is a premium LLM present in this execution?"
- Modify `src/llm/smart_router.hpp` — add 2 `CallContext` fields.
- Modify `src/llm/smart_router.cpp` — add 1 gate rule in `routeFor`.
- Modify `src/cli/commands/agent_cmd.cpp` — native-local advisory backend.
- Modify `src/cli/commands/atomize_cmd.cpp` + `src/cli/commands/distill_cmd.cpp` — build `CallContext` from `premiumAvailable()`.
- Modify `src/cli/commands/cron_cmd.cpp` (+ `cronjobs_cmd.cpp`) — set `ICMG_RUN_MODE=headless` on spawned children.
- Modify `src/core/hooks/runners.cpp` (PreCompact path) — premium = `!isHeadless()`.
- Create tests: `tests/core/test_exec_context.cpp`, `tests/llm/test_smart_router.cpp`, `tests/cli/test_agent_local.cpp`.
- Modify `CMakeLists.txt` — 3 `add_icmg_test(...)` lines (permission-gated).

---

### Task 1: exec_context signal

**Files:**
- Create: `src/core/exec_context.hpp`, `src/core/exec_context.cpp`
- Test: `tests/core/test_exec_context.cpp`
- Modify: `CMakeLists.txt` (register test — permission-gated)

**Does NOT cover:** does not auto-detect Claude via env-sniff (CLAUDECODE etc.) — explicit signal only. A process that neither sets the env nor calls the setter is treated as premium-present (local OFF).

- [ ] **Step 1: Write failing test**

```cpp
// tests/core/test_exec_context.cpp
#include "../test_main.hpp"
#include "../../src/core/exec_context.hpp"
#include <cstdlib>

using namespace icmg::core;

TEST("exec_context: default is interactive + premium present") {
    setRunMode(RunMode::INTERACTIVE);   // reset any prior state
    setPremiumAvailable(true);
    ASSERT_TRUE(premiumAvailable());
    ASSERT_FALSE(isHeadless());
}

TEST("exec_context: setPremiumAvailable(false) reflected") {
    setPremiumAvailable(false);
    ASSERT_FALSE(premiumAvailable());
    setPremiumAvailable(true); // restore
}

TEST("exec_context: setRunMode HEADLESS reported by isHeadless") {
    setRunMode(RunMode::HEADLESS);
    ASSERT_TRUE(isHeadless());
    setRunMode(RunMode::INTERACTIVE);
}

TEST("exec_context: parseRunMode maps env strings") {
    ASSERT_TRUE(parseRunMode("headless") == RunMode::HEADLESS);
    ASSERT_TRUE(parseRunMode("interactive") == RunMode::INTERACTIVE);
    ASSERT_TRUE(parseRunMode("garbage") == RunMode::INTERACTIVE); // safe default
    ASSERT_TRUE(parseRunMode("") == RunMode::INTERACTIVE);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target icmg_test && ./build/icmg_test "exec_context"`
Expected: FAIL to compile — `exec_context.hpp` not found / symbols undefined.

- [ ] **Step 3: Implement minimal change**

```cpp
// src/core/exec_context.hpp
#pragma once
#include <string>
namespace icmg::core {

enum class RunMode { INTERACTIVE, HEADLESS };

// Pure helper: map an env string to a RunMode (unknown/empty -> INTERACTIVE).
RunMode parseRunMode(const std::string& s);

// Lazy: first call reads env ICMG_RUN_MODE; subsequent calls return cached/overridden.
RunMode currentRunMode();
void    setRunMode(RunMode m);
bool    isHeadless();

// Premium (Claude/external) availability. DEFAULT TRUE = assume present = local OFF.
// First read derives from currentRunMode() (HEADLESS => false) unless overridden.
bool premiumAvailable();
void setPremiumAvailable(bool v);

} // namespace icmg::core
```

```cpp
// src/core/exec_context.cpp
#include "exec_context.hpp"
#include <cstdlib>
#include <optional>
namespace icmg::core {

RunMode parseRunMode(const std::string& s) {
    return s == "headless" ? RunMode::HEADLESS : RunMode::INTERACTIVE;
}

static std::optional<RunMode> g_mode;
static std::optional<bool>    g_premium;

RunMode currentRunMode() {
    if (!g_mode) {
        const char* e = std::getenv("ICMG_RUN_MODE");
        g_mode = parseRunMode(e ? e : "");
    }
    return *g_mode;
}

void setRunMode(RunMode m) { g_mode = m; g_premium.reset(); /* re-derive */ }
bool isHeadless() { return currentRunMode() == RunMode::HEADLESS; }

bool premiumAvailable() {
    if (!g_premium) g_premium = (currentRunMode() != RunMode::HEADLESS);
    return *g_premium;
}
void setPremiumAvailable(bool v) { g_premium = v; }

} // namespace icmg::core
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target icmg_test && ./build/icmg_test "exec_context"`
Expected: PASS (4 tests).

CMakeLists registration line (add near other `tests/core/` entries, ~line 635):

```cmake
add_icmg_test(test_exec_context tests/core/test_exec_context.cpp)
```

- [ ] **Step 5: Commit**

```bash
git add src/core/exec_context.hpp src/core/exec_context.cpp tests/core/test_exec_context.cpp CMakeLists.txt
git commit -m "feat(llm): exec_context premium-availability signal (no-premium routing core)"
```

---

### Task 2: smart_router premium gate

**Files:**
- Modify: `src/llm/smart_router.hpp`, `src/llm/smart_router.cpp`
- Test: `tests/llm/test_smart_router.cpp`
- Modify: `CMakeLists.txt` (register test — permission-gated)

**Does NOT cover:** the gate only blocks LLM when premium is present AND not explicit. It does NOT change hot→regex, cache, build-off, user-disabled, cooldown, small-input, or RAM rules — those still gate independently and can still force REGEX even when `!premium_available`.

- [ ] **Step 1: Write failing test**

```cpp
// tests/llm/test_smart_router.cpp
#include "../test_main.hpp"
#include "../../src/llm/smart_router.hpp"

using namespace icmg::llm;

static CallContext baseCtx() {
    CallContext c;
    c.tier            = PathTier::WARM;
    c.kind            = "agent";
    c.input_tokens_est= 2000;      // above small-input threshold
    c.result_cached   = false;
    c.llm_loaded      = true;      // warm model present
    c.user_disabled   = false;
    c.build_has_llama = true;
    return c;
}

TEST("router: premium present + not explicit -> REGEX (new gate)") {
    CallContext c = baseCtx();
    c.premium_available = true;
    c.explicit_local    = false;
    ASSERT_TRUE(routeFor(c).route == Route::REGEX);
}

TEST("router: no premium -> LLM_LOCAL") {
    CallContext c = baseCtx();
    c.premium_available = false;
    ASSERT_TRUE(routeFor(c).route == Route::LLM_LOCAL);
}

TEST("router: premium present + explicit_local -> LLM_LOCAL") {
    CallContext c = baseCtx();
    c.premium_available = true;
    c.explicit_local    = true;
    ASSERT_TRUE(routeFor(c).route == Route::LLM_LOCAL);
}

TEST("router: hot path still REGEX even when no premium") {
    CallContext c = baseCtx();
    c.premium_available = false;
    c.tier = PathTier::HOT;
    ASSERT_TRUE(routeFor(c).route == Route::REGEX);
}

TEST("router: build lacks llama -> REGEX regardless of premium") {
    CallContext c = baseCtx();
    c.premium_available = false;
    c.build_has_llama   = false;
    ASSERT_TRUE(routeFor(c).route == Route::REGEX);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target icmg_test && ./build/icmg_test "router:"`
Expected: FAIL to compile — `CallContext` has no member `premium_available` / `explicit_local`.

- [ ] **Step 3: Implement minimal change**

In `src/llm/smart_router.hpp`, add to `struct CallContext` (after `build_has_llama`):

```cpp
    bool        premium_available = true;   // Claude/premium present in this execution
    bool        explicit_local    = false;  // user explicitly chose local (ask --backend=local, chat)
```

In `src/llm/smart_router.cpp`, inside `routeFor`, immediately AFTER the `user_disabled`
hard-rule (line ~46, before the session-disable block) add:

```cpp
    // No-premium gate: reserve local LLM for executions without a premium LLM,
    // or when the caller explicitly requested local. Claude wins when present.
    if (ctx.premium_available && !ctx.explicit_local)
        return { Route::REGEX, "premium (Claude) present — local reserved for no-premium/explicit" };
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target icmg_test && ./build/icmg_test "router:"`
Expected: PASS (5 tests). Then full gate: `ctest --test-dir build -R icmg_test --output-on-failure` (no regression).

CMakeLists registration line (add near other `tests/` entries):

```cmake
add_icmg_test(test_smart_router tests/llm/test_smart_router.cpp)
```

- [ ] **Step 5: Commit**

```bash
git add src/llm/smart_router.hpp src/llm/smart_router.cpp tests/llm/test_smart_router.cpp CMakeLists.txt
git commit -m "feat(llm): routeFor no-premium gate (premium+!explicit -> regex)"
```

---

### Task 3: `icmg agent` native-local advisory backend

**Files:**
- Modify: `src/cli/commands/agent_cmd.cpp`
- Test: `tests/cli/test_agent_local.cpp`
- Modify: `CMakeLists.txt` (register test — permission-gated)

**Does NOT cover:** local backend is advisory-only. `--exec` on the local route is REFUSED (returns error, no inference) — `--exec` requires a premium agentic CLI. Does not implement multi-turn/tool-calling for the local model. Quality of local output is out of scope (gated to no-premium/explicit by Task 2).

This task adds a small pure helper (testable without a live model) plus the wiring. The
live-inference branch mirrors `bundle_cmd.cpp` rerank (`WarmPool::acquire` + `infer`).

- [ ] **Step 1: Write failing test**

```cpp
// tests/cli/test_agent_local.cpp
#include "../test_main.hpp"
#include "../../src/cli/agent_local.hpp"

using namespace icmg::cli;

TEST("agent_local: refuse local + exec (advisory-only)") {
    // explicit_local OR no-premium would route local; --exec must be refused there.
    auto d = agentLocalDecision(/*premium_available=*/false, /*explicit_local=*/false,
                                /*exec=*/true);
    ASSERT_FALSE(d.use_local);
    ASSERT_TRUE(d.refuse_exec);
    ASSERT_CONTAINS(d.reason, "exec");
}

TEST("agent_local: local advisory when no premium, no exec") {
    auto d = agentLocalDecision(false, false, false);
    ASSERT_TRUE(d.use_local);
    ASSERT_FALSE(d.refuse_exec);
}

TEST("agent_local: premium present + no explicit -> external CLI (no local)") {
    auto d = agentLocalDecision(true, false, false);
    ASSERT_FALSE(d.use_local);
    ASSERT_FALSE(d.refuse_exec);
}

TEST("agent_local: explicit_local advisory even with premium") {
    auto d = agentLocalDecision(true, true, false);
    ASSERT_TRUE(d.use_local);
}

TEST("agent_local: truncatePrompt clamps to window budget") {
    std::string big(40000, 'x');           // ~10k tokens > 8k window
    bool warned = false;
    std::string out = truncatePromptToWindow(big, /*max_tokens=*/8000, warned);
    ASSERT_TRUE(warned);
    ASSERT_TRUE(out.size() < big.size());
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target icmg_test && ./build/icmg_test "agent_local"`
Expected: FAIL to compile — `agent_local.hpp` not found.

- [ ] **Step 3: Implement minimal change**

```cpp
// src/cli/agent_local.hpp  (pure decision helpers — unit-testable, no model)
#pragma once
#include <string>
namespace icmg::cli {

struct AgentLocalDecision {
    bool use_local   = false;  // route to in-process WarmPool advisory
    bool refuse_exec = false;  // --exec attempted on a local route -> refuse
    std::string reason;
};

// Local fires when (!premium || explicit_local). If that holds AND exec requested,
// it must be refused (advisory-only): a weak local model never auto-edits files.
inline AgentLocalDecision agentLocalDecision(bool premium_available,
                                             bool explicit_local,
                                             bool exec) {
    AgentLocalDecision d;
    bool local_route = (!premium_available || explicit_local);
    if (local_route && exec) {
        d.use_local = false;
        d.refuse_exec = true;
        d.reason = "local backend is advisory-only; --exec requires a premium agentic CLI";
        return d;
    }
    d.use_local = local_route;
    d.reason = local_route ? "no premium / explicit -> local advisory"
                           : "premium present -> external CLI";
    return d;
}

// Rough char-budget = max_tokens * 4. Truncate head-preserving + set warned.
inline std::string truncatePromptToWindow(const std::string& prompt,
                                          int max_tokens, bool& warned) {
    const size_t budget = static_cast<size_t>(max_tokens) * 4;
    if (prompt.size() <= budget) { warned = false; return prompt; }
    warned = true;
    return prompt.substr(0, budget);
}

} // namespace icmg::cli
```

Then wire into `agent_cmd.cpp` `run()` (after prompt assembly, before the external-CLI
spawn). Pseudocode-anchored to existing symbols:

```cpp
#include "../agent_local.hpp"
#include "../../core/exec_context.hpp"
#include "../../llm/smart_router.hpp"
#include "../../llm/warm_pool.hpp"
#include "../../llm/llama_runner.hpp"

// ... after `prompt` is built and `bool exec = hasFlag(args, "--exec");` ...
bool explicit_local = hasFlag(args, "--local");
bool premium = core::premiumAvailable();
auto ld = agentLocalDecision(premium, explicit_local, exec);
if (ld.refuse_exec) {
    std::cerr << "icmg agent: " << ld.reason << "\n";
    return 1;
}
if (ld.use_local && llm::LlamaRunner::available()) {
    llm::CallContext rc;
    rc.tier = llm::PathTier::WARM;
    rc.kind = "agent";
    rc.input_tokens_est = prompt.size() / 4;
    rc.build_has_llama  = true;
    rc.llm_loaded       = llm::WarmPool::instance().isLoaded();
    rc.premium_available = premium;
    rc.explicit_local    = explicit_local;
    if (llm::routeFor(rc).route == llm::Route::LLM_LOCAL) {
        bool warned = false;
        std::string p = truncatePromptToWindow(prompt, 8000, warned);
        if (warned) std::cerr << "icmg agent: local context truncated to ~8000 tokens\n";
        std::string err;
        if (auto* run = llm::WarmPool::instance().acquire(err)) {
            llm::InferParams ip; ip.max_tokens = 1024; ip.temperature = 0.3f;
            auto res = run->infer(p, ip);
            if (res.ok) { std::cout << res.text << "\n"; /* store decision as today */ return 0; }
        }
        std::cerr << "icmg agent: local inference unavailable (" << err << "), falling back\n";
        // fall through to external CLI
    }
}
// ... existing external-CLI spawn path unchanged ...
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target icmg_test && ./build/icmg_test "agent_local"`
Expected: PASS (5 tests).

CMakeLists registration line:

```cmake
add_icmg_test(test_agent_local tests/cli/test_agent_local.cpp)
```

- [ ] **Step 5: Commit**

```bash
git add src/cli/agent_local.hpp src/cli/commands/agent_cmd.cpp tests/cli/test_agent_local.cpp CMakeLists.txt
git commit -m "feat(agent): native-local advisory backend (no-premium); --exec refused on local; overflow truncate"
```

---

### Task 4: Wire cron + atomize/distill to no-premium

**Files:**
- Modify: `src/cli/commands/cron_cmd.cpp` (and `cronjobs_cmd.cpp` if it spawns children)
- Modify: `src/cli/commands/atomize_cmd.cpp`, `src/cli/commands/distill_cmd.cpp`
- Test: extend `tests/core/test_exec_context.cpp`

**Does NOT cover:** does not persist `session-disable` across cron runs (each run is a fresh process; cold-load failures re-attempt next run — accepted minor limitation). Does not change cron scheduling logic.

- [ ] **Step 1: Write failing test**

Add to `tests/core/test_exec_context.cpp`:

```cpp
TEST("exec_context: env ICMG_RUN_MODE=headless => premium absent (fresh derive)") {
    // simulate child spawned by cron: env set, state un-cached
    setRunMode(RunMode::HEADLESS);   // mirrors parseRunMode("headless")
    ASSERT_FALSE(premiumAvailable());
    setRunMode(RunMode::INTERACTIVE); // restore
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `./build/icmg_test "ICMG_RUN_MODE=headless"`
Expected: FAIL (PASS only after Task 1 setRunMode resets g_premium — if Task 1 done, this verifies the derive; if the reset was omitted it fails). Confirms `setRunMode` re-derives premium.

- [ ] **Step 3: Implement minimal change**

In the cron child-spawn (where `cron_cmd.cpp` builds the child `icmg` command / env), set
the env on the child environment block:

```cpp
// cron spawns headless icmg children -> mark no-premium so local LLM may serve.
// (Windows: add to the lpEnvironment block; POSIX: setenv before exec / pass in envp.)
// Minimal portable approach: prepend to the command the runner already builds:
//   set ICMG_RUN_MODE=headless before invoking, or inject into child env map.
```

In `atomize_cmd.cpp` / `distill_cmd.cpp`, where the local LLM is invoked, build the
`CallContext` premium from the signal:

```cpp
#include "../../core/exec_context.hpp"
// ... when constructing CallContext for the atomize/distill LLM step:
ctx.premium_available = icmg::core::premiumAvailable();
ctx.explicit_local    = false;
```

- [ ] **Step 4: Run test to verify it passes**

Run: `./build/icmg_test "exec_context" && ctest --test-dir build -R icmg_test --output-on-failure`
Expected: PASS, no regression. Manual smoke: `set ICMG_RUN_MODE=headless && icmg atomize ... ` exercises local route (warm model) — `icmg llm status` telemetry count increments.

- [ ] **Step 5: Commit**

```bash
git add src/cli/commands/cron_cmd.cpp src/cli/commands/atomize_cmd.cpp src/cli/commands/distill_cmd.cpp tests/core/test_exec_context.cpp
git commit -m "feat(cron): mark cron/atomize/distill no-premium so local LLM serves headless"
```

---

### Task 5: PreCompact COLD premium signal

**Files:**
- Modify: `src/core/hooks/runners.cpp` (PreCompact summarize path; see `precompact_output.hpp`)
- Test: covered by Task 2 router tests + a targeted assertion below

**Does NOT cover:** PreCompact in an interactive Claude session keeps `premium_available=true` → REGEX (Claude present). Local summarize fires only when the compact runs headless (daemon-driven). This path will rarely activate in normal interactive use — intentional.

- [ ] **Step 1: Write failing test**

Add to `tests/llm/test_smart_router.cpp`:

```cpp
TEST("router: precompact COLD no-premium -> LLM_LOCAL; with premium -> REGEX") {
    CallContext c;
    c.tier = PathTier::COLD; c.kind = "compact"; c.input_tokens_est = 4000;
    c.build_has_llama = true; c.llm_loaded = true;
    c.premium_available = false; c.explicit_local = false;
    ASSERT_TRUE(routeFor(c).route == Route::LLM_LOCAL);
    c.premium_available = true;
    ASSERT_TRUE(routeFor(c).route == Route::REGEX);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `./build/icmg_test "precompact COLD"`
Expected: FAIL until the PreCompact path passes the signal (test of routeFor passes after Task 2; this asserts COLD behavior explicitly).

- [ ] **Step 3: Implement minimal change**

In the PreCompact summarize CallContext construction in `runners.cpp`:

```cpp
#include "../exec_context.hpp"
// ...
ctx.tier = llm::PathTier::COLD;
ctx.premium_available = !icmg::core::isHeadless();  // interactive Claude => premium present
ctx.explicit_local    = false;
```

- [ ] **Step 4: Run test to verify it passes**

Run: `./build/icmg_test "precompact COLD" && ctest --test-dir build -R icmg_test --output-on-failure`
Expected: PASS, no regression.

- [ ] **Step 5: Commit**

```bash
git add src/core/hooks/runners.cpp tests/llm/test_smart_router.cpp
git commit -m "feat(precompact): COLD summarize uses local LLM only when headless (no-premium)"
```

---

## Final Verification (after all tasks)

- [ ] Full gate: `ctest --test-dir build -R icmg_test --output-on-failure` — all pass.
- [ ] MSVC ship build: `pwsh -File build.ps1 -Target both -RunTests` (per CLAUDE.md MUTLAK).
- [ ] CI-lint intact: `tools/lint_no_llm_in_hot.sh` passes (no `smart_router.hpp` include leaked into hot-path TUs — exec_context.hpp is not the router).
- [ ] Smoke: `set ICMG_RUN_MODE=headless; icmg agent "summarize X"` → local advisory output; `icmg agent --local --exec "..."` → refused; `icmg llm status` telemetry count > 0.
- [ ] Post-change sync: `icmg graph update` + `icmg store --topic decisions-llm "..."` + `icmg wflog add "..."` + `icmg verify --command "ctest ... -R icmg_test"`.

## Self-Review

- **Spec coverage:** exec_context (Task 1) ✓ · CallContext+gate (Task 2) ✓ · agent native-local advisory + --exec refuse + overflow (Task 3) ✓ · cron atomize/summarize wiring (Task 4) ✓ · PreCompact COLD (Task 5) ✓ · safety preserved (gate inserted after hard-rules, CI-lint note in Final Verification) ✓.
- **Type consistency:** `CallContext.premium_available`/`explicit_local`, `Route::LLM_LOCAL`/`REGEX`, `PathTier::WARM`/`COLD`/`HOT`, `AgentLocalDecision{use_local,refuse_exec,reason}`, `premiumAvailable()`/`isHeadless()`/`setRunMode()`/`parseRunMode()` — used identically across tasks.
- **Placeholder scan:** all test + impl steps contain real code; cron child-env step is descriptive because the exact spawn mechanism is platform-specific (Win lpEnvironment vs POSIX envp) — flagged, not a logic gap.
