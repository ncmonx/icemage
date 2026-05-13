warning: unable to find all commit-graph files
# Phase 82 — Multi-Language Coverage

**Goal:** Extend icmg graph/symbol to support all major programming languages — not just C#, SQL, C++, Python already covered in Phase 18 + 32.

**Prerequisite:** Phase 18 (symbol schema + regex extractors) and Phase 32 (tree-sitter core + 4 grammars) must be shipped first.

**Why:** Users work in Go, Rust, TypeScript, Java, Kotlin, Ruby, Bash, Lua, etc. Right now `icmg graph update` silently skips those files. Symbol extraction = 0 for non-C# repos. The BM25 search still works (text), but call-graph edges, `icmg graph callers`, and symbol-level context slicing all fail.

**Approach:** tree-sitter grammar expansion (build on Phase 32 infrastructure). Each grammar is a vendored generated `.c` file — no runtime toolchain required. Languages that don't yet have a tree-sitter grammar get a lightweight regex extractor as interim fallback.

**Estimate:** 4–6 days.

---

## Language Tiers

| Tier | Grammar method | Languages |
|------|----------------|-----------|
| **A — AST** (tree-sitter, full symbol + calls) | vendored `.c` grammar | Go, Rust, Java, TypeScript/JS, Kotlin, Ruby, Bash, Lua, C |
| **B — Regex** (lightweight, function/class only) | inline extractor | PHP, Swift, Zig, R, Elixir, Perl |
| **C — Text-only** (BM25/recall works, no symbols) | no extractor | YAML, JSON, Markdown, Dockerfile, plain text |

Phase 18 already ships Tier-A for: C#, SQL, C++, Python.  
Phase 32 adds tree-sitter for those 4. Phase 82 adds the remaining Tier-A list + all Tier-B.

---

## Task 1 — Language detection pipeline

**Files:**
- Create: `src/graph/lang_detect.hpp` / `.cpp`

```cpp
enum class Lang {
    Cpp, CSharp, Python, TypeScript, JavaScript,
    Go, Rust, Java, Kotlin, Ruby, Bash, Lua, C,
    PHP, Swift, Zig, R, Elixir, Perl,
    Sql, Yaml, Json, Markdown, Dockerfile,
    Unknown
};

Lang detectLang(const std::string& path, const std::string& first_line = "");
```

**Detection priority:**
1. Extension map (`.go` → Go, `.rs` → Rust, `.ts` → TypeScript, `.js` → JavaScript, etc.)
2. Shebang line (`#!/usr/bin/env python3`, `#!/bin/bash`)
3. Content heuristic (XML-like → XML, `<?php` → PHP)
4. Fallback: Unknown → Tier-C (text-only)

**Extension map (complete):**

| Extension(s) | Lang |
|---|---|
| `.cpp .cxx .cc .hpp .hxx` | Cpp |
| `.cs` | CSharp |
| `.py .pyi` | Python |
| `.ts .tsx` | TypeScript |
| `.js .jsx .mjs` | JavaScript |
| `.go` | Go |
| `.rs` | Rust |
| `.java` | Java |
| `.kt .kts` | Kotlin |
| `.rb .rake` | Ruby |
| `.sh .bash .zsh` | Bash |
| `.lua` | Lua |
| `.c .h` | C |
| `.php` | PHP |
| `.swift` | Swift |
| `.zig` | Zig |
| `.r .R` | R |
| `.ex .exs` | Elixir |
| `.pl .pm` | Perl |
| `.sql` | Sql |
| `.yaml .yml` | Yaml |
| `.json` | Json |
| `.md .mdx` | Markdown |
| `Dockerfile` | Dockerfile |

**Verify:** 50-fixture test: each extension → correct Lang enum.

---

## Task 2 — Tree-sitter grammar expansion

**Files:**
- Vendor new grammars (generated `.c` files):
  - `third_party/tree-sitter-go/` — `parser.c` (~150KB)
  - `third_party/tree-sitter-rust/` — `parser.c` (~250KB)
  - `third_party/tree-sitter-java/` — `parser.c` (~200KB)
  - `third_party/tree-sitter-javascript/` — reuse TS scanner (`parser.c` ~200KB)
  - `third_party/tree-sitter-kotlin/` — `parser.c` (~300KB)
  - `third_party/tree-sitter-ruby/` — `parser.c` (~200KB)
  - `third_party/tree-sitter-bash/` — `parser.c` (~100KB)
  - `third_party/tree-sitter-lua/` — `parser.c` (~80KB)
  - `third_party/tree-sitter-c/` — `parser.c` (~100KB)
