# WASM Skill Modules (W2 + W3) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers-optimized:executing-plans (inline, batch-local per cadence #30922) to implement task-by-task. Steps use checkbox (`- [ ]`) syntax. **Do NOT tag/ship — local batch only until kak Cahyo says ship.**

**Goal:** Turn icmg into an extensible platform by running sandboxed, user-supplied WASM modules as Tkil filters (text-in/text-out), gated by a verified dynamic-load of the already-bundled `wasmtime.dll`.
**Architecture:** Pure manifest/capability helpers (no I/O, unit-tested) → a dynamic-load binding layer over `wasmtime.dll` (LoadLibrary+GetProcAddress, graceful-degrade) → `runWasmFilter` with compile-once module cache + fuel/epoch/memory limits → a `WasmFilter : BaseFilter` wired into the existing `FilterRegistry`/`icmg run` path → `icmg skill wasm` CLI backed by the zoned profile store.
**Tech Stack:** C++17, nlohmann/json, wasmtime C API (dynamic-loaded, Windows-first), existing `core::Registry`/`ProfileStore`/`Db`, mono test harness (`add_icmg_test`), `pwsh build.ps1`.
**Assumptions:**
- Assumes the bundled `wasmtime.dll` exports the standard C API — VERIFIED 2026-06-07 (objdump). Will NOT work if a future bundle ships a DLL without these symbols → handled by graceful-degrade (feature reports unavailable).
- Assumes Windows-first. Will NOT run on Linux/macOS until W3.5 bundles `libwasmtime.{so,dylib}` (documented non-goal here).
- Assumes module compile is the expensive step and instantiate is cheap (module-cache). Will be FALSE-PROVABLE in Task 9 benchmark — if instantiate is slow, WASM filters are positioned bespoke-only (not hot-path default).

---

## Scope Check

Spec section 11 covers two phases: **W2** (runtime: manifest + caps + dyn-load + runWasmFilter + fixture + benchmark) and **W3** (wire: BaseFilter + registry + CLI + doctor). W2 is self-contained and shippable as a library capability; W3 makes it user-reachable. Kept as ONE plan because W3 is thin and depends directly on W2 types. W4 (opt-in caps) and W5 (auto-update) are explicitly OUT.

## File Structure

| File | Responsibility |
|---|---|
| `src/wasm/wasm_skill.hpp` | `WasmSkill` struct + `parseSkillManifest` + `grantedCaps` (PURE, no I/O) |
| `src/wasm/wasmtime_dyn.hpp` | `WasmtimeApi` fn-pointer struct + `loadWasmtime()` (LoadLibrary/GetProcAddress) |
| `src/wasm/wasm_runtime.hpp` / `.cpp` | `runWasmFilter()` + per-skill compiled-module cache + limits |
| `src/tkil/filters/wasm_filter.cpp` | `WasmFilter : BaseFilter` — adapts a registered skill to the filter ABI |
| `src/cli/commands/skill_cmd.cpp` | extend: `icmg skill wasm add/list/remove/run` subcommands |
| `src/tkil/tkil.cpp` | wire: after built-in `getFilter`, try a matching WASM skill |
| `tests/wasm/test_wasm_skill.cpp` | pure-helper tests (manifest + caps) |
| `tests/wasm/test_wasm_runtime.cpp` | integration tests (fixture, limits, sha, cache) |
| `tests/fixtures/uppercase.wasm` | tiny ABI-conformant fixture module |
| `CMakeLists.txt` | register the two test targets |

Namespace: `icmg::wasm`. Filter lives in `icmg::tkil` (matches existing filters).

---

## W2 — Runtime (gate clear)

### Task 1: `WasmSkill` + `parseSkillManifest` (pure)

**Files:**
- Create: `src/wasm/wasm_skill.hpp`
- Test: `tests/wasm/test_wasm_skill.cpp`

- [ ] **Step 1: Write failing test**

