# Phase 11: Import (ICM/Graphify/JSON/CSV) + Export

> **For Claude:** REQUIRED SUB-SKILL: Use `executing-plans` or `subagent-driven-development` to implement this plan.

**Goal:** Import data dari ICM existing (MCP tool), Graphify GRAPH_REPORT.md, generic JSON, dan CSV. Export ke JSON.
**Architecture:** BaseImporter interface, registry-based. Each importer map external format ke icmg internal schema.
**Tech Stack:** C++17, SQLite, regex (untuk parse Markdown)
**Assumptions:** Phase 01-10 selesai. ICM DB path diketahui user (bukan auto-detect).

---

### Task 1: BaseImporter interface

**Files:**
- Create: `src/import/base_importer.hpp`

```cpp
struct ImportStats {
    int memory_nodes = 0;
    int graph_nodes = 0;
    int graph_edges = 0;
    int abbreviations = 0;
    int stored_procs = 0;
    int rules = 0;
    int errors = 0;
    std::vector<std::string> error_messages;
};

class BaseImporter {
public:
    virtual ~BaseImporter() = default;
    virtual ImportStats import(const std::string& source,
                               core::Db& target_db,
                               const std::string& project_name = "") = 0;
    virtual std::string name() const = 0;
    virtual std::string description() const = 0;
};
```

---

### Task 2: ICM importer

**Files:**
- Create: `src/import/icm_importer.cpp`

ICM storage: SQLite DB (icm.exe binary dari icm MCP tool).
Cari ICM DB di common paths: `~/.icm/`, `%APPDATA%/icm/`, atau user-specified.

**Mapping ICM → icmg memory_nodes:**
```
icm.topic     → memory_nodes.topic
icm.content   → memory_nodes.content
icm.importance → memory_nodes.importance
icm.keywords  → memory_nodes.keywords
icm.last_used → memory_nodes.last_used
icm.frequency → memory_nodes.frequency
```

Jika ICM schema tidak dikenal → fallback: dump semua rows sebagai generic memory nodes.

Registration: `ICMG_REGISTER_IMPORTER("icm", IcmImporter);`

---

### Task 3: Graphify importer

**Files:**
- Create: `src/import/graphify_importer.cpp`

Graphify output: `graphify-out/GRAPH_REPORT.md`
Parse Markdown sections untuk extract:
- File paths (dari heading atau code blocks)
- Node metadata (language, size)
- Community assignments
- Edge relationships (jika ada)

**Mapping → graph_nodes + graph_edges:**
```
graphify.node.path      → graph_nodes.path
graphify.node.type      → graph_nodes.lang
graphify.community_id   → graph_nodes.tags (prefix: "community:")
graphify.edges          → graph_edges (if parseable)
```

Setelah import: trigger scanner untuk fill context + symbols (karena Graphify tidak punya).

Registration: `ICMG_REGISTER_IMPORTER("graphify", GraphifyImporter);`

---

### Task 4: JSON importer

**Files:**
- Create: `src/import/json_importer.cpp`

Format JSON yang diterima (icmg export format):
```json
{
  "version": "1.0",
  "memory_nodes": [...],
  "graph_nodes": [...],
  "graph_edges": [...],
  "abbreviations": [...],
  "stored_procedures": [...],
  "rules": [...]
}
```

Juga support flat array: `[{...}, {...}]` dengan type inference dari fields.

Registration: `ICMG_REGISTER_IMPORTER("json", JsonImporter);`

---

### Task 5: CSV importer

**Files:**
- Create: `src/import/csv_importer.cpp`

Format per type:
```
# abbreviations CSV
short,full,domain
bkm,bukti kas masuk,accounting

# memory CSV
topic,content,importance
preferences,caveman mode,high
```

Registration: `ICMG_REGISTER_IMPORTER("csv", CsvImporter);`

---

### Task 6: Exporter

**Files:**
- Create: `src/export/exporter.hpp`
- Create: `src/export/exporter.cpp`

```cpp
class Exporter {
public:
    explicit Exporter(core::Db& db);
    std::string toJson(const std::string& type = "") const;
    // type: "memory"|"graph"|"abbreviations"|"sp"|"rules"|"" (all)
};
```

---

### Task 7: CLI commands

