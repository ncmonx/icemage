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
