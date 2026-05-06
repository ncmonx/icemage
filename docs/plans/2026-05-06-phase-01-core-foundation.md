# Phase 01: Core Foundation

> **For Claude:** REQUIRED SUB-SKILL: Use `executing-plans` or `subagent-driven-development` to implement this plan.

**Goal:** Setup project structure, SQLite RAII wrapper, config engine, registry macros, hook bus, dan DB migration system.
**Architecture:** Single CMakeLists.txt dengan src/ terstruktur. SQLite amalgamation di third_party/. Registry pakai static map, hook bus pakai priority queue, migrator scan migrations/ folder.
**Tech Stack:** C++17, CMake 3.20+, Clang (cross-platform), SQLite 3.x amalgamation
**Assumptions:**
- SQLite3 amalgamation (sqlite3.h + sqlite3.c) di-download manual ke `third_party/sqlite3/` — TIDAK auto-download
- Clang tersedia di PATH — TIDAK fallback ke MSVC/GCC otomatis
- C++17 minimum — TIDAK support C++14

---

### Task 1: Project skeleton + CMakeLists.txt

**Files:**
- Create: `CMakeLists.txt`
- Create: `src/main.cpp`
- Create: `third_party/sqlite3/DOWNLOAD.md`
- Create: `.gitignore`

**Step 1: Buat CMakeLists.txt**

```cmake
cmake_minimum_required(VERSION 3.20)
project(icmg VERSION 0.1.0 LANGUAGES CXX C)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(sqlite3 STATIC third_party/sqlite3/sqlite3.c)
target_include_directories(sqlite3 PUBLIC third_party/sqlite3)

file(GLOB_RECURSE SOURCES "src/*.cpp")
add_executable(icmg ${SOURCES})
target_link_libraries(icmg PRIVATE sqlite3)
target_include_directories(icmg PRIVATE src)

if(WIN32)
    target_link_libraries(icmg PRIVATE ws2_32)
endif()
```

**Step 2: src/main.cpp minimal**
```cpp
#include <iostream>
int main(int argc, char* argv[]) {
    std::cout << "icmg v0.1.0\n";
    return 0;
}
```

**Step 3: Verify build**
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/icmg
```
Expected: `icmg v0.1.0`

---

### Task 2: SQLite RAII wrapper

**Files:**
- Create: `src/core/db.hpp`
- Create: `src/core/db.cpp`

**db.hpp interface:**
```cpp
#pragma once
#include <sqlite3.h>
#include <string>
#include <vector>
#include <functional>
#include <stdexcept>

namespace icmg::core {

class DbError : public std::runtime_error {
public:
    explicit DbError(const std::string& msg) : std::runtime_error(msg) {}
};

using Row = std::vector<std::string>;
using RowCallback = std::function<void(const Row&)>;

class Db {
public:
    explicit Db(const std::string& path);
    ~Db();
    Db(const Db&) = delete;

    void run(const std::string& sql);
    void run(const std::string& sql, const std::vector<std::string>& params);
    void query(const std::string& sql, const std::vector<std::string>& params, RowCallback cb);
    int64_t lastInsertId() const;
    int userVersion() const;
    void setUserVersion(int v);

private:
    sqlite3* db_ = nullptr;
    void checkRc(int rc, const std::string& ctx) const;
};

} // namespace icmg::core
```

**db.cpp:** WAL mode + busy timeout 5000ms + parameterized bind.

**Step 3: Verify**
```bash
cmake --build build
```
Expected: compile clean.

---

### Task 3: Config engine

**Files:**
- Create: `src/core/config.hpp`
- Create: `src/core/config.cpp`

Config path: `~/.icmg/config.json` (Windows: `%APPDATA%\icmg\config.json`)

```cpp
class Config {
public:
    static Config& instance();
    void load(const std::string& path);
    bool getBool(const std::string& key, bool def = false) const;
    int getInt(const std::string& key, int def = 0) const;
    std::string getString(const std::string& key, const std::string& def = "") const;
    void set(const std::string& key, const std::string& value);
    void save() const;
    std::string globalDbPath() const;
    std::string projectDbPath(const std::string& root) const;
};
```

Minimal JSON parser: flat key-value, no nested objects needed.

---

### Task 4: Registry macros

**Files:**
- Create: `src/core/registry.hpp`

Template Registry<Base> dengan static instance(). ICMG_REGISTER macro untuk:
- ICMG_REGISTER_EXTRACTOR(lang, Class)
- ICMG_REGISTER_FILTER(pattern, Class)
- ICMG_REGISTER_IMPORTER(name, Class)
- ICMG_REGISTER_COMMAND(name, Class)
- ICMG_REGISTER_MCP_TOOL(name, Class)

Pattern: static bool dummy = []() { Registry::instance().reg(key, factory); return true; }()

---

### Task 5: Hook bus

**Files:**
- Create: `src/core/hook_bus.hpp`
- Create: `src/core/hook_bus.cpp`

Events: PRE_STORE, POST_STORE, PRE_RECALL, POST_RECALL, PRE_GRAPH_SCAN, POST_GRAPH_SCAN, PRE_SP_ADD, POST_SP_ADD, PRE_RUN_CMD, POST_RUN_CMD

```cpp
class HookBus {
public:
    static HookBus& instance();
    void subscribe(HookEvent event, HookFn fn, int priority = 0);
    void emit(HookEvent event, HookContext& ctx);
};
```

ICMG_REGISTER_HOOK(event, fn, priority) macro.

---

### Task 6: DB Migration system

**Files:**
- Create: `src/core/migrator.hpp`
- Create: `src/core/migrator.cpp`
- Create: `migrations/0001_initial_schema.sql`

**Logic:**
1. Baca `PRAGMA user_version` dari DB
2. Scan `migrations/` cari file `NNNN_*.sql`
3. Run semua migration dengan nomor > current version
4. Update `PRAGMA user_version` setelah tiap migration

**migrations/0001_initial_schema.sql:** CREATE TABLE untuk semua tabel (memory_nodes, commands, graph_nodes, graph_edges, rules, structured_data, abbreviations, stored_procedures, sp_versions).

**Verify:**
```bash
cmake --build build && ./build/icmg
ls .icmg/
```
Expected: `.icmg/data.db` terbuat, schema version = 1.

---

### Task 7: CLI dispatcher skeleton

**Files:**
- Modify: `src/main.cpp`
- Create: `src/cli/base_command.hpp`
- Create: `src/cli/dispatcher.hpp`
- Create: `src/cli/dispatcher.cpp`

Subcommand groups: `store`, `recall`, `graph`, `run`, `sp`, `abbr`, `rule`, `data`, `project`, `cmd`, `stats`, `import`, `export`

**Verify:**
```bash
./build/icmg --help
./build/icmg bad-command
```
Expected: help text + "unknown command: bad-command"

---

### Task 8: Final build check + commit

**Step 1:**
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -- -j4
```
Expected: zero errors, zero warnings.