**Files:**
- Create: `src/cli/commands/import_cmd.cpp`
- Create: `src/cli/commands/export_cmd.cpp`

```
# Import
icmg import icm [--path ~/.icm/memory.db]
icmg import icm --path "C:/Users/Admin/AppData/Roaming/icm/data.db"
icmg import graphify ./graphify-out/GRAPH_REPORT.md
icmg import graphify ./graphify-out/ --project smart-home
icmg import json backup.json
icmg import json memory.json --type memory
icmg import csv abbr.csv --type abbreviation
icmg import csv sp-list.csv --type sp

# Export
icmg export                             # export all ke stdout
icmg export > backup.json
icmg export --type memory > memory.json
icmg export --type graph > graph.json
icmg export --project smart-home > smart-home-backup.json
```

**import output:**
```
Import from ICM: ~/.icm/memory.db
  Memory nodes: 47 imported, 0 skipped, 0 errors
  Duration: 123ms
Done.
```

---

### Task 8: Final verify + commit

**Step 1:**
```bash
# Test JSON round-trip
./build/icmg export > /tmp/backup.json
./build/icmg import json /tmp/backup.json
./build/icmg recall "test"

# Test CSV
echo "short,full,domain\ntest,testing abbreviation,general" > /tmp/test.csv
./build/icmg import csv /tmp/test.csv --type abbreviation
./build/icmg abbr list
```

**Step 2: Commit**
```bash
git add src/import/ src/export/ src/cli/commands/import_cmd.cpp src/cli/commands/export_cmd.cpp
git commit -m "feat: phase-11 import/export (ICM, Graphify, JSON, CSV)"
```

---

## Amendments from Security & Architecture Review

### CRITICAL Fixes

**A1 — Transaction rollback untuk semua import**
```cpp
ImportStats BaseImporter::importWithTransaction(core::Db& db, ...) {
    db.run("BEGIN TRANSACTION");
    try {
        auto stats = doImport(db, ...);
        db.run("COMMIT");
        return stats;
    } catch (...) {
        db.run("ROLLBACK");
        throw;
    }
}
```
Partial import = tidak boleh terjadi. All-or-nothing.

**A2 — Path canonicalization di semua importers**
```cpp
// Di GraphifyImporter, setelah parse path dari Markdown:
node.path = core::path_utils::canonicalize(parsedPath);
if (!core::path_utils::isWithinRoot(node.path, projectRoot)) {
    stats.errors++;
    stats.error_messages.push_back("Skipped path outside root: " + parsedPath);
    continue;
}
```

**A3 — SQLite magic byte verification sebelum open**
```cpp
// Di IcmImporter dan cross-project DB open:
if (!core::path_utils::isSQLiteFile(dbPath)) {
    throw ImportError("File is not a valid SQLite database: " + dbPath);
}
```

### HIGH Fixes

**A4 — Import file size limit**
```cpp
if (fs::file_size(sourcePath) > 100 * 1024 * 1024) {  // 100MB
    throw ImportError("Import file too large (max 100MB): " + sourcePath);
}
```

**A5 — Schema validation untuk JSON import**
```cpp
struct JsonImportValidator {
    void validateMemoryNode(const nlohmann::json& j) {
        if (!j.contains("topic") || !j["topic"].is_string())
            throw ValidationError("memory_node missing 'topic' string field");
        if (j["content"].get<std::string>().size() > 1024*1024)
            throw ValidationError("content exceeds 1MB limit");
        // ... validate all fields
    }
};
```
Unknown fields → log warning, skip (tidak error).

### MEDIUM Additions

**A6 — Streaming export untuk dataset besar**
```cpp
class StreamingExporter {
public:
    void exportTo(std::ostream& out, const std::string& type = "") {
        out << "{\n";
        if (type.empty() || type == "memory") {
            out << "\"memory_nodes\": [\n";
            // Stream satu per satu dari DB, bukan load semua ke memory
            db.query("SELECT * FROM memory_nodes", {}, [&](const Row& r) {
                out << serializeMemoryNode(r) << ",\n";
            });
            out << "]\n";
        }
        // ... other types
        out << "}\n";
    }
};
```

**A7 — Null byte validation**
```cpp
// Untuk semua string yang diimport:
if (str.find('\0') != std::string::npos) {
    throw ValidationError("String contains null byte — possible injection attempt");
}
```
