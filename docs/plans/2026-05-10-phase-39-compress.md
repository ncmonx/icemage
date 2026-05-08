# Phase 39 — `icmg compress` (semantic prompt compression)

> **For Claude:** REQUIRED SUB-SKILL: Use the appropriate execution skill (`executing-plans` or `subagent-driven-development`) to implement this plan.

**Goal:** Cut token spend 30–60% on dynamic prompt context via semantic compression with reversible glossary, gated by content-type and size threshold.

**Architecture:** Two-stage pipeline. Stage 1 (`compress`): tokenize-estimate → dedup → glossary substitute → optional filler-strip → emit text+glossary. Stage 2 (`expand`): apply glossary in reverse on Claude output. Glossary cached per-project in SQLite (`compression_glossary` table). Default OFF, opt-in via `--compress` flag or auto-trigger when input > 8K tokens.

**Tech Stack:** C++17 (matches existing icmg). Tokenizer reuse Phase 34 WordPiece (close enough for estimation; ±10% acceptable). No new external deps. Optional perplexity strip in later task — heuristic-only for T1.

**Assumptions:**
- Anthropic billing tokenizes plaintext text. Compression is semantic, not byte-level. Will NOT work as gzip/brotli — those produce binary that the model reads as garbage.
- Tokenizer estimates use WordPiece BPE; Anthropic uses different BPE but error within 10%. Will NOT match exactly — use as guide, not contract.
- Glossary substitutions are reversible — losing the glossary cache means losing expansion. Will NOT work if cache DB corrupted; fallback to raw.
- Content-type detection is heuristic (file ext + first-line probe). Will NOT classify novel formats correctly — default to "safe" mode (no aggressive strip).
- Anthropic prompt cache uses content-hash; compressing cached prefix breaks cache. Will NOT save money on stable prefixes — explicitly skip cache region.

---

## Best practices encoded

| # | Rule | Why | Where enforced |
|---|------|-----|----------------|
| 1 | Skip if input < 8K tokens | Overhead > saving | `compress.cpp` early-exit |
| 2 | Never compress source files about to be Edited | Lossy → wrong byte-offset edits | ext blocklist `.cs/.ts/.cpp/.h/.py/.rs/.go/.java` |
| 3 | Never compress Anthropic prompt-cache prefix | Breaks cache → 90% loss flips to 25% premium | Mark cache region with sentinel `<<CACHED>>...<</CACHED>>`, pass-through |
| 4 | Heuristic-first (dedup + glossary + filler) | Sub-100ms; no LM dep | T1 default |
| 5 | Glossary reversible + cached + TTL'd | Idempotent expand, drift-safe | SQLite `compression_glossary` (project+content_hash → mapping) |
| 6 | Auto-expand before user-visible output | Aliases unreadable raw | hook in dispatcher post-receive layer |
| 7 | Telemetry per call (before/after tokens, ms) | Prove ROI, debug regressions | log to `verifications` table |
| 8 | Default OFF; opt-in `--compress`; auto when > 8K | Conservative roll-out | CLI flag + threshold |
| 9 | Lossless first (glossary), lossy opt-in (`--aggressive`) | Trust by default | two modes |
| 10 | Skip on regex-search / exact-match contexts | Lossy breaks `grep -F` semantics | blocklist by command kind |
| 11 | Per-project glossary, not global | Different projects → different jargon | scoped to `<project>/.icmg/data.db` |
| 12 | Glossary entries earn their place | "MemoryStore" 4-tok → "$M" 2-tok only worth it ≥ 10× usage | min-frequency gate before substitute |

---

## Task 1 — `icmg compress` skeleton + dedup + glossary

**Files:**
- Create: `src/cli/commands/compress_cmd.cpp`
- Create: `src/compress/compressor.hpp` + `compressor.cpp`
- Create: `src/compress/glossary_store.hpp` + `glossary_store.cpp`
- Create: `migrations/0042_compression_glossary.sql`
- Test: `tests/compress/test_compressor.cpp`