```cpp
// tests/wasm/test_wasm_skill.cpp
#include "../test_main.hpp"
#include "../../src/wasm/wasm_skill.hpp"
using namespace icmg::wasm;

TEST("wasm_skill: parse valid manifest") {
    std::string err;
    auto s = parseSkillManifest(R"({"name":"strip","kind":"tkil-filter","match":"acme",
        "wasm":"skills/strip.wasm","abi":"filter-v1","capabilities":[],"sha256":"ab12"})", err);
    ASSERT_TRUE(s.has_value());
    ASSERT_EQ(s->name, std::string("strip"));
    ASSERT_EQ(s->match, std::string("acme"));
    ASSERT_EQ(s->abi, std::string("filter-v1"));
    ASSERT_EQ(s->sha256, std::string("ab12"));
    ASSERT_TRUE(s->caps.empty());
}

TEST("wasm_skill: missing required field -> nullopt + err") {
    std::string err;
    auto s = parseSkillManifest(R"({"name":"x","kind":"tkil-filter"})", err);
    ASSERT_FALSE(s.has_value());
    ASSERT_TRUE(!err.empty());
}

TEST("wasm_skill: unknown abi rejected") {
    std::string err;
    auto s = parseSkillManifest(R"({"name":"x","kind":"tkil-filter","match":"a",
        "wasm":"x.wasm","abi":"filter-v999","sha256":"00"})", err);
    ASSERT_FALSE(s.has_value());
    ASSERT_CONTAINS(err, "abi");
}

TEST("wasm_skill: bad json -> nullopt") {
    std::string err;
    ASSERT_FALSE(parseSkillManifest("{not json", err).has_value());
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `pwsh -File build.ps1 -Target both` then `C:\icmg-build\build-msvc-full\icmg_test.exe wasm_skill` (from `C:\Temp` cwd)
Expected: FAIL — `wasm_skill.hpp` does not exist (compile error).

- [ ] **Step 3: Implement minimal change**

```cpp
// src/wasm/wasm_skill.hpp
#pragma once
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>
#include <algorithm>

namespace icmg::wasm {

struct WasmSkill {
    std::string name, kind, match, wasmPath, abi, sha256;
    std::vector<std::string> caps;
};

// Only ABIs this build understands. filter-v1 = text-in/text-out.
inline bool knownAbi(const std::string& abi) { return abi == "filter-v1"; }

inline std::optional<WasmSkill> parseSkillManifest(const std::string& json,
                                                   std::string& err) {
    err.clear();
    nlohmann::json j;
    try { j = nlohmann::json::parse(json); }
    catch (const std::exception& e) { err = std::string("json: ") + e.what(); return std::nullopt; }
    auto req = [&](const char* k, std::string& out) -> bool {
        if (!j.contains(k) || !j[k].is_string()) { err = std::string("missing/non-string field: ") + k; return false; }
        out = j[k].get<std::string>(); return true;
    };
    WasmSkill s;
    if (!req("name", s.name) || !req("kind", s.kind) || !req("match", s.match)
        || !req("wasm", s.wasmPath) || !req("abi", s.abi) || !req("sha256", s.sha256))
        return std::nullopt;
    if (!knownAbi(s.abi)) { err = "unknown abi: " + s.abi; return std::nullopt; }
    if (j.contains("capabilities") && j["capabilities"].is_array())
        for (auto& c : j["capabilities"]) if (c.is_string()) s.caps.push_back(c.get<std::string>());
    return s;
}

// Pure: granted = declared INTERSECT allowlist. Unknown/over-declared dropped.
inline std::vector<std::string> grantedCaps(const std::vector<std::string>& declared,
                                            const std::vector<std::string>& allowlist) {
    std::vector<std::string> out;
    for (const auto& d : declared)
        if (std::find(allowlist.begin(), allowlist.end(), d) != allowlist.end())
            out.push_back(d);
    return out;
}

} // namespace icmg::wasm
```

- [ ] **Step 4: Run test to verify it passes**

Run: `C:\icmg-build\build-msvc-full\icmg_test.exe wasm_skill` (cwd `C:\Temp`)
Expected: PASS (4 wasm_skill tests).

- [ ] **Step 5: Commit**

```bash
git add src/wasm/wasm_skill.hpp tests/wasm/test_wasm_skill.cpp CMakeLists.txt
git commit -m "feat(wasm): WasmSkill + parseSkillManifest (pure, filter-v1)"
```

> CMake registration done in Task 2's step (both pure tests share the file). If committing Task 1 alone, add `add_icmg_test(test_wasm_skill tests/wasm/test_wasm_skill.cpp)` to CMakeLists.txt first.

---

### Task 2: `grantedCaps` tests + register test target

**Files:**
- Modify: `tests/wasm/test_wasm_skill.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write failing test** (append to `tests/wasm/test_wasm_skill.cpp`)

