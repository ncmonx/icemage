# WASM skill modules (filter-v1)

Ship a sandboxed, no-rebuild, distributable **Tkil filter** as a WebAssembly
module. icmg runs it in a wasmtime sandbox (no syscalls, bounded fuel/epoch/memory)
and pipes a command's output through it — a deterministic, zero-LLM, zero-token
filter you write once and reuse across projects.

Requires the bundled `wasmtime` runtime (check: `icmg doctor` → `wasm runtime: available`).

## The `filter-v1` contract

Your module exports exactly three things:

```wat
(memory (export "memory") ...)                          ;; linear memory
(func (export "icmg_alloc") (param i32) (result i32))   ;; reserve N bytes -> ptr
(func (export "icmg_filter")(param i32 i32)(result i64));; (in_ptr,in_len) -> (out_ptr<<32 | out_len)
```

Flow: host calls `icmg_alloc(len)`, writes your input at `ptr`, calls
`icmg_filter(ptr, len)`, unpacks the packed `i64`, and reads `out_len` bytes at
`out_ptr` as the filtered output. Pure computation only — **zero host imports**
(a strict sandbox; the module sees only the text you pass it).

## Authoring

Write `.wat` (text, as in `uppercase.wat`) or compile any language to a
freestanding `.wasm` (Rust `--target wasm32-unknown-unknown`, C/clang
`--target=wasm32`, TinyGo, …) exporting the three symbols above. icmg accepts a
`.wat` path directly (compiled in-process via the bundled runtime) or a `.wasm`.

## Manifest

```json
{
  "name": "uppercase",
  "kind": "tkil-filter",
  "match": "acme-tool",                       // substring of the command this filter applies to
  "wasm": "examples/wasm-skills/uppercase.wat",
  "abi": "filter-v1",
  "capabilities": [],                          // [] = strict sandbox (the only mode today)
  "sha256": ""                                 // optional: pin the file's sha256 (refused if mismatched)
}
```

## Use

```bash
icmg skill wasm add examples/wasm-skills/uppercase.skill.json
icmg skill wasm list
echo "hello acme" | icmg skill wasm run uppercase     # -> HELLO ACME
# Now any `icmg run` whose command contains "acme-tool" auto-applies this filter.
icmg skill wasm remove uppercase
```

## Safety

- Sandbox: no syscalls; only the three exports are reachable.
- Bounded per call: fuel + ~200 ms epoch timeout + memory cap → a hostile/runaway
  module cannot hang or OOM the host.
- Integrity: a non-empty `sha256` is verified before the module is compiled.
- Fail-open: any error leaves the original output untouched — a broken skill never
  breaks `icmg run`.

Capability imports (`read_memory` / `read_graph`) are **not** available yet
(default `[]`); they are a future opt-in behind an allowlist.

### Host-caps (v2.12+): `icmg.log`

Both ABIs may now optionally IMPORT a read-only host function:

```wat
(import "icmg" "log" (func $log (param i32 i32)))   ;; icmg.log(ptr, len)
```

`icmg.log(ptr,len)` sends `len` bytes of the module's memory at `ptr` to the
host (observability / debugging). It is defined via a wasmtime **linker**, so:

- Modules that DON'T import it instantiate exactly as before (fully
  backward-compatible — no module change needed).
- On an older `wasmtime.dll` lacking the linker symbols, host-caps silently
  disables and import-less modules still run (graceful degrade). Check support:
  the runtime exposes `wasmHostCapsAvailable()` internally; a module that
  imports `icmg.log` simply won't instantiate if unsupported.

This is the seam for richer read-only host calls later (e.g. `icmg.recall`,
`icmg.graph_symbol`) — the sandbox stays strict; only explicitly-defined,
read-only functions are reachable.

---

## `extractor-v1` — WASM language extractors (add a language, no rebuild)

Same sandbox, different job: instead of filtering command output, an
**extractor** parses a *source file* and returns the symbols/imports icmg feeds
into its code graph. Drop a `.wasm` to teach icmg a new language — no binary
rebuild, no PR.

### Contract

```wat
(memory (export "memory") ...)                          ;; linear memory
(func (export "icmg_alloc")  (param i32) (result i32))  ;; reserve N bytes -> ptr
(func (export "icmg_extract")(param i32 i32)(result i64));; (src_ptr,src_len) -> (out_ptr<<32 | out_len)
```

Flow is identical to `filter-v1` (alloc → write source → call → unpack i64),
but the output at `out_ptr` is a UTF-8 **JSON** object mapping 1:1 to icmg's
`ExtractResult` (every field optional):

```json
{
  "context":    "first doc comment / description",
  "imports":    ["std", "fmt"],
  "namespaces": ["main"],
  "classes":    ["Foo"],
  "functions":  ["doThing", "init"],
  "tables":     []
}
```

Non-string array entries are dropped defensively; malformed JSON fails open to
an empty result (a broken extractor degrades to "no symbols", never a crash).

### Manifest

```json
{
  "name": "demo-extractor",
  "kind": "extractor",                          // <- selects extractor-v1
  "abi": "extractor-v1",
  "language": "demolang",                        // graph language key
  "extensions": [".demo"],                       // file extensions handled
  "wasm": "examples/wasm-skills/demo-extractor.wat",
  "sha256": "",                                  // optional integrity pin
  "priority": 0,                                 // >0 overrides a built-in extractor; 0 fills a gap
  "min_icmg": "2.12.0"                           // semver floor (refused on older icmg)
}
```

`priority` arbitration: `0` only fills a language with **no** built-in extractor
(never shadows one); a value `> built-in` (built-ins are priority 0) overrides it.

### Use

```bash
icmg skill wasm add examples/wasm-skills/demo-extractor.skill.json
icmg skill wasm list                                  # shows [extractor] rows too
icmg skill wasm test demo-extractor --file some.demo  # runs it, prints imports/functions/...
icmg skill wasm remove demo-extractor
```

> `demo-extractor.wat` here emits a **fixed** result to demonstrate the ABI.
> A real extractor tokenizes the input — write that in Rust / AssemblyScript /
> Zig and compile to `.wasm` (hand-written WAT tokenizers are impractical).