**Does NOT cover:** binary/gzip compression (impossible — Claude tokenizes text); perplexity-strip (T2); per-section budgets (T3); ANSI color stripping; Unicode normalization beyond NFKC.

**Step 1: Add failing test**
Run: `./build/test_compressor.exe`
Expected: fail — Compressor class missing

**Step 2: Implement minimal compressor**
- `Compressor::compress(input, opts) → {text, glossary, before_tok, after_tok, ms}`
- Heuristics in order:
  1. Whitespace normalize (collapse runs, strip trailing).
  2. Line dedup (consecutive identical lines → `<repeated N×>`).
  3. Path glossary: identify repeated long paths (≥ 3 occurrences, ≥ 20 chars) → assign `@P1..@PN`.
  4. Identifier glossary: tokens ≥ 8 chars and ≥ 5 occurrences → `$I1..$IN`.
  5. Boilerplate filler strip (only with `--aggressive`): "the ", "a ", "really ", "just " (LLMLingua-style).
- Emit glossary as preface block:
  ```
  <glossary>
  @P1=src/cli/commands/some/long/path
  $I1=MemoryConsolidator
  </glossary>
  <body>
  ...compressed text using aliases...
  </body>
  ```

**Step 3: Verify task**
Run: `./build/test_compressor.exe`
Expected: pass — round-trip (compress → expand) bit-identical for lossless mode

**Step 4: Commit**
```bash
git add src/cli/commands/compress_cmd.cpp src/compress/ migrations/0042_compression_glossary.sql tests/compress/
git commit -m "Phase 39 T1: icmg compress skeleton + dedup + reversible glossary"
```

---

## Task 2 — Threshold gating + auto-skip

**Files:**
- Modify: `src/compress/compressor.hpp` (add `shouldCompress(input, ctx)`)
- Modify: `src/cli/commands/compress_cmd.cpp` (CLI flags)
- Test: `tests/compress/test_compressor_gating.cpp`

**Does NOT cover:** machine learning content-type classification — heuristics only.

**Behavior:**
- Skip when est-tokens < 8000 (configurable `--threshold`)
- Skip when content-type ∈ {source-code-editable, regex-pattern, sql-query}
- Skip region marked `<<CACHED>>...<</CACHED>>` (pass-through verbatim)
- Honor `--force` to override

**Step 1–4:** TDD as above. Tests cover each skip path returns identity.

```bash
git commit -m "Phase 39 T2: compress threshold + skip rules"
```

---

## Task 3 — `icmg expand` reverse mapping

**Files:**
- Create: `src/cli/commands/expand_cmd.cpp`
- Modify: `src/compress/compressor.cpp` (add `expand(text, glossary)`)
- Test: `tests/compress/test_expand.cpp`

**Does NOT cover:** partial-glossary expansion (must have full glossary or fail clearly); fuzzy-match expansion when alias mistyped — strict equality only.

**Behavior:**
- Read text + glossary (inline preface or `--glossary <file>`)
- Replace `@P1`/`$I1` with originals
- Strict mode: error if alias missing in glossary
- Lenient mode (`--lenient`): leave unknown aliases as-is

**Step 1–4:** TDD round-trip and missing-glossary error path.

```bash
git commit -m "Phase 39 T3: icmg expand + round-trip guarantee"
```

---

## Task 4 — Pipeline integration: `icmg pack --compress` + auto-trigger

**Files:**
- Modify: `src/cli/commands/pack_cmd.cpp` (add `--compress` flag)
- Modify: `src/cli/commands/context_cmd.cpp` (add `--compress` flag)
- Modify: `src/cli/commands/diff_summary_cmd.cpp` (add `--compress` flag)
- Test: `tests/compress/test_pipeline.cpp`

**Does NOT cover:** automatic compression of MCP tool outputs (deferred — needs MCP protocol audit); compression of stdout passthrough from `icmg run`.

**Behavior:**
- After existing pipeline emits text, if `--compress` set OR auto-threshold hit: route through Compressor.
- Emit telemetry line at end: `[compress] 28412→11203 tok (60% saved) in 84ms`
- Persist to `verifications` table for `pr-summary` recall.