```cpp
TEST("wasm_skill: grantedCaps = declared INTERSECT allowlist") {
    ASSERT_EQ(grantedCaps({"read_memory","read_graph"}, {"read_memory"}).size(), (size_t)1);
    ASSERT_EQ(grantedCaps({"read_memory"}, {}).size(), (size_t)0);          // empty allowlist denies all
    ASSERT_EQ(grantedCaps({"evil_cap"}, {"read_memory"}).size(), (size_t)0); // unknown dropped
    ASSERT_EQ(grantedCaps({}, {"read_memory"}).size(), (size_t)0);
}
```

- [ ] **Step 2: Register test in CMake** (anchor: after the service_singleton line added earlier)

```cmake
add_icmg_test(test_wasm_skill    tests/wasm/test_wasm_skill.cpp)
```

- [ ] **Step 3: Run** — `pwsh -File build.ps1 -Target both` → `icmg_test.exe wasm_skill` (cwd `C:\Temp`)
Expected: PASS (5 tests total).

- [ ] **Step 4: Commit**

```bash
git add tests/wasm/test_wasm_skill.cpp CMakeLists.txt
git commit -m "test(wasm): grantedCaps allowlist-intersection cases"
```

---

### Task 3: `wasmtime_dyn.hpp` — dynamic-load binding

**Files:**
- Create: `src/wasm/wasmtime_dyn.hpp`

**Does NOT cover:** non-Windows (no `dlopen` branch here — W3.5). On non-`_WIN32`, `loadWasmtime` returns false (feature unavailable) — that is the documented degrade, NOT an error.

- [ ] **Step 1: Write the binding (no behavior test yet — exercised in Task 5)**

Define typedefs + a struct of function pointers for EXACTLY the verified symbols:
`wasm_engine_new`, `wasm_engine_delete`, `wasm_config_new`, `wasm_byte_vec_new`,
`wasm_byte_vec_delete`, `wasmtime_config_consume_fuel_set`,
`wasmtime_config_epoch_interruption_set`, `wasmtime_store_new`, `wasmtime_store_delete`,
`wasmtime_store_context`, `wasmtime_module_new`, `wasmtime_module_delete`,
`wasmtime_instance_new`, `wasmtime_instance_export_get`, `wasmtime_func_call`,
`wasmtime_memory_data`, `wasmtime_memory_size`.

```cpp
// src/wasm/wasmtime_dyn.hpp
#pragma once
#include <string>
#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

namespace icmg::wasm {

// Opaque wasmtime types — we only pass pointers; never deref fields.
struct WasmtimeApi {
    bool ok = false;
    void* dll = nullptr;
    // Engine/config
    void* (*engine_new_with_config)(void*) = nullptr;   // wasm_engine_new_with_config
    void  (*engine_delete)(void*) = nullptr;
    void* (*config_new)(void*) = nullptr;               // wasm_config_new (no-arg in C API)
    void  (*config_consume_fuel_set)(void*, bool) = nullptr;
    void  (*config_epoch_interruption_set)(void*, bool) = nullptr;
    // Store
    void* (*store_new)(void*, void*, void*) = nullptr;
    void  (*store_delete)(void*) = nullptr;
    void* (*store_context)(void*) = nullptr;
    // Module / instance / call
    void* (*module_new)(void*, const uint8_t*, size_t, void**) = nullptr;
    void  (*module_delete)(void*) = nullptr;
    bool  (*instance_new)(void*, const void*, const void*, size_t, void*, void**) = nullptr;
    bool  (*instance_export_get)(void*, const void*, const char*, size_t, void*) = nullptr;
    bool  (*func_call)(void*, const void*, const void*, size_t, void*, size_t, void**) = nullptr;
    uint8_t* (*memory_data)(void*, const void*) = nullptr;
    size_t   (*memory_size)(void*, const void*) = nullptr;
};

// Resolve every symbol from the bundled wasmtime.dll. Any miss -> ok=false.
inline WasmtimeApi loadWasmtime(std::string& err) {
    WasmtimeApi a;
#ifdef _WIN32
    a.dll = (void*)LoadLibraryA("wasmtime.dll");  // exe-dir first via app manifest/SxS
    if (!a.dll) { err = "wasmtime.dll not found"; return a; }
    auto G = [&](const char* n) -> FARPROC {
        FARPROC p = GetProcAddress((HMODULE)a.dll, n);
        if (!p && err.empty()) err = std::string("missing symbol: ") + n;
        return p;
    };
    a.engine_new_with_config = (decltype(a.engine_new_with_config))G("wasm_engine_new_with_config");
    a.engine_delete          = (decltype(a.engine_delete))G("wasm_engine_delete");
    a.config_new             = (decltype(a.config_new))G("wasm_config_new");
    a.config_consume_fuel_set= (decltype(a.config_consume_fuel_set))G("wasmtime_config_consume_fuel_set");
    a.config_epoch_interruption_set = (decltype(a.config_epoch_interruption_set))G("wasmtime_config_epoch_interruption_set");
    a.store_new       = (decltype(a.store_new))G("wasmtime_store_new");
    a.store_delete    = (decltype(a.store_delete))G("wasmtime_store_delete");
    a.store_context   = (decltype(a.store_context))G("wasmtime_store_context");
    a.module_new      = (decltype(a.module_new))G("wasmtime_module_new");
    a.module_delete   = (decltype(a.module_delete))G("wasmtime_module_delete");
    a.instance_new    = (decltype(a.instance_new))G("wasmtime_instance_new");
    a.instance_export_get = (decltype(a.instance_export_get))G("wasmtime_instance_export_get");
    a.func_call       = (decltype(a.func_call))G("wasmtime_func_call");
    a.memory_data     = (decltype(a.memory_data))G("wasmtime_memory_data");
    a.memory_size     = (decltype(a.memory_size))G("wasmtime_memory_size");
    a.ok = err.empty();
#else
    err = "wasm runtime: non-Windows unsupported (W3.5)";
#endif
    return a;
}

} // namespace icmg::wasm
```

