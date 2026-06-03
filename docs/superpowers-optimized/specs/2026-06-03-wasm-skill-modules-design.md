# v2.0.0 Breakthrough — WASM Compiled Skill Modules (design)

**Date:** 2026-06-03
**Status:** Design (brainstorm approved on scope) — pending user review → writing-plans.
**Origin:** User idea — "skill can contain code that compiles to a module, registered in skill memory, used as an icmg add-on." Elevated to a v2.0.0 breakthrough alongside the context governor.

---

## 1. Scope & decisions (user-approved)

- **Compile responsibility:** USER supplies a pre-compiled `.wasm`. icmg does NOT bundle a
  toolchain and does NOT compile source. (Keeps the release small; no toolchain dependency.
  A future `icmg skill build` that shells an external `rustc`/`clang` on PATH is a separate,
  optional follow-up — out of this MVP.)
- **MVP extension point:** **Tkil filter** — a WASM skill receives a tool's raw output as
  text and returns filtered text (token-saving). This is icmg's core value and the simplest
  ABI (text-in / text-out).
- **ABI model:** **capability-based hybrid.** Default = text-in/text-out with ZERO
  capabilities (strict sandbox). A skill may DECLARE optional capabilities (e.g.
  `read_memory`, `read_graph`); the host exposes those import functions ONLY when the skill
  declares them AND config/user approves. Full-host (DB/file/network) is never granted by
  default and is out of MVP.
- **Runtime:** `wasmtime`. `wasmtime.dll` IS bundled in the release — BUT (verified 2026-06-03)
  icmg never links/uses it; it is a transitive dependency, with NO `wasmtime.h`/`wasm.h`/import-lib
  in the tree. So W2 has a REAL vendoring step: add the wasmtime C API headers + import-lib (or
  `LoadLibrary`/`GetProcAddress` dynamic-load) and confirm the bundled DLL version matches. The
  DLL being present lowers shipping cost, but it is NOT "free to call" yet. This is the W2 gate.
- **Registry/storage:** reuse the just-shipped zoned profile store — WASM skills are stored
  + looked up by zone (a `kind="wasm"` profile entry pointing at the module + manifest), so
  modules are cross-project, fast-searchable, and partitioned.

### Non-goals (MVP)
- ✗ Bundling a WASM compiler / `icmg skill build` from source.
- ✗ Extractor / custom-command WASM skills (later extension points; ABI larger).
- ✗ Full-host capabilities (file/network/DB write).
- ✗ WASM threads / SIMD / async host calls.

---

## 2. Architecture & data flow

```
icmg run <noisy cmd>
   └─ Detector classifies cmd-type
        └─ FilterRegistry lookup
             ├─ built-in C++ BaseFilter (default, unchanged)
             └─ WasmFilter (NEW): if a registered .wasm skill matches the cmd-type
                   └─ wasmtime: instantiate module in a fresh Store (sandbox)
                        ├─ grant imports per declared+approved capabilities (none by default)
                        ├─ call exported `filter(ptr,len) -> (ptr,len)` over linear memory
                        └─ drop Store (no persistent state between calls)
```

- **No ambient authority:** each call gets a fresh `wasmtime::Store` with only the granted
  imports linked. A skill with zero caps can do pure computation on its input and nothing else.
- **Resource limits:** per-call fuel/epoch timeout + memory cap (e.g. 64 MB, 200 ms) so a
  malformed/hostile module cannot hang or OOM the host. (This is the WASM analogue of the
  hook-timeout fix — bound everything.)

---

## 3. Skill manifest (declares identity + capabilities)

A registered WASM skill = a small manifest (stored as a profile-store entry, `kind="wasm"`)
plus the `.wasm` bytes (path or blob):

```json
{
  "name": "strip-acme-logs",
  "kind": "tkil-filter",
  "match": "acme-tool",            // cmd-type / regex this filter applies to
  "wasm": "skills/strip-acme.wasm", // path (exe-dir relative) or content hash
  "abi": "filter-v1",              // ABI contract version
  "capabilities": [],              // [] = strict sandbox; e.g. ["read_memory"] opt-in
  "sha256": "..."                  // integrity check before load
}
```

Host refuses to load if `sha256` mismatches or `abi` is unknown. Capabilities not in an
allowlist (config) are denied even if declared.

---

## 4. ABI contract `filter-v1` (text-in / text-out)

WASM module exports (minimal, language-agnostic):

```
;; allocator the host calls to place input
(func (export "icmg_alloc") (param i32) (result i32))
;; main entry: input at (in_ptr,in_len) in linear memory; returns packed (out_ptr<<32|out_len)
(func (export "icmg_filter") (param i32 i32) (result i64))
```

