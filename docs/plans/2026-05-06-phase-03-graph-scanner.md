# Phase 03: Graph CRUD + Scanner + Language Extractors

> **For Claude:** REQUIRED SUB-SKILL: Use `executing-plans` or `subagent-driven-development` to implement this plan.

**Goal:** Graph node/edge CRUD, recursive directory scanner, dan language extractors untuk 6 bahasa + generic fallback.
**Architecture:** GraphStore untuk DB ops. Scanner walk dir + dispatch ke Extractor per extension. Registry-based extractor lookup.
**Tech Stack:** C++17, filesystem (std::filesystem), regex
**Assumptions:** Phase 01 selesai. std::filesystem tersedia (C++17).

---

### Task 1: GraphNode + GraphEdge structs + GraphStore CRUD

**Files:**
- Create: `src/graph/graph_node.hpp`
- Create: `src/graph/graph_store.hpp`
- Create: `src/graph/graph_store.cpp`

**graph_node.hpp:**
```cpp
struct GraphNode {
    int64_t id = 0;
    std::string path;
    std::string lang;
    std::string context;    // concise summary
    std::string symbols;    // JSON: {imports:[],classes:[],functions:[]}
    int64_t size_bytes = 0;
    std::string file_hash;  // MD5 for staleness
    int64_t updated_at = 0;
};

struct GraphEdge {
    int64_t src = 0;
    int64_t dst = 0;
    std::string edge_type;  // imports|calls|inherits|includes
    double weight = 1.0;
};
```

**GraphStore methods:**
```cpp
class GraphStore {
public:
    explicit GraphStore(core::Db& db);
    int64_t upsertNode(const GraphNode& node);
    void upsertEdge(const GraphEdge& edge);
    std::optional<GraphNode> getNode(const std::string& path);
    std::vector<GraphNode> related(const std::string& path, int limit = 10);
    std::vector<GraphNode> search(const std::string& query, int limit = 10);
    std::vector<GraphEdge> edgesFrom(int64_t nodeId);
    std::vector<GraphEdge> edgesTo(int64_t nodeId);
    void removeNode(const std::string& path);
    std::vector<GraphNode> all() const;
    bool isStale(const std::string& path, const std::string& hash) const;
};
```

**Step 3: Verify**
```bash
./build/icmg graph context src/main.cpp   # before scan = "not found"
```

---

### Task 2: Base extractor interface

**Files:**
- Create: `src/graph/extractor/base_extractor.hpp`

```cpp
struct ExtractResult {
    std::string context;            // first doc comment or description
    std::vector<std::string> imports;
    std::vector<std::string> classes;
    std::vector<std::string> functions;
    std::vector<std::string> tables; // for SQL files
};

class BaseExtractor {
public:
    virtual ~BaseExtractor() = default;
    virtual ExtractResult extract(const std::string& path,
                                   const std::string& content) = 0;
    virtual std::vector<std::string> extensions() const = 0;
};
```

---

### Task 3: C/C++ Extractor

**Files:**
- Create: `src/graph/extractor/cpp_extractor.hpp`
- Create: `src/graph/extractor/cpp_extractor.cpp`

**Detect:**
- `#include "..."` / `#include <...>` → imports
- `class Foo`, `struct Foo`, `template<...> class Foo` → classes
- `void foo(`, `int foo(`, `Foo foo(`, `static foo(` → functions (top-level)
- First `/* ... */` or `//` block before anything → context
- Extensions: `.cpp`, `.cxx`, `.cc`, `.c`, `.hpp`, `.hxx`, `.h`

Registration: `ICMG_REGISTER_EXTRACTOR("cpp", CppExtractor);`

---

### Task 4: Python Extractor

**Files:**
- Create: `src/graph/extractor/python_extractor.cpp`

**Detect:**
- `import X`, `from X import Y` → imports
- `class Foo:`, `class Foo(Bar):` → classes
- `def foo(` → functions
- First `"""..."""` docstring → context
- Extensions: `.py`, `.pyw`

Registration: `ICMG_REGISTER_EXTRACTOR("python", PythonExtractor);`