> NOTE for executor: the exact C-API signatures (param order, the real `wasmtime_*` typedefs like `wasmtime_context_t*`, `wasm_byte_vec_t`, `wasmtime_extern_t`, `wasmtime_val_t`) must be taken from the official `wasmtime.h`/`wasm.h`. Fetch the header matching the bundled DLL version (`icmg fetch` the wasmtime C-API docs / vendor the header into `third_party/wasmtime/include/` READ-ONLY for signatures — do NOT link the import-lib). The `void*`/opaque skeleton above defines the binding SHAPE; replace param types with the real structs during implementation. This is the one task that needs header cross-reference.

- [ ] **Step 2: Build to confirm it compiles** — `pwsh -File build.ps1 -Target icmg`
Expected: compiles (header-only, no callers yet).

- [ ] **Step 3: Commit**

```bash
git add src/wasm/wasmtime_dyn.hpp third_party/wasmtime/include/
git commit -m "feat(wasm): wasmtime_dyn dynamic-load binding (verified symbols)"
```

---

### Task 4: Fixture `uppercase.wasm` (ABI filter-v1)

**Files:**
- Create: `tests/fixtures/uppercase.wat`
- Create: `tests/fixtures/uppercase.wasm` (compiled)

**Does NOT cover:** non-ASCII transform (uppercase is ASCII-only — fine for a fixture).

- [ ] **Step 1: Write the WAT** (exports `icmg_alloc`, `icmg_filter`, `memory`)

```wat
;; tests/fixtures/uppercase.wat — filter-v1: uppercases ASCII in place, returns same ptr/len.
(module
  (memory (export "memory") 2)
  (global $bump (mut i32) (i32.const 1024))
  (func (export "icmg_alloc") (param $n i32) (result i32)
    (local $p i32)
    (local.set $p (global.get $bump))
    (global.set $bump (i32.add (global.get $bump) (local.get $n)))
    (local.get $p))
  (func (export "icmg_filter") (param $ptr i32) (param $len i32) (result i64)
    (local $i i32) (local $c i32)
    (block $done (loop $loop
      (br_if $done (i32.ge_u (local.get $i) (local.get $len)))
      (local.set $c (i32.load8_u (i32.add (local.get $ptr) (local.get $i))))
      (if (i32.and (i32.ge_u (local.get $c) (i32.const 97)) (i32.le_u (local.get $c) (i32.const 122)))
        (then (i32.store8 (i32.add (local.get $ptr) (local.get $i)) (i32.sub (local.get $c) (i32.const 32)))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $loop)))
    (i64.or (i64.shl (i64.extend_i32_u (local.get $ptr)) (i64.const 32))
            (i64.extend_i32_u (local.get $len))))) 
```