```bash
git commit -m "Phase 39 T4: pack/context/diff-summary --compress integration + telemetry"
```

---

## Task 5 — Tests + docs + bump v0.19.0

**Files:**
- Test: full ctest pass + 3 new test binaries (T1/T2/T3)
- Modify: `README.md` — `compress` section with token-saving table
- Modify: `AGENTS.md` — when to use compress vs raw
- Modify: `COMMANDS.md` — `compress`, `expand`
- Modify: `CMakeLists.txt`, `src/main.cpp`, `src/cli/dispatcher.cpp`, `src/mcp/server.cpp`, `src/cli/commands/update_cmd.cpp` — bump 0.18.1 → 0.19.0
- Update: `PROGRESS.md` — Phase 39 row

**Verification commands:**
```bash
cmake --build build && ctest        # 39+/39+ pass (3 new tests)
./build/icmg.exe compress sample.txt
./build/icmg.exe pack "feature X" --compress
./build/icmg.exe expand compressed.txt
```

**Expected output sample:**
```
$ icmg pack "fix auth bug" --compress
<glossary>
@P1=src/middleware/auth/jwt_validator.cpp
$I1=JwtValidator
$I2=TokenExpiryCheck
</glossary>
<body>
... compressed pack body using aliases ...
</body>
[compress] 24,891→9,847 tok (60% saved) in 73ms
```

```bash
git commit -m "Phase 39 T5: tests + docs + v0.19.0"
git push && gh release create v0.19.0 ...
```

---

## Verification Checklist

- [ ] `icmg compress < input.txt` produces glossary+body
- [ ] `icmg expand < compressed.txt` round-trips bit-identical (lossless mode)
- [ ] `--aggressive` reduces further but only on flag
- [ ] Auto-skip on small inputs (< 8K tok), source-code ext, cached regions
- [ ] `pack --compress` emits telemetry line
- [ ] Verifications table records before/after token counts
- [ ] Glossary survives across sessions (SQLite-backed, project-scoped)
- [ ] Unknown alias in expand → clear error (strict) or pass-through (lenient)
- [ ] All 37 existing tests still pass
- [ ] 3 new tests pass
- [ ] README + AGENTS + COMMANDS updated

---

## Rollback / Risk

| Risk | Mitigation |
|---|---|
| Lossy break code-edit | ext blocklist + default lossless mode |
| Anthropic cache invalidation | sentinel-protected pass-through region |
| Glossary cache corruption | content-hash key; cache miss → fallback to raw |
| Tokenizer estimate drift | log estimated vs Anthropic-reported (when available) |
| Aliases leak to user output | auto-expand pre-display in dispatcher |
| Aggressive strip removes load-bearing word | opt-in only; never default |

---

## Token-Efficiency Impact

| Scenario | Before | After (T1) | After (T1+T4 auto) |
|---|---|---|---|
| `pack` 30K tok | $0.090 in | $0.036 (60% off) | same — cached glossary keeps cost |
| `diff-summary` big PR 50K tok | $0.150 | $0.045 (70% off) | $0.045 |
| Small Q&A 3K tok | $0.009 | $0.009 (skipped) | $0.009 |
| 100-session day mixed | $24/day | ~$11/day | ~$11/day |

Cumulative with existing icmg layers + Anthropic cache: **85–95% reduction vs naive workflow**.

---

## Estimate

- T1 skeleton + glossary: 1.5 days
- T2 gating: 0.5 day
- T3 expand: 0.5 day
- T4 integration: 1 day
- T5 tests + docs + release: 1 day

**Total: 4.5 days**

---

## Out of Scope (defer)

- LM-based perplexity strip (DistilBERT) — needs ONNX (Phase 37 dep)
- Cross-project shared glossary (needs federation design)
- MCP tool output compression (audit MCP protocol first)
- Anthropic prompt-cache integration via `cache_control` headers — needs API client
- Voice/IDE plugin entry points — Phase 38 territory
- Compression for binary/image streams — out of charter