**Step 2:**
```bash
./build/icmg --version
```
Expected: `icmg 0.1.0`

**Step 3: Commit**
```bash
git init
git add CMakeLists.txt src/ migrations/ third_party/ docs/ .gitignore
git commit -m "feat: phase-01 core foundation"
```

---

## Amendments from Security & Architecture Review

### CRITICAL Fixes (must implement before anything else)

**A1 — NEVER use system() or popen(string)**
All command execution must use argv-array APIs:
- Windows: `CreateProcess` dengan `lpCommandLine` dari joined argv
- Unix: `execvp(argv[0], argv)` via `posix_spawn` atau fork+exec
- `popen()` hanya boleh dipakai dengan string yang sepenuhnya dikontrol (tidak ada user input di dalamnya)

Tambahkan di Task 2: `src/core/exec_utils.hpp`
```cpp
// Safe command execution — NO shell intermediary
struct ExecResult { std::string out; std::string err; int exit_code; int64_t duration_ms; };
ExecResult safeExec(const std::vector<std::string>& argv, bool merge_stderr = true);
```

**A2 — path_utils.hpp wajib ada di core**
Semua path validation terpusat:
```cpp
// src/core/path_utils.hpp
std::string canonicalize(const std::string& path);
bool isWithinRoot(const std::string& path, const std::string& root);
bool isSQLiteFile(const std::string& path);  // check magic bytes 53 51 4C 69 74 65
```
Dipakai oleh: importer, MCP tools, cross-project resolver, browser opener.

**A3 — Auto-init DB on first command**
Task 1 (pre-command hook) tambahkan:
```cpp
// Sebelum dispatch ke command handler:
if (!fs::exists(projectDbPath)) {
    fs::create_directories(icmgDir);
    fs::permissions(icmgDir, fs::perms::owner_all);  // chmod 0700
    db.open(projectDbPath);
    fs::permissions(projectDbPath, fs::perms::owner_read | fs::perms::owner_write); // chmod 0600
    migrator.runAll(db);
}
```

**A4 — Migration transaction wrapping**
Task 6 (migrator.cpp): wrap setiap migration dalam BEGIN/COMMIT:
```cpp
db.run("BEGIN TRANSACTION");
try {
    db.run(migrationSql);
    db.run("COMMIT");
    db.setUserVersion(version);
} catch (...) {
    db.run("ROLLBACK");
    throw;
}
```

**A5 — nlohmann/json sebagai third_party**
Tambahkan ke Task 1:
- Download `nlohmann/json.hpp` single header ke `third_party/nlohmann/json.hpp`
- Update CMakeLists.txt: `target_include_directories(icmg PRIVATE third_party)`
- Jangan buat JSON parser sendiri di Phase 13

### HIGH Fixes

**A6 — DB file permissions**
Setelah `sqlite3_open()` di db.cpp:
```cpp
#ifndef _WIN32
    chmod(path.c_str(), 0600);
#else
    // Set ACL owner-only via SetFileSecurity
#endif
```

**A7 — Verbose/debug mode global flag**
Tambahkan ke config.hpp:
```cpp
bool verbose = false;  // --verbose / -v flag
void log(const std::string& msg);  // prints if verbose=true
```

**A8 — Logging subsystem**
Tambahkan `src/core/logger.hpp`:
- Log file: `~/.icmg/icmg.log`
- Rotating: max 5 files × 1MB
- Levels: DEBUG, INFO, WARN, ERROR

**A9 — Health check command**
`icmg doctor` cek:
- SQLite PRAGMA integrity_check
- Schema version vs binary version
- Watcher PID alive
- config.json valid JSON
- Disk space tersedia

**A10 — CLI arg parser yang proper**
Gunakan simple flag parser yang handle:
- `--flag=value` dan `--flag value`
- Short flags: `-v` = `--verbose`, `-h` = `--help`, `-j` = `--json`
- Flags di posisi manapun (sebelum atau sesudah subcommand)

**A11 — Config file versioning**
```json
{ "version": 1, "features": {...}, "filters": {...} }
```
Validasi versi saat load, warn jika version field tidak ada.

### MEDIUM Additions

**A12 — Pager untuk long output**
```cpp
// Detect TTY + output > terminal height → pipe ke less
if (isatty(STDOUT_FILENO) && lineCount > terminalHeight()) {
    pipeToPager(output);
}
```

**A13 — Hook bus stable priority**
Priority queue ganti dengan sorted vector (stable_sort) untuk deterministic execution pada priority sama.