- [ ] **Step 2: Compile to .wasm**

Run: `wat2wasm tests/fixtures/uppercase.wat -o tests/fixtures/uppercase.wasm`
Expected: `uppercase.wasm` created. (If `wat2wasm` (wabt) absent: `pacman -S mingw-w64-x86_64-wabt`, or hand-assemble once and check the binary in.)

- [ ] **Step 3: Commit**

```bash
git add tests/fixtures/uppercase.wat tests/fixtures/uppercase.wasm
git commit -m "test(wasm): uppercase.wasm filter-v1 fixture"
```

---

### Task 5: `runWasmFilter` + module-cache + limits (integration)

**Files:**
- Create: `src/wasm/wasm_runtime.hpp`, `src/wasm/wasm_runtime.cpp`
- Test: `tests/wasm/test_wasm_runtime.cpp`
- Modify: `CMakeLists.txt`

**Does NOT cover:** capability imports (W4). Modules declaring caps still run with ZERO imports linked here.

- [ ] **Step 1: Write failing test**

```cpp
// tests/wasm/test_wasm_runtime.cpp
#include "../test_main.hpp"
#include "../../src/wasm/wasm_runtime.hpp"
#include <fstream>
using namespace icmg::wasm;

static std::string fixturePath() { return "tests/fixtures/uppercase.wasm"; }

TEST("wasm_runtime: availability is detectable") {
    std::string err;
    bool avail = wasmRuntimeAvailable(err);
    // On Windows CI/dev the bundled DLL is present -> available.
    // If unavailable, the remaining tests SKIP (documented degrade), not fail.
    ASSERT_TRUE(avail || !err.empty());
}

TEST("wasm_runtime: uppercase filter transforms input") {
    std::string err;
    if (!wasmRuntimeAvailable(err)) { return; } // skip when runtime absent
    WasmSkill s; s.name="up"; s.abi="filter-v1"; s.wasmPath=fixturePath();
    WasmLimits lim;
    std::string out, rerr;
    bool ok = runWasmFilter(s, "abc123xyz", lim, out, rerr);
    ASSERT_TRUE(ok);
    ASSERT_EQ(out, std::string("ABC123XYZ"));
}

TEST("wasm_runtime: oversized output capped") {
    std::string err; if (!wasmRuntimeAvailable(err)) return;
    WasmSkill s; s.name="up"; s.abi="filter-v1"; s.wasmPath=fixturePath();
    WasmLimits lim; lim.maxOutBytes = 4;
    std::string out, rerr;
    runWasmFilter(s, "abcdefgh", lim, out, rerr);
    ASSERT_TRUE(out.size() <= 4);
}

TEST("wasm_runtime: missing wasm file -> fail, no crash") {
    std::string err; if (!wasmRuntimeAvailable(err)) return;
    WasmSkill s; s.name="x"; s.abi="filter-v1"; s.wasmPath="tests/fixtures/does-not-exist.wasm";
    std::string out, rerr;
    ASSERT_FALSE(runWasmFilter(s, "x", WasmLimits{}, out, rerr));
}
```

- [ ] **Step 2: Register + run to fail**

Add to CMakeLists: `add_icmg_test(test_wasm_runtime tests/wasm/test_wasm_runtime.cpp)`
Run: `pwsh -File build.ps1 -Target both` → FAIL (`wasm_runtime.hpp` missing).

- [ ] **Step 3: Implement**

Header declares the contract; `.cpp` holds the wasmtime calls + cache.

```cpp
// src/wasm/wasm_runtime.hpp
#pragma once
#include "wasm_skill.hpp"
#include <cstdint>
#include <string>

namespace icmg::wasm {

struct WasmLimits {
    uint64_t memBytes   = 64ull*1024*1024;
    uint64_t fuel       = 50'000'000;
    int      timeoutMs  = 200;
    size_t   maxOutBytes= 4ull*1024*1024;
};

// True if wasmtime.dll loaded + all symbols resolved (cached after first call).
bool wasmRuntimeAvailable(std::string& err);

// Run skill.wasmPath over `input` (filter-v1). Module compiled once + cached by
// wasmPath; instantiated per call in a fresh store. Bounded by limits.
// Returns false (and rerr set) on any failure — never throws/crashes.
bool runWasmFilter(const WasmSkill& skill, const std::string& input,
                   const WasmLimits& lim, std::string& out, std::string& rerr);

} // namespace icmg::wasm
```

