# Phase 18 — Function/Symbol-level Nodes

**Goal:** Two-tier graph: existing file nodes stay; add child symbol nodes (class, function, method, sp) with parent linkage. Edges can connect symbols → call-graph precision.

**Why:** File-level context = read 800-line files. Symbol-level = read one 30-line function + its 2 callers. 80%+ token reduction for "fix bug in X" tasks.

**Tech:** Schema extension, per-language symbol extractor, AST-aware edge resolver.

**Estimate:** 3-5 days.

**Assumptions:**
- File nodes remain authoritative for staleness/scan; symbol nodes regenerated on file change.
- Tier-1 languages: C#, SQL/SP. Tier-2: C++, Python. Others: file-level only (no degradation).
- Call-graph edges via regex are best-effort, not 100% precise (acceptable for navigation).

---

## Task 1 — Schema: parent-child + kind

**Files:**
- Create: `migrations/0007_symbol_nodes.sql`
- Modify: `src/core/embedded_migrations.hpp`

```sql
ALTER TABLE graph_nodes ADD COLUMN parent_id  INTEGER REFERENCES graph_nodes(id) ON DELETE CASCADE;
ALTER TABLE graph_nodes ADD COLUMN kind       TEXT DEFAULT 'file';
ALTER TABLE graph_nodes ADD COLUMN symbol_name TEXT;
ALTER TABLE graph_nodes ADD COLUMN signature  TEXT;
ALTER TABLE graph_nodes ADD COLUMN line_start INTEGER;
ALTER TABLE graph_nodes ADD COLUMN line_end   INTEGER;
ALTER TABLE graph_nodes ADD COLUMN body_hash  TEXT;
CREATE INDEX IF NOT EXISTS idx_graph_parent ON graph_nodes(parent_id);
CREATE INDEX IF NOT EXISTS idx_graph_kind   ON graph_nodes(kind);
CREATE INDEX IF NOT EXISTS idx_graph_symbol ON graph_nodes(symbol_name) WHERE symbol_name IS NOT NULL;
```

**Verify:** Fresh DB shows new columns; existing `kind='file'` for all rows.

---

## Task 2 — Symbol extractor interface

**Files:**
- Create: `src/graph/symbol_extractor/base_symbol_extractor.hpp`

```cpp
struct Symbol {
    std::string kind;        // class | function | method | sp
    std::string name;
    std::string signature;
    int         line_start = 0;
    int         line_end   = 0;
    std::string body_hash;   // FNV1a of body — for staleness
    std::vector<std::string> calls;  // names referenced inside body
};
class BaseSymbolExtractor {
public:
    virtual ~BaseSymbolExtractor() = default;
    virtual std::vector<Symbol> extractSymbols(const std::string& path,
                                                const std::string& content) = 0;
};
```

Register via existing `ICMG_REGISTER_EXTRACTOR` pattern (or new `ICMG_REGISTER_SYMBOL_EXTRACTOR`).

---

## Task 3 — C# symbol extractor

**Files:**
- Create: `src/graph/symbol_extractor/csharp_symbol_extractor.cpp`
- Test: `tests/graph/test_csharp_symbols.cpp`

**Regex sequence:**
- `class\s+(\w+)` → class symbol with body brace-matched
- `(public|private|internal|protected)?\s+(static\s+)?\S+\s+(\w+)\s*\([^)]*\)\s*\{` → method
- Inside method body: `(\w+)\s*\(` → call refs (filter keywords)

**Verify with test fixtures:** `Sync.cs` with 3 classes, 12 methods → extractor returns 15 symbols, line ranges correct.

---

## Task 4 — SQL/SP symbol extractor

**Files:**
- Create: `src/graph/symbol_extractor/sql_symbol_extractor.cpp`

**Patterns:**
- `CREATE\s+(OR\s+ALTER\s+)?(PROC|PROCEDURE|FUNCTION)\s+(\[?\w+\]?\.)?\[?(\w+)\]?` → sp/function symbol
- Inside body: `EXEC\s+(\w+)`, `\b(\w+)\s*\(` for fn calls.

**Reuse existing `sp_versions` table** if richer SP intel needed; symbol_node is just the lightweight pointer.

---

## Task 5 — Scanner integration

**Files:**
- Modify: `src/graph/scanner.cpp`

After file node upsert:
```cpp
auto sym_ext = getSymbolExtractor(lang);
if (sym_ext) {
    auto symbols = sym_ext->extractSymbols(fpath, content);
    for (auto& s : symbols) {
        GraphNode sn;
        sn.parent_id   = file_node_id;
        sn.kind        = s.kind;
        sn.symbol_name = s.name;
        sn.path        = fpath + "#" + s.name;     // unique
        sn.line_start  = s.line_start;
        sn.line_end    = s.line_end;
        sn.body_hash   = s.body_hash;
        store_.upsertNode(sn);
        // collect call refs for Pass-2 resolution as fn→fn edges
    }
}
```

**Stale handling:** symbol node's `body_hash` differs → re-upsert; symbols absent in new scan → soft-delete (or hard if parent file rescanned).

---

## Task 6 — Symbol-aware edge resolution

**Files:**
- Modify: `src/graph/graph_store.cpp::resolveAndInsertEdges`
- Add: `buildCallGraphEdges()` — second pass over symbol nodes

**Match logic:**
- Symbol A calls "X" → look up symbol_name='X' (prefer same file → same class → global) → insert `calls` edge with weight 1.5.
- Fallback: if X is class name → edge to class symbol, not method.

---

## Task 7 — Recall + context with symbol scope

**Files:**
- Modify: `src/cli/commands/recall_cmd.cpp` — `--kind <kind>` flag.
- Modify: `src/cli/commands/graph_cmd.cpp` — `graph context <file_or_symbol> --depth N --kind`.

**New command:**
```bash
icmg graph symbol <name>                # find symbol by name
icmg graph callers <symbol>             # who calls X
icmg graph callees <symbol>             # what X calls
```

---

## Task 8 — Viz drill-down

**Files:**
- Modify: `src/viz/graph_serializer.cpp` — emit symbol nodes with `parent_id`.
- Modify: `src/viz/html_template.hpp` — toggle "Show symbols" → expand file compound parents into child fn nodes.

---

## Task 9 — MCP tools

**Files:**
- Create: `src/mcp/tools/symbol_tool.cpp` — expose `icmg_symbol_search`, `icmg_callers`, `icmg_callees`.

---

## Verification Checklist

- [ ] C# project: `icmg graph stats --kind function` returns ≥10× file count.
- [ ] `icmg graph callers EnsureStockRegistered` lists known caller fns.
- [ ] `icmg recall "stock register" --kind function` returns symbol nodes, not files.
- [ ] Body-hash staleness: edit fn body → next scan re-upserts that one symbol, not whole file.
- [ ] `icmg viz --show-symbols` renders fn nodes inside file compound parents.
- [ ] All 15 existing tests still pass.
- [ ] New tests: `test_csharp_symbols`, `test_sql_symbols`, `test_symbol_edges`.

---

## Rollback Plan

`--kind file` (default) hides symbol nodes everywhere. Drop migration via `DELETE FROM graph_nodes WHERE kind != 'file'`.