- Create: `src/graph/symbol_extractor/treesitter_extractor.cpp` (extend existing Phase 32 impl)

**S-expression queries per language:**

```
Go:         (function_declaration name: (identifier) @name)
            (method_declaration name: (field_identifier) @name)
            (type_declaration name: (type_identifier) @name)

Rust:       (function_item name: (identifier) @name)
            (struct_item name: (type_identifier) @name)
            (impl_item type: (type_identifier) @name)
            (trait_item name: (type_identifier) @name)

Java:       (class_declaration name: (identifier) @name)
            (method_declaration name: (identifier) @name)
            (interface_declaration name: (identifier) @name)

TypeScript: (function_declaration name: (identifier) @name)
            (class_declaration name: (type_identifier) @name)
            (arrow_function) — anonymous, tag as "lambda"
            (interface_declaration name: (type_identifier) @name)

Kotlin:     (function_declaration (simple_identifier) @name)
            (class_declaration (simple_identifier) @name)
            (object_declaration (simple_identifier) @name)

Ruby:       (method name: (identifier) @name)
            (class name: (constant) @name)
            (module name: (constant) @name)

Bash:       (function_definition name: (word) @name)

Lua:        (function_declaration name: (identifier) @name)
            (local_function name: (identifier) @name)

C:          (function_definition declarator: (function_declarator
              declarator: (identifier) @name))
            (struct_specifier name: (type_identifier) @name)
```

**CMake opt-in per grammar:**

```cmake
option(ICMG_LANG_GO       "Include Go tree-sitter grammar"         ON)
option(ICMG_LANG_RUST      "Include Rust tree-sitter grammar"       ON)
option(ICMG_LANG_JAVA      "Include Java tree-sitter grammar"       ON)
option(ICMG_LANG_KOTLIN    "Include Kotlin tree-sitter grammar"     ON)
option(ICMG_LANG_RUBY      "Include Ruby tree-sitter grammar"       ON)
option(ICMG_LANG_BASH      "Include Bash tree-sitter grammar"       ON)
option(ICMG_LANG_LUA       "Include Lua tree-sitter grammar"        ON)
option(ICMG_LANG_C         "Include C tree-sitter grammar"          ON)
```

Default: all ON (grammars are small `.c` files, not runtime binaries). Disable to slim binary for embedded use.

**Binary size impact:** ~1.5–2MB total for all 9 new grammars (`.c` compiled to object).

---

## Task 3 — Regex extractors (Tier-B fallback)

**Files:**
- Create: `src/graph/symbol_extractor/generic_regex_extractor.cpp`

Single configurable extractor, driven by per-language pattern table:

```cpp
struct LangPattern {
    Lang          lang;
    std::regex    fn_pattern;    // group 1 = name
    std::regex    class_pattern;
    std::string   kind_fn    = "function";
    std::string   kind_class = "class";
};
```

Patterns for PHP, Swift, Zig, R, Elixir, Perl — simple function/class regex, no call-graph edges (those require AST).

**Verify:** PHP: extract 3 function names from fixture. Swift: struct + func. Zig: fn keyword.

---

## Task 4 — Extractor registry

**Files:**
- Create: `src/graph/symbol_extractor/extractor_registry.cpp`

```cpp
BaseSymbolExtractor* getExtractor(Lang lang);
// Returns: tree-sitter extractor if grammar compiled in,
//          regex extractor for Tier-B,
//          nullptr for Tier-C (no symbol extraction)
```

Scanner calls `getExtractor(detectLang(path))` — no per-language `if` chains in scanner itself.

```cpp
// scanner.cpp — after file node upsert:
auto* ext = extractor_registry::get(detectLang(fpath));
if (ext) {
    for (auto& sym : ext->extractSymbols(fpath, content))
        store_.upsertSymbolNode(file_node_id, sym);
}
```

---

## Task 5 — Language-aware CLI commands

**Files:**
- Modify: `src/cli/commands/graph_cmd.cpp`

**New flags and subcommands:**

```bash
# List detected languages in current project
icmg graph lang list
# Output:
#   Go         47 files   312 symbols
#   TypeScript 23 files   187 symbols
#   Bash        8 files    41 symbols
#   Markdown   15 files     0 symbols (text-only)

# Filter search by language
icmg graph search "parse token" --lang go
icmg graph search "error handling" --lang rust

# Filter symbol lookup by language
icmg graph symbol "NewClient" --lang go
icmg graph callers "HandleRequest" --lang go

# Show extractor method per language
icmg graph lang status
# Output:
#   Go         → tree-sitter (full AST + calls)
#   PHP        → regex (function/class only)
#   YAML       → text-only (no symbols)
```

