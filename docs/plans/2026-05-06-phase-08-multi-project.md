# Phase 08: Multi-project Registry

> **For Claude:** REQUIRED SUB-SKILL: Use `executing-plans` or `subagent-driven-development` to implement this plan.

**Goal:** Registry project di global.db, memungkinkan query lintas project dengan --project flag di semua commands.
**Architecture:** ProjectRegistry di global.db. Per-project punya .icmg/data.db sendiri. --project flag attach ke Db yang berbeda.
**Tech Stack:** C++17, SQLite
**Assumptions:** Phase 01 selesai. global.db schema sudah include projects table.

---

### Task 1: Global DB + projects table

**Files:**
- Create: `src/core/global_db.hpp`
- Create: `src/core/global_db.cpp`

```cpp
struct Project {
    int64_t id = 0;
    std::string name;
    std::string path;
    std::string db_path;    // path ke .icmg/data.db
    std::string description;
    int64_t registered_at = 0;
};

class GlobalDb {
public:
    static GlobalDb& instance();
    void init();  // create/migrate global.db
    int64_t addProject(const Project& p);
    bool removeProject(const std::string& name);
    std::optional<Project> getProject(const std::string& name) const;
    std::vector<Project> listProjects() const;
    bool projectExists(const std::string& name) const;
private:
    std::unique_ptr<core::Db> db_;
};
```

global.db path: `~/.icmg/global.db`

---

### Task 2: Project context resolver

**Files:**
- Create: `src/core/project_context.hpp`
- Create: `src/core/project_context.cpp`

```cpp
class ProjectContext {
public:
    // Resolve: --project flag atau auto-detect dari cwd
    static ProjectContext resolve(const std::string& project_name = "");

    core::Db& db();
    std::string name() const;
    std::string rootPath() const;
    bool isRemote() const;  // true jika --project flag ke project lain

private:
    std::string name_;
    std::string root_path_;
    std::unique_ptr<core::Db> db_;
    bool remote_ = false;
};
```

Auto-detect: walk up dari cwd, cari `.icmg/data.db`.

---

### Task 3: --project flag di semua commands

**Files:**
- Modify: `src/cli/dispatcher.cpp`
- Modify: semua command files

Parse `--project <name>` sebelum dispatch ke command handler.
Inject ProjectContext ke semua command handlers.

**Semua commands support:**
```
icmg recall "query" --project smart-home
icmg graph context src/db.go --project smart-home
icmg rule list --project nestle
icmg data list --project nestle --type model
icmg sp search "user" --project mulia-jaya
icmg abbr list --project nestle --domain accounting
```

**Does NOT cover:** Write operations ke project lain (read-only cross-project).

---

### Task 4: CLI: project commands

**Files:**
- Create: `src/cli/commands/project_cmd.cpp`

```
icmg project add <name> <path> [--description "desc"]
icmg project add smart-home /d/Personal/Project/Smart\ Home
icmg project list
icmg project remove <name>
icmg project info <name>
icmg project current          # show active project (dari cwd)
```

**project list output:**
```
Name          Path                            DB
smart-home    /d/Personal/Project/Smart Home  .icmg/data.db (249 nodes)
nestle        /d/ES/MJ/koding/nestle          .icmg/data.db (12 nodes)
* icm-graph   /d/Data Kerja/Personal/AI/...   .icmg/data.db (current)
```

---

### Task 5: Final verify + commit

**Step 1:**
```bash
./build/icmg project add test-proj /tmp/test-proj
mkdir -p /tmp/test-proj/.icmg
./build/icmg project list
./build/icmg project info test-proj
./build/icmg project current
# Test cross-project read
./build/icmg recall "test" --project test-proj
```
Expected: project terdaftar, cross-project recall bekerja.

**Step 2: Commit**
```bash
git add src/core/global_db* src/core/project_context* src/cli/commands/project_cmd.cpp
git commit -m "feat: phase-08 multi-project registry + cross-project queries"
```

---

## Amendments from Security & Architecture Review

### HIGH Fixes

**A1 — Project ownership verification**
```cpp
void GlobalDb::addProject(const Project& p) {
    // Verify direktori dimiliki user yang sama
#ifndef _WIN32
    struct stat st;
    if (stat(p.path.c_str(), &st) == 0 && st.st_uid != getuid()) {
        throw SecurityError("Project path owned by different user. Cross-user project access not allowed.");
    }
#endif
    // Juga verify db_path adalah file .icmg/data.db yang valid
    if (!core::path_utils::isSQLiteFile(p.db_path)) {
        throw SecurityError("db_path is not a valid SQLite file.");
    }
    // ...insert...
}
```

**A2 — Access control warning**
Tampilkan warning saat cross-project query:
```
icmg recall "auth" --project smart-home
⚠  Cross-project read: smart-home (/d/Personal/Smart Home)
   No authentication — any local user can read this data.
   (Disable warning: icmg config set cross_project_warn=false)
```

**A3 — Global DB migration system**
`GlobalDb::init()` harus run migrations untuk global.db:
```cpp
void GlobalDb::init() {
    db_->open(globalDbPath);
    Migrator globalMigrator("migrations/global/");
    globalMigrator.runAll(*db_);
}
```
Buat folder `migrations/global/` terpisah dari `migrations/` (project).
Migration pertama: `migrations/global/0001_projects_table.sql`

**A4 — Path canonicalization sebelum store**
```cpp
// Sebelum insert project:
p.path = core::path_utils::canonicalize(p.path);
p.db_path = core::path_utils::canonicalize(p.db_path);
if (!core::path_utils::isWithinRoot(p.db_path, p.path)) {
    throw SecurityError("db_path must be within project path.");
}
```
