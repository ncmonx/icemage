# Phase 10: Stored Procedure Engine + SQL Parser

> **For Claude:** REQUIRED SUB-SKILL: Use `executing-plans` or `subagent-driven-development` to implement this plan.

**Goal:** Store, search, dan manage SQL stored procedures dengan auto-extract context, parameters, table deps, dan SP-to-SP dependencies.
**Architecture:** SpStore CRUD + version history. SqlParser lexical extraction. SP nodes terintegrasi ke graph (phase 03).
**Tech Stack:** C++17, SQLite, regex
**Assumptions:** Phase 01 + GraphStore dari Phase 03 tersedia.

---

### Task 1: StoredProcedure struct + SpStore

**Files:**
- Create: `src/sp/stored_procedure.hpp`
- Create: `src/sp/sp_store.hpp`
- Create: `src/sp/sp_store.cpp`

```cpp
struct SpParameter {
    std::string name;
    std::string type;
    std::string direction;  // IN|OUT|INOUT
    std::string default_val;
};

struct StoredProcedure {
    int64_t id = 0;
    std::string name;
    std::string db_type;        // mysql|postgresql|mssql|oracle
    std::string database_name;
    std::string content;        // full SQL
    std::string context;        // description
    std::vector<SpParameter> parameters;
    std::string return_type;
    std::vector<std::string> tables_used;
    std::vector<std::string> sp_dependencies;
    std::string scope_path;
    std::string tags;
    int version = 1;
    int64_t created_at = 0;
    int64_t updated_at = 0;
};

class SpStore {
public:
    explicit SpStore(core::Db& db);
    int64_t add(const StoredProcedure& sp);
    bool update(const std::string& name, const std::string& sql, const std::string& note = "");
    bool remove(const std::string& name);
    std::optional<StoredProcedure> get(const std::string& name) const;
    std::vector<StoredProcedure> list(const std::string& db_type = "", const std::string& database = "") const;
    std::vector<StoredProcedure> search(const std::string& query, int limit = 10) const;
    std::vector<StoredProcedure> usesTable(const std::string& table) const;
    std::vector<StoredProcedure> calledBy(const std::string& sp_name) const;
    std::vector<StoredProcedure> history(const std::string& name) const;
};
```

---

### Task 2: SQL Parser (lexical, not AST)

**Files:**
- Create: `src/sp/sql_parser.hpp`
- Create: `src/sp/sql_parser.cpp`

```cpp
struct ParseResult {
    std::string sp_name;
    std::string db_type;
    std::string context;           // first comment block
    std::vector<SpParameter> parameters;
    std::vector<std::string> tables;
    std::vector<std::string> sp_calls;
};

class SqlParser {
public:
    ParseResult parse(const std::string& sql, const std::string& hint_db_type = "") const;
private:
    std::string detectDbType(const std::string& sql) const;
    std::string extractContext(const std::string& sql) const;
    std::vector<SpParameter> extractParams(const std::string& sql, const std::string& db_type) const;
    std::vector<std::string> extractTables(const std::string& sql) const;
    std::vector<std::string> extractSpCalls(const std::string& sql) const;
};
```

**Extraction rules:**
- SP name: `CREATE (OR REPLACE)? PROCEDURE (\w+)` (case-insensitive)
- Context: `-- Description: ...` atau first `/* ... */` block
- Params MSSQL: `@\w+ \w+` pattern
- Params MySQL/PG: `(IN|OUT|INOUT) \w+ \w+` pattern
- Tables: `FROM (\w+)`, `JOIN (\w+)`, `INTO (\w+)`, `UPDATE (\w+)`, `DELETE FROM (\w+)`
- SP calls: `EXEC(UTE)? (\w+)` (MSSQL), `CALL (\w+)` (MySQL/PG)

---

### Task 3: SP graph integration

Setelah SP di-add → auto-create graph nodes:
- SP node: `sp://<database_name>/<sp_name>` sebagai path
- Table nodes: `table://<database_name>/<table_name>`
- Edges: SP→Table (reads/writes), SP→SP (sp_calls)

Register di GraphStore sebagai special node type.

---

### Task 4: CLI commands

**Files:**
- Create: `src/cli/commands/sp_cmd.cpp`