**New DB view (no migration needed):**

```sql
CREATE VIEW IF NOT EXISTS lang_stats AS
SELECT
    lang,
    COUNT(DISTINCT file_node_id) AS files,
    COUNT(*) FILTER (WHERE kind != 'file') AS symbols
FROM graph_nodes
GROUP BY lang;
```

Requires adding `lang TEXT` column to `graph_nodes` — migration `0008_lang_column.sql`.

---

## Task 6 — `icmg graph update` incremental per-lang

**Files:**
- Modify: `src/cli/commands/graph_cmd.cpp` — add `--lang <lang>` filter

```bash
icmg graph update --lang go       # re-scan only .go files (fast)
icmg graph update --lang rust     # after adding Rust to existing project
icmg graph update --no-mem-sync   # existing flag, still works
```

Use case: user adds a new language to a project, wants to index just those files without full rescan.

---

## Task 7 — MCP tools extension

**Files:**
- Modify: `src/mcp/tools/symbol_tool.cpp` (Phase 18 created this)

Add `lang` param to existing MCP tools:
```json
{ "tool": "icmg_symbol_search", "query": "NewClient", "lang": "go" }
{ "tool": "icmg_graph_search",  "query": "retry logic", "lang": "rust" }
```

---

## Task 8 — Tests

**Files:**
- Create: `tests/graph/test_lang_detect.cpp` — 50 extension fixtures
- Create: `tests/graph/test_go_symbols.cpp` — Go functions, methods, structs
- Create: `tests/graph/test_rust_symbols.cpp` — fn, struct, impl, trait
- Create: `tests/graph/test_java_symbols.cpp` — class, method, interface
- Create: `tests/graph/test_bash_symbols.cpp` — function declarations
- Create: `tests/graph/test_extractor_registry.cpp` — getExtractor(Go) returns AST extractor

**Fixtures:** inline strings in each test, no external files needed.

---

## Verification Checklist

- [ ] `icmg graph lang list` on a polyglot repo shows all detected languages
- [ ] Go: `icmg graph symbol "ServeHTTP"` resolves to correct file + line
- [ ] Rust: `icmg graph callers "parse"` shows call-graph edges
- [ ] Java: class + method symbols extracted, generics not truncated
- [ ] TypeScript: interface + arrow-function symbols captured
- [ ] Bash: `#!/bin/bash` shebang detected even without `.sh` extension
- [ ] PHP (Tier-B): function names extracted, no crash
- [ ] YAML (Tier-C): no symbols, BM25 search still works
- [ ] `icmg graph update --lang go` only rescans `.go` files
- [ ] All Phase 18 + 32 tests still pass
- [ ] Binary size increase: ≤2.5MB vs Phase 32 baseline

---

## Rollback / Risk

| Risk | Mitigation |
|---|---|
| Grammar `.c` file compile error on MSVC | Test on Windows CI; most grammars support MSVC |
| Kotlin grammar immature / slow | Kotlin grammar is large (~300KB) — can ship later, disable via cmake flag |
| Binary too big | Disable grammars via `cmake -DICMG_LANG_KOTLIN=OFF` etc. |
| `lang` column migration breaks existing DBs | Migration is additive (`ALTER TABLE ADD COLUMN lang TEXT`) — safe |
| Shebang detection slow on large repos | Only read first line; O(1) per file |

---

## Dependency Graph

```
T1 (lang_detect)        — independent, start immediately
T2 (grammar vendor)     — after T1 (needs Lang enum)
T3 (regex Tier-B)       — after T1, parallel with T2
T4 (extractor registry) — after T2 + T3
T5 (CLI commands)       — after T4
T6 (graph update flag)  — after T4
T7 (MCP)                — after T4
T8 (tests)              — after T1-T7
```

**Recommended wall-clock:**
- Day 1–2: T1 + T3 (lang detect + regex extractors)
- Day 2–4: T2 (grammar vendoring, parallel per grammar)
- Day 4–5: T4 + T5 + T6 (registry + CLI)
- Day 5–6: T7 + T8 (MCP + tests)

---

## Impact After Phase 82

| Before | After |
|---|---|
| Symbol extraction: C#, SQL, C++, Python only | 18+ languages with AST or regex |
| `graph search` returns files regardless of language | `--lang go` scopes to Go files only |
| New project in Go/Rust = 0 symbol nodes | Full function/method/struct graph on first `graph update` |
| `icmg graph callers` works only in C# repos | Works in Go, Rust, Java, TypeScript repos too |
| Language of file invisible to graph | `lang` column visible in stats, search, and MCP tools |