`.cpp` outline (executor fills exact C-API per vendored header):
1. `wasmRuntimeAvailable`: lazy `static WasmtimeApi api = loadWasmtime(err);` return `api.ok`.
2. Engine: `config_new()` → `config_consume_fuel_set(cfg,true)` + `config_epoch_interruption_set(cfg,true)` → `engine_new_with_config(cfg)` (cache one engine, `static`).
3. Module cache: `static std::unordered_map<std::string, wasmtime_module_t*>` keyed by `wasmPath`. On miss: read file bytes (fail→return false), `module_new(engine, bytes, len, &mod)`; **verify `skill.sha256`** against the file bytes before compile (sha mismatch → false).
4. Per call: `store_new(engine,…)`; set fuel via context; spawn a timer thread that calls the engine's `increment_epoch` after `timeoutMs` (epoch interruption) → bounded wall-clock; `instance_new(ctx, mod, nullptr, 0, &inst)` (zero imports).
5. Get exports: `instance_export_get(... "memory" ...)`, `"icmg_alloc"`, `"icmg_filter"`.
6. `icmg_alloc(in_len)` → ptr; `memcpy` input into `memory_data()+ptr` (bounds-check vs `memory_size`).
7. `icmg_filter(ptr,len)` → packed i64; unpack `out_ptr=hi32`, `out_len=lo32`; clamp `out_len` to `min(out_len, maxOutBytes, memory_size-out_ptr)`; copy out.
8. `store_delete`; return true. Module stays cached.

- [ ] **Step 4: Build + run**

Run: `pwsh -File build.ps1 -Target both` → `icmg_test.exe wasm_runtime` (cwd = REPO ROOT so `tests/fixtures/...` resolves; if mono-test cwd-sensitive, copy fixture path absolute).
Expected: PASS (transform=ABC123XYZ; cap; missing-file false; availability true on Windows).

- [ ] **Step 5: Commit**

```bash
git add src/wasm/wasm_runtime.hpp src/wasm/wasm_runtime.cpp tests/wasm/test_wasm_runtime.cpp CMakeLists.txt
git commit -m "feat(wasm): runWasmFilter + compile-once module cache + fuel/epoch/mem limits"
```

---

### Task 6: Limit enforcement tests (fuel/epoch/sha)

**Files:**
- Create: `tests/fixtures/spin.wat` / `spin.wasm` (infinite loop in `icmg_filter`)
- Modify: `tests/wasm/test_wasm_runtime.cpp`

- [ ] **Step 1: Failing tests**

```cpp
TEST("wasm_runtime: infinite loop aborts within timeout") {
    std::string err; if (!wasmRuntimeAvailable(err)) return;
    WasmSkill s; s.name="spin"; s.abi="filter-v1"; s.wasmPath="tests/fixtures/spin.wasm";
    WasmLimits lim; lim.timeoutMs = 100; lim.fuel = 1'000'000;
    std::string out, rerr;
    bool ok = runWasmFilter(s, "x", lim, out, rerr);
    ASSERT_FALSE(ok);                 // trapped by fuel/epoch
    ASSERT_TRUE(!rerr.empty());
}

TEST("wasm_runtime: sha256 mismatch refused") {
    std::string err; if (!wasmRuntimeAvailable(err)) return;
    WasmSkill s; s.name="up"; s.abi="filter-v1"; s.wasmPath="tests/fixtures/uppercase.wasm";
    s.sha256 = "deadbeef";            // wrong on purpose
    std::string out, rerr;
    ASSERT_FALSE(runWasmFilter(s, "abc", WasmLimits{}, out, rerr));
    ASSERT_CONTAINS(rerr, "sha");
}
```

`spin.wat`: same exports; `icmg_filter` body = `(loop $l (br $l))`.

- [ ] **Step 2: Build + run** → both PASS (trap + sha refuse). Sha test requires Task 5 step-3 sha check implemented; if `WasmSkill.sha256` empty, skip the check (fixture tests in Task 5 leave it empty).

- [ ] **Step 3: Commit**

```bash
git add tests/fixtures/spin.wat tests/fixtures/spin.wasm tests/wasm/test_wasm_runtime.cpp
git commit -m "test(wasm): fuel/epoch timeout + sha256 mismatch enforcement"
```

---