```
icmg sp add sp_GetBKM ./sql/sp_GetBKM.sql [--db mssql] [--database db_keuangan]
icmg sp add sp_GetBKM ./sql/sp_GetBKM.sql --context "Ambil BKM per cabang"
icmg sp list [--db mssql] [--database db_keuangan] [--tag X]
icmg sp show sp_GetBKM
icmg sp show sp_GetBKM --json
icmg sp search "bukti kas"
icmg sp deps sp_GetBKM              # dependency tree
icmg sp uses-table bukti_kas_masuk  # semua SP yang pakai tabel ini
icmg sp update sp_GetBKM ./sql/sp_v2.sql --note "tambah filter status"
icmg sp history sp_GetBKM
icmg sp remove sp_GetBKM
```

**sp show output:**
```
[mssql] sp_GetBKM  v2  (db: db_keuangan)
Context: "Ambil semua bukti kas masuk berdasarkan tanggal dan cabang"

Parameters:
  @tgl_awal   DATE     IN
  @tgl_akhir  DATE     IN
  @cabang_id  INT      IN  (default: NULL = semua cabang)

Tables used: bukti_kas_masuk, cabang, users
Calls: sp_ValidateCabang, sp_GetUser

--- SQL ---
CREATE PROCEDURE sp_GetBKM
  @tgl_awal DATE,
  ...
```

---

### Task 5: Final verify + commit

**Step 1:**
```bash
# Buat dummy SQL file
echo "-- Description: Get BKM by date range
CREATE PROCEDURE sp_GetBKM @tgl_awal DATE, @tgl_akhir DATE AS
BEGIN SELECT * FROM bukti_kas_masuk WHERE tgl BETWEEN @tgl_awal AND @tgl_akhir
EXEC sp_ValidateDates @tgl_awal END" > /tmp/test_sp.sql

./build/icmg sp add sp_GetBKM /tmp/test_sp.sql --db mssql
./build/icmg sp show sp_GetBKM
./build/icmg sp search "bukti kas"
./build/icmg sp uses-table bukti_kas_masuk
./build/icmg sp deps sp_GetBKM
```

**Step 2: Commit**
```bash
git add src/sp/ src/cli/commands/sp_cmd.cpp
git commit -m "feat: phase-10 stored procedure engine + SQL lexical parser"
```

---

## Amendments from Architecture Review

### HIGH Additions

**A1 — SP diff antara versi**
```
icmg sp diff sp_GetBKM v1 v2
icmg sp diff sp_GetBKM HEAD~1
```
Implementasi: Myers diff algorithm atau shell out ke `diff -u` (safer, cross-platform).
Output: unified diff format antara dua version content.

**A2 — SP linting**
```
icmg sp lint sp_GetBKM
icmg sp lint --all
```
Checks:
- Semua `sp_calls` ada di store → ERROR jika tidak
- Semua `tables_used` plausible (bukan keyword SQL) → WARN jika tidak
- Parameter yang di-declare tapi tidak dipakai di body → WARN
- SP yang memanggil dirinya sendiri (recursive) tanpa explicit note → WARN

**A3 — SP template/dry-run**
```
icmg sp template sp_GetBKM
```
Output ready-to-execute call dengan placeholder:
```sql
-- MSSQL
EXEC sp_GetBKM
    @tgl_awal  = '2024-01-01',   -- DATE IN
    @tgl_akhir = '2024-12-31',   -- DATE IN
    @cabang_id = NULL            -- INT IN (optional)
```

**A4 — SP impact analysis**
```
icmg sp impact-table bukti_kas_masuk [--depth 2]
```
BFS melalui `sp_dependencies`:
```
Table: bukti_kas_masuk
Direct users (depth 1):
  sp_GetBKM, sp_ValidateBKM, sp_ReportBKM  [3 SPs]
Transitive (depth 2):
  sp_MonthlyReport calls sp_GetBKM         [1 SP]
Total impact: 4 SPs
```

**A5 — SP search by parameter type**
```
icmg sp list --param-type DATE
icmg sp list --param-direction IN --param-type INT
```
Implementasi: JSON parse dari `parameters` field di DB.

**A6 — State machine SQL parser**
Upgrade SQL parser dari pure regex ke simple state machine untuk handle:
- String literals: `'SELECT * FROM fake_table'` tidak dianggap real table
- Block comments: `/* FROM fake */` tidak dianggap import
- Line comments: `-- FROM fake` tidak dianggap import

States: DEFAULT → IN_STRING → IN_LINE_COMMENT → IN_BLOCK_COMMENT