---

### Task 5: JS/TS Extractor

**Files:**
- Create: `src/graph/extractor/js_extractor.cpp`

**Detect:**
- `import ... from '...'`, `require('...')` → imports
- `export class Foo`, `class Foo` → classes
- `function foo(`, `const foo = (`, `export function foo(`, `async function foo(` → functions
- First `/** ... */` JSDoc block → context
- Extensions: `.js`, `.jsx`, `.ts`, `.tsx`, `.mjs`

Registration: `ICMG_REGISTER_EXTRACTOR("js", JsExtractor);`

---

### Task 6: Go + Rust + Java Extractors

**Files:**
- Create: `src/graph/extractor/go_extractor.cpp`
- Create: `src/graph/extractor/rust_extractor.cpp`
- Create: `src/graph/extractor/java_extractor.cpp`

**Go:** `import`, `type X struct`, `func (x X) Foo(`, `func Foo(`
**Rust:** `use X`, `pub struct X`, `impl X`, `pub fn foo(`, `fn foo(`
**Java:** `import X`, `class X`, `public void foo(`, `interface X`

---

### Task 7: Generic fallback extractor

**Files:**
- Create: `src/graph/extractor/generic_extractor.cpp`

Fallback untuk semua extension tidak dikenal:
- Extract lines yang look like declarations (lines dengan `(` di tengah)
- Context = first non-empty line
- No imports extraction

Registration: used as fallback, not via ICMG_REGISTER_EXTRACTOR.

---

### Task 8: Directory Scanner

**Files:**
- Create: `src/graph/scanner.hpp`
- Create: `src/graph/scanner.cpp`

```cpp
class Scanner {
public:
    struct Options {
        int max_depth = 20;
        std::vector<std::string> include_langs;  // empty = all
        std::vector<std::string> ignore_dirs = {".git", "node_modules", "build", ".icmg"};
        bool skip_stale = true;  // skip files yang hash sama
    };

    explicit Scanner(GraphStore& store);
    int scan(const std::string& root, const Options& opts = {});

private:
    GraphStore& store_;
    std::string md5File(const std::string& path) const;
    std::string detectLang(const std::string& ext) const;
    BaseExtractor* getExtractor(const std::string& lang) const;
};
```

**Step 3: CLI command**
```
icmg graph scan <path> [--depth N] [--lang cpp,py,js]
icmg graph scan .       # scan current dir
```

**Verify:**
```bash
./build/icmg graph scan src/
./build/icmg graph context src/main.cpp
./build/icmg graph related src/core/db.cpp --limit 5
```
Expected: nodes populated, context + symbols terisi.

---

### Task 9: CLI: graph context + related + list

**Files:**
- Create: `src/cli/commands/graph_cmd.cpp`

```
icmg graph scan <path> [--depth N] [--lang X]
icmg graph context <file> [--json]
icmg graph related <file> [--limit N] [--json]
icmg graph list [--lang X] [--json]
icmg graph stats
```

**graph context output:**
```
File: src/core/db.cpp
Lang: C++  |  Size: 4.2 KB  |  Updated: 2h ago
Context: "SQLite RAII wrapper with WAL mode and parameterized queries"
Imports: sqlite3.h, string, vector, functional
Classes: Db, DbError
Functions: run, query, lastInsertId, userVersion, setUserVersion
Depends on (2): third_party/sqlite3/sqlite3.h, src/core/db.hpp
```

---

### Task 10: Final verify + commit

**Step 1:**
```bash
cmake --build build
./build/icmg graph scan . --depth 5
./build/icmg graph stats
./build/icmg graph context src/core/db.cpp
./build/icmg graph related src/core/db.cpp --limit 3
./build/icmg graph context src/core/db.cpp --json
```
Expected: nodes + edges populated, output terformat.

**Step 2: Commit**
```bash
git add src/graph/ src/cli/commands/graph_cmd.cpp
git commit -m "feat: phase-03 graph store + scanner + 6-language extractors"
```

---

## Amendments from Security & Architecture Review

### CRITICAL Fixes