Host side (C++):
1. `icmg_alloc(in_len)` → `in_ptr`; copy input bytes into module memory.
2. `icmg_filter(in_ptr, in_len)` → packed `i64`; unpack `out_ptr`/`out_len`; read result.
3. Drop the Store.

Optional capability imports (linked ONLY when declared + approved), e.g.:
```
(import "icmg" "read_memory" (func (param i32 i32) (result i64)))  ;; query -> json (read-only)
(import "icmg" "read_graph"  (func (param i32 i32) (result i64)))
```
These call back into a READ-ONLY view (no writes, no other DBs).

---

## 5. Interfaces (host side, testable)

```cpp
// Pure: parse + validate a skill manifest (no I/O).
struct WasmSkill { std::string name, kind, match, wasmPath, abi, sha256; std::vector<std::string> caps; };
std::optional<WasmSkill> parseSkillManifest(const std::string& json, std::string& err);

// Pure: decide which capabilities are actually granted (declared ∩ allowlist).
std::vector<std::string> grantedCaps(const std::vector<std::string>& declared,
                                     const std::vector<std::string>& allowlist);

// Integration: run a filter module over input (wasmtime), bounded by limits.
struct WasmLimits { uint64_t memBytes = 64*1024*1024; uint64_t fuel = 50'000'000; int timeoutMs = 200; };
bool runWasmFilter(const WasmSkill& s, const std::string& input,
                   const WasmLimits& lim, std::string& output, std::string& err);
```

`parseSkillManifest` + `grantedCaps` are pure → unit-tested with no wasmtime. `runWasmFilter`
gets an integration test against a tiny fixture `.wasm` (a 10-line identity/uppercase filter
checked into `tests/fixtures/`).

---

## 6. Storage / registry

- WASM skill manifests stored via the zoned profile store (`kind="wasm"`), zone = e.g.
  `tkil` / team name. `.wasm` bytes live at `<exe-dir>/skills/*.wasm` (or content-addressed).
- `FilterRegistry` (existing `ICMG_REGISTER_FILTER`) gains a runtime lookup: after built-in
  C++ filters, check registered WASM skills whose `match` fits the cmd-type.
- CLI: `icmg skill wasm add <manifest.json>` / `list` / `remove` / `run <name> < input`
  (the last for testing a filter without a real command).

---

## 7. Security model (the crux)

| Risk | Mitigation |
|------|-----------|
| Hostile module runs arbitrary native code | WASM sandbox — no syscalls; only linked imports reachable. |
| Module hangs the host | epoch/fuel timeout (200 ms) per call. |
| Module exhausts memory | linear-memory cap (64 MB). |
| Tampered module | `sha256` verified before instantiate. |
| Capability escalation | granted = declared ∩ config-allowlist; default allowlist empty (text-only). |
| Supply-chain (team skill) | manifest pins sha256; `icmg skill wasm add` prints caps for user confirm. |

Default posture: a freshly-added WASM filter with no declared caps is a **pure function** —
it sees only the text you pass it and can affect nothing else.

---

## 8. Testing strategy (TDD)

- `parseSkillManifest`: valid / missing-field / unknown-abi / bad-json.
- `grantedCaps`: declared∩allowlist, empty allowlist denies all, unknown cap dropped.
- `runWasmFilter` (integration): fixture `uppercase.wasm` turns "abc"→"ABC"; oversized output
  capped; timeout fixture (infinite loop) aborts within limit; sha mismatch refused.
- FilterRegistry: WASM filter selected for matching cmd-type; falls back to built-in when none.

---

## 9. Phasing

- **W1 (core, no wasmtime):** `parseSkillManifest` + `grantedCaps` pure + tests. Manifest +
  capability model locked.
- **W2 (runtime):** `runWasmFilter` via wasmtime C API (link `wasmtime.dll`), limits + sha
  guard + fixture `.wasm`. Integration tests.
- **W3 (wire):** `WasmFilter : BaseFilter` + FilterRegistry runtime lookup + `icmg run`
  integration. `icmg skill wasm add/list/remove/run`.
- **W4 (opt-in caps):** `read_memory`/`read_graph` import funcs behind allowlist. Separate,
  after W1-W3 prove the sandbox.

MVP shippable = W1+W2+W3 (text-in/out filters, strict sandbox). W4 = the hybrid upgrade.

---

## 10. Honest assessment (carried from discussion)

Powerful but **niche** — audience = power users / teams wanting safe, no-rebuild,
distributable extensions + zero-token deterministic filters. Highest-value MVP use: custom
Tkil filters for bespoke/in-house tools. Worth shipping as a v2.0.0 pillar BECAUSE it turns
icmg from a fixed-feature tool into an extensible platform. The runtime DLL (`wasmtime`) is
already in the bundle, which lowers shipping cost — but calling it needs a W2 vendoring step
(headers/import-lib, verified missing). Sequence AFTER governor P3 so the flagship
token/stability story lands first.