### Task 7: Instantiate benchmark (the Critical-failure gate)

**Files:**
- Modify: `tests/wasm/test_wasm_runtime.cpp` (a timing assertion, generous bound)

**Does NOT cover:** absolute perf SLA — only proves instantiate is NOT pathologically slow (cache works).

- [ ] **Step 1: Add benchmark test**

```cpp
TEST("wasm_runtime: cached module -> 1000 calls under budget") {
    std::string err; if (!wasmRuntimeAvailable(err)) return;
    WasmSkill s; s.name="up"; s.abi="filter-v1"; s.wasmPath="tests/fixtures/uppercase.wasm";
    std::string out, rerr;
    runWasmFilter(s, "warmup", WasmLimits{}, out, rerr); // compile + cache
    auto t0 = std::chrono::steady_clock::now();
    for (int i=0;i<1000;i++) runWasmFilter(s, "abcabcabc", WasmLimits{}, out, rerr);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now()-t0).count();
    // Generous: 1000 instantiate+call must be < 2s (avg <2ms/call) or WASM is hot-path-unsuitable.
    ASSERT_TRUE(ms < 2000);
}
```

- [ ] **Step 2: Run** → PASS confirms module-cache makes per-call instantiate cheap. **If it FAILS (>2s): STOP — record finding `icmg fail store`, mark WASM-filter bespoke-only in spec, do NOT wire into hot-path (skip Task 9's auto-apply, keep only `icmg skill wasm run`).**

- [ ] **Step 3: Commit**

```bash
git add tests/wasm/test_wasm_runtime.cpp
git commit -m "test(wasm): cached-module per-call latency benchmark (hot-path gate)"
```

---

## W3 — Wire + CLI (gated on Task 7 passing)

### Task 8: `WasmFilter : BaseFilter`

**Files:**
- Create: `src/tkil/filters/wasm_filter.cpp`

**Does NOT cover:** cmd-type auto-routing (Task 9 decides when WasmFilter is selected). This task only adapts a known skill to the filter ABI.

- [ ] **Step 1: Failing test** (in `tests/wasm/test_wasm_runtime.cpp`)

```cpp
#include "../../src/tkil/filters/wasm_filter.hpp"
TEST("wasm_filter: adapts skill to FilterResult") {
    std::string err; if (!icmg::wasm::wasmRuntimeAvailable(err)) return;
    icmg::wasm::WasmSkill s; s.name="up"; s.abi="filter-v1"; s.wasmPath="tests/fixtures/uppercase.wasm";
    icmg::tkil::WasmFilter f(s);
    auto r = f.filter("hello world", "acme-tool");
    ASSERT_EQ(r.output, std::string("HELLO WORLD"));
}
```

- [ ] **Step 2: Implement** `src/tkil/filters/wasm_filter.hpp` + `.cpp`

```cpp
// src/tkil/filters/wasm_filter.hpp
#pragma once
#include "../base_filter.hpp"
#include "../../wasm/wasm_runtime.hpp"
namespace icmg::tkil {
class WasmFilter : public BaseFilter {
public:
    explicit WasmFilter(icmg::wasm::WasmSkill s) : skill_(std::move(s)) {}
    FilterResult filter(const std::string& raw, const std::string& cmd) override {
        FilterResult fr; fr.output = raw;
        std::string out, err;
        if (icmg::wasm::runWasmFilter(skill_, raw, icmg::wasm::WasmLimits{}, out, err)) {
            fr.output = out;
        } else { fr.notes = "wasm-filter fallthrough: " + err; }
        return fr;
    }
    std::string name() const override { return "wasm:" + skill_.name; }
private:
    icmg::wasm::WasmSkill skill_;
};
} // namespace icmg::tkil
```

- [ ] **Step 3: Build + run** → PASS.
- [ ] **Step 4: Commit** — `git commit -m "feat(wasm): WasmFilter adapts skill to BaseFilter (fail-open)"`

---

### Task 9: Wire into `icmg run` (after built-in filter)

**Files:**
- Modify: `src/tkil/tkil.cpp`
- Create: `src/wasm/wasm_registry.hpp` (load registered skills from profile store, match by cmd)

**Does NOT cover:** when Task 7 failed — in that case SKIP the auto-apply wiring; only `icmg skill wasm run` (Task 10) exposes WASM. State this at the top of the task before implementing.

- [ ] **Step 1:** `wasm_registry.hpp` — `std::optional<WasmSkill> matchWasmSkill(const std::string& command)`: query profile store `kind="wasm"` entries, return first whose `match` (regex/substring) fits `command`. Fail-open (returns nullopt on any error).
- [ ] **Step 2:** In `Tkil::runFiltered`, after the built-in `getFilter(type)` result is computed and BEFORE printing, if `matchWasmSkill(command)` returns a skill, run `WasmFilter(*skill).filter(all_out, command)` on the output and prefer its result (fall back to built-in on empty/err). Guard the whole block in `try/catch` — WASM must never break `icmg run`.
- [ ] **Step 3: Test** — integration: register a fixture skill via profile store with `match="acme"`, run a fake `acme` command through Tkil, assert output uppercased. (If profile-store wiring is heavy, defer the live-wire test to a manual smoke and unit-test `matchWasmSkill` matching logic in isolation.)
- [ ] **Step 4: Commit** — `git commit -m "feat(wasm): icmg run applies a matching WASM skill filter (fail-open)"`

---

### Task 10: `icmg skill wasm` CLI + doctor

**Files:**
- Modify: `src/cli/commands/skill_cmd.cpp`
- Modify: `src/cli/commands/doctor_cmd.cpp`

- [ ] **Step 1:** Add `wasm` subcommand dispatch in `skill_cmd.cpp` `run()`:
  - `add <manifest.json>` → `parseSkillManifest` (print error + exit 1 on fail) → compute sha256 of the `.wasm` (warn if manifest sha mismatches actual) → print declared caps for user confirm → store as profile entry `kind="wasm"`, zone from manifest or `tkil`.
  - `list` → print registered wasm skills (name/match/abi/caps).
  - `remove <name>` → delete the profile entry.
  - `run <name>` → read stdin, `runWasmFilter`, write stdout (lets a user test a filter with no real command).
- [ ] **Step 2:** In `doctor_cmd.cpp`, add a line: `wasm runtime: <available|unavailable: reason>` via `wasmRuntimeAvailable(err)`.
- [ ] **Step 3: Test** — `icmg skill wasm add` a fixture manifest, `list` shows it, `run up < input` uppercases, `remove` drops it. Unit-test the manifest→profile-entry mapping; smoke the CLI manually.
- [ ] **Step 4: Commit** — `git commit -m "feat(wasm): icmg skill wasm add/list/remove/run + doctor availability"`

---

## Self-Review

**Spec coverage:** §11.1 gate → Task 3 (binding of verified symbols). §11.2 dynamic-load → Task 3. §11.3 module-cache → Task 5 (cache) + Task 7 (benchmark gate). §11.4 graceful-degrade/doctor → Task 3 (degrade) + Task 10 (doctor). §11.5 phasing W2→W3 → Tasks 1-7 (W2), 8-10 (W3); W3.5/W5 explicitly out. §11.6 failure modes: #1 symbol-miss→Task 3 degrade; #3 perf→Task 7 gate; #4 output cap + fuel/epoch→Task 5/6; #5 caps→out (W4). Spec §4 ABI filter-v1 → fixture Task 4 + runtime Task 5. §3 manifest → Task 1. §7 security (sha/limits) → Task 5/6. **No gaps.**

**Placeholder scan:** Task 3 + Task 5 `.cpp` intentionally describe the wasmtime C-API call sequence in prose because exact struct types require the vendored `wasmtime.h` (flagged as the one header-cross-reference task) — the SHAPE, symbol names, and step order are concrete. All pure-helper tasks (1,2) + tests have real code. No "TBD/handle edge cases" left.

**Type consistency:** `WasmSkill` (name/kind/match/wasmPath/abi/sha256/caps) used identically across Tasks 1,5,8,9,10. `WasmLimits` (memBytes/fuel/timeoutMs/maxOutBytes) consistent Tasks 5-7. `runWasmFilter(skill,input,lim,out,rerr)` signature stable. `wasmRuntimeAvailable(err)` stable. `loadWasmtime(err)→WasmtimeApi{ok}` stable.

---

## Non-goals (carried)
- ✗ W4 capability imports (read_memory/read_graph) — default zero-cap only.
- ✗ W3.5 cross-platform (libwasmtime .so/.dylib).
- ✗ W5 auto-update / signed feed.
- ✗ Bundling a WASM compiler (user supplies `.wasm`; fixtures via wabt).