**A1 — ON DELETE CASCADE + missing indexes**
Tambahkan ke migration 0001 atau migration baru:
```sql
-- Indexes wajib ada
CREATE INDEX idx_graph_edges_src ON graph_edges(src);
CREATE INDEX idx_graph_edges_dst ON graph_edges(dst);
CREATE INDEX idx_graph_nodes_lang ON graph_nodes(lang);
CREATE INDEX idx_graph_nodes_updated ON graph_nodes(updated_at);

-- Foreign key dengan CASCADE
-- (redefine di migration jika SQLite tidak support ALTER CONSTRAINT)
-- Implementasikan di GraphStore::removeNode() sebagai manual cascade:
void removeNode(const std::string& path) {
    auto node = getNode(path);
    db.run("DELETE FROM graph_edges WHERE src=? OR dst=?", {node.id, node.id});
    db.run("DELETE FROM graph_nodes WHERE id=?", {node.id});
}
```

**A2 — N+1 fix: graph context query**
Ganti multiple queries dengan JOIN:
```sql
SELECT n.*, e.edge_type, e.weight, r.path as related_path
FROM graph_nodes n
LEFT JOIN graph_edges e ON e.src = n.id
LEFT JOIN graph_nodes r ON r.id = e.dst
WHERE n.path = ?
```
Satu query, bukan N queries per edge.

### HIGH Additions

**A3 — Cycle detection**
```
icmg graph cycles [--lang cpp] [--json]
```
Implementasi: DFS dengan color marking (WHITE/GRAY/BLACK).
Output:
```
Cycle detected (3 files):
  src/a.cpp → src/b.cpp → src/c.cpp → src/a.cpp
```

**A4 — Orphan/dead code detection**
```
icmg graph orphans [--min-age 30d] [--exclude-pattern "main.*,index.*"]
```
Query: `SELECT * FROM graph_nodes WHERE id NOT IN (SELECT dst FROM graph_edges)`
Exclude known entry points (main.cpp, index.ts, mod.rs, __init__.py).

**A5 — Impact analysis**
```
icmg graph impact <file> [--depth N] [--json]
```
BFS melalui inbound edges (siapa yang depend ke file ini):
```
Impact of changing: src/core/db.hpp
  Direct (depth 1):  src/core/db.cpp, src/icm/memory_store.cpp  [2 files]
  Transitive (depth 2+): src/cli/dispatcher.cpp, src/graph/graph_store.cpp  [5 files]
  Total impact: 7 files
  Risk score: 7 × avg_importance(1.8) = 12.6
```

**A6 — SCC (Strongly Connected Components)**
```
icmg graph scc [--json]
```
Algoritma Tarjan. Output: komponen dengan >1 node = potensial tight coupling.

**A7 — Edge resolution pass**
Setelah scan selesai, jalankan resolution pass:
```cpp
void resolveEdges() {
    // Untuk setiap edge dengan dst_path (belum resolved ke dst ID):
    // Cari graph_nodes yang pathnya match (relative + absolute)
    // Update edge.dst = found_node.id
    // Jika tidak ditemukan: mark edge sebagai unresolved (dst = -1)
}
```

**A8 — Graph diff**
```
icmg graph diff [--since 7d] [--since-scan N]
```
Tambahkan tabel:
```sql
CREATE TABLE scan_runs (
    id INTEGER PRIMARY KEY,
    root_path TEXT,
    node_count INTEGER,
    edge_count INTEGER,
    created_at INTEGER
);
```
Diff: bandingkan node set sekarang vs snapshot scan sebelumnya.

### MEDIUM Additions

**A9 — .gitignore awareness**
```cpp
// Di Scanner::scan(), sebelum recurse:
GitIgnore gi;
gi.load(root + "/.gitignore");
if (gi.matches(relativePath)) continue;  // skip
```
Implementasi: parse .gitignore patterns (glob matching).

**A10 — Access counter**
```sql
ALTER TABLE graph_nodes ADD COLUMN access_count INTEGER DEFAULT 0;
```
Bump saat `graph context` atau `graph related` dipanggil.
```
icmg graph hot [--days 7] [--limit 20]
```
