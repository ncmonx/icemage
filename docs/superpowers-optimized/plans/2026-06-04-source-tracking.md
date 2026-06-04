# Source-Tracking (Provenance) Lapis 1 — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers-optimized:executing-plans to implement this plan task-by-task (inline; CLAUDE.md forbids Claude subagents). Steps use checkbox (`- [ ]`) syntax.

**Goal:** Setiap memory + persona entry bawa kolom `source` (free-text, default 'unknown') yang ditulis lewat `--source` dan ditampilkan saat dibaca.
**Architecture:** Unit M = `memory_nodes` (project DB) dapat kolom `source` via migration 0041 (file + embedded array) + MemoryNode/store/recall. Unit P = `profile_entries` (persona DB, no Migrator) dapat kolom via guarded ALTER (PRAGMA table_info check) di ctor ProfileStore + put/get/listZone. Source = metadata murni, TIDAK masuk BM25/keywords (ranking utuh).
**Tech Stack:** C++17, SQLite (Db/Migrator/MemoryStore/ProfileStore), CMake `add_icmg_test`, build `pwsh -File build.ps1 -Target both -RunTests`.
**Assumptions:**
- Assumes Migrator dev-mode pakai `migrations/*.sql` (cwd repo) — test/build kita jalan dari repo → file-based aktif. Embedded array dipakai HANYA binary jauh-dari-repo. Salah kalau test dijalankan dari luar repo (tidak — ctest jalan di repo).
- Assumes ALTER ADD COLUMN NOT NULL DEFAULT didukung SQLite (ya, sejak lama; baris lama baca default).
- Assumes persona DB tak punya Migrator (benar — ProfileStore bootstrap CREATE IF NOT EXISTS). Guarded ALTER aman.

---

## File Structure
- **Create** `migrations/0041_memory_source.sql` — ALTER memory_nodes ADD source.
- **Modify** `src/core/embedded_migrations.hpp` — append `{41, ...}` (deployed parity).
- **Modify** `src/imem/memory_node.hpp` — `MemoryNode.source`.
- **Modify** `src/imem/memory_store.cpp` — INSERT + SELECT include source.
- **Modify** `src/cli/commands/store_cmd.cpp` — `--source` flag.
- **Modify** `src/cli/commands/recall_cmd.cpp` (+ memory show) — display `[from: X]`.
- **Modify** `src/core/profile_store.hpp` + `.cpp` — guarded ALTER, put source param, ProfileRow.source, get/listZone.
- **Modify** `src/cli/commands/profile_cmd.cpp` — `--source` + display.
- **Create** `tests/imem/test_memory_source.cpp`, `tests/core/test_profile_source.cpp` + 2 `add_icmg_test` lines.

---

### Task 1: Unit M — memory_nodes.source migration + struct + store round-trip (TDD)

**Files:**
- Create: `migrations/0041_memory_source.sql`, `tests/imem/test_memory_source.cpp`
- Modify: `src/core/embedded_migrations.hpp`, `src/imem/memory_node.hpp`, `src/imem/memory_store.cpp`, `CMakeLists.txt`

**Does NOT cover:** TIDAK ubah ranking (source bukan keyword/FTS). TIDAK sentuh persona (Task 3).

- [ ] **Step 1: Write failing test** — `tests/imem/test_memory_source.cpp`

```cpp
// Provenance: memory_nodes carries a free-text source (default 'unknown').
#include "../test_main.hpp"
#include "../../src/imem/memory_store.hpp"
#include "../../src/imem/memory_node.hpp"
#include "../../src/core/db.hpp"
#include <string>
using namespace icmg;

static std::string tmpDb() { return std::string("memory_source_test.db"); }

TEST("memory: store with source round-trips") {
    core::Db db(tmpDb());
    imem::MemoryStore ms(db);
    imem::MemoryNode n;
    n.topic = "decisions-x"; n.content = "use approach B"; n.source = "kak Cahyo";
    int64_t id = ms.store(n);
    ASSERT_TRUE(id > 0);
    auto got = ms.get(id);
    ASSERT_TRUE(got.has_value());
    ASSERT_EQ(got->source, std::string("kak Cahyo"));
}

TEST("memory: store without source defaults to unknown") {
    core::Db db(tmpDb());
    imem::MemoryStore ms(db);
    imem::MemoryNode n; n.topic = "t"; n.content = "c";  // source left default
    int64_t id = ms.store(n);
    auto got = ms.get(id);
    ASSERT_TRUE(got.has_value());
    ASSERT_EQ(got->source, std::string("unknown"));
}
```

(Note implementer: confirm `MemoryStore::get(id) -> std::optional<MemoryNode>` exists; if the accessor differs, use the actual read path — adapt assertion to the real getter. Verify via `grep -n "std::optional<MemoryNode>\\|MemoryNode get" src/imem/memory_store.hpp`.)

- [ ] **Step 2: Run test to verify it fails**

Run: `pwsh -File build.ps1 -Target both` then `ctest --test-dir build-msvc-full -R test_memory_source`
Expected: FAIL — `MemoryNode` has no member `source` (compile error).

- [ ] **Step 3: Implement**

`migrations/0041_memory_source.sql`:
```sql
-- v2.x: provenance — memory_nodes carries free-text source (default 'unknown').
ALTER TABLE memory_nodes ADD COLUMN source TEXT NOT NULL DEFAULT 'unknown';
```

`src/core/embedded_migrations.hpp` — append before the closing `};` of the array (after the `{34, ...}` block):
```cpp
        {41, R"SQL(
ALTER TABLE memory_nodes ADD COLUMN source TEXT NOT NULL DEFAULT 'unknown';
)SQL"},
```

`src/imem/memory_node.hpp` — add field after `git_sha`:
```cpp
    std::string source = "unknown";   // provenance: who/what supplied this info
```

`src/imem/memory_store.cpp` — extend the INSERT (around line 243) to include `source`:
```cpp
        "INSERT INTO memory_nodes(topic,content,keywords,importance,frequency,"
        "last_used,created_at,expires_at,zone,created_by,git_sha,source) VALUES(?,?,?,?,?,?,?,?,?,?,?,?)",
        {effective.topic, effective.content, effective.keywords,
         std::to_string(effective.importance),
         std::to_string(effective.frequency),
         std::to_string(now), std::to_string(now),
         expires, zone, core::currentUser(), git_sha,
         effective.source.empty() ? std::string("unknown") : effective.source});
```
Then in the SELECT/row-hydration path (wherever MemoryNode is built from a row — grep `n.git_sha =` or the column list in the SELECT), add `source` to the selected columns and assign `node.source = row[...]`. (Implementer: locate the hydrator via `grep -n "git_sha" src/imem/memory_store.cpp`; mirror it for `source`.)

`CMakeLists.txt` — add near other imem tests:
```cmake
add_icmg_test(test_memory_source tests/imem/test_memory_source.cpp)  # provenance: memory source column
```

- [ ] **Step 4: Run test to verify it passes**

Run: `pwsh -File build.ps1 -Target both -RunTests` then `ctest --test-dir build-msvc-full -R test_memory_source --output-on-failure`
Expected: PASS (2 TESTs).

- [ ] **Step 5: Commit**

```bash
git add migrations/0041_memory_source.sql src/core/embedded_migrations.hpp src/imem/memory_node.hpp src/imem/memory_store.cpp tests/imem/test_memory_source.cpp CMakeLists.txt
git commit -m "feat(provenance): memory_nodes.source column + store round-trip (Unit M, migration 0041)"
```

---

### Task 2: Unit M — `store --source` flag + recall display

**Files:**
- Modify: `src/cli/commands/store_cmd.cpp`, `src/cli/commands/recall_cmd.cpp`

**Does NOT cover:** TIDAK ubah ranking. `memory show` display ditangani di langkah ini juga jika getter sama; kalau beda command, biarkan recall dulu (show = stretch).

- [ ] **Step 1: Verify current state**

Run: `grep -n "\\-\\-source" src/cli/commands/store_cmd.cpp`
Expected: NO match (flag belum ada).

- [ ] **Step 2: Implement** — `store_cmd.cpp`: parse `--source`, set on node.

```cpp
// near other flagValue parses (e.g. --importance):
std::string source = flagValue(args, "--source", "unknown");
// ... where the MemoryNode is built before ms.store(node):
node.source = source.empty() ? "unknown" : source;
```

`recall_cmd.cpp` — where each recalled row prints, append source:
```cpp
// after the existing content/topic print for a row `r`:
if (!r.source.empty() && r.source != "unknown")
    std::cout << "  [from: " << r.source << "]\n";
```
(Implementer: match the existing per-row print style in recall_cmd.cpp; insert the `[from: ...]` line within that loop. Use the actual row variable name.)

- [ ] **Step 3: Build + smoke**

Run: `pwsh -File build.ps1` then:
`./build-msvc-full/Release/icmg.exe store --topic test-src --source "kak Cahyo" "provenance smoke" && ./build-msvc-full/Release/icmg.exe recall "provenance smoke"`
Expected: recall output shows `[from: kak Cahyo]`.

- [ ] **Step 4: Commit**

```bash
git add src/cli/commands/store_cmd.cpp src/cli/commands/recall_cmd.cpp
git commit -m "feat(provenance): store --source flag + recall displays [from: X] (Unit M CLI)"
```

---

### Task 3: Unit P — profile_entries.source guarded ALTER + put/get/listZone (TDD)

**Files:**
- Modify: `src/core/profile_store.hpp`, `src/core/profile_store.cpp`
- Create: `tests/core/test_profile_source.cpp`
- Modify: `CMakeLists.txt`

**Does NOT cover:** TIDAK ubah profile_cmd CLI (Task 4). Guarded ALTER hanya untuk kolom `source`.

- [ ] **Step 1: Write failing test** — `tests/core/test_profile_source.cpp`

```cpp
// Provenance: profile_entries carries a free-text source (default 'unknown').
#include "../test_main.hpp"
#include "../../src/core/profile_store.hpp"
#include "../../src/core/db.hpp"
#include <string>
using namespace icmg::core;

static std::string tmpDb() { return std::string("profile_source_test.db"); }

TEST("profile: put with source round-trips via get") {
    Db db(tmpDb()); ProfileStore ps(db);
    ps.put("u_src", "_vision", "core", "note", "MIMPI", "kak Cahyo");
    std::string c, k, src;
    ASSERT_TRUE(ps.get("u_src", "_vision", "core", c, k, src));
    ASSERT_EQ(src, std::string("kak Cahyo"));
}

TEST("profile: put without source defaults to unknown") {
    Db db(tmpDb()); ProfileStore ps(db);
    ps.put("u_def", "_x", "k", "note", "v");   // source omitted -> default param
    std::string c, k, src;
    ps.get("u_def", "_x", "k", c, k, src);
    ASSERT_EQ(src, std::string("unknown"));
}

TEST("profile: bootstrap ALTER is idempotent across constructs") {
    Db db(tmpDb());
    { ProfileStore a(db); a.put("u_i", "_z", "k", "note", "v", "s"); }
    { ProfileStore b(db);                 // second ctor: guarded ALTER must not throw
      std::string c, k, src; b.get("u_i", "_z", "k", c, k, src);
      ASSERT_EQ(src, std::string("s")); }
}

TEST("profile: listZone populates source") {
    Db db(tmpDb()); ProfileStore ps(db);
    ps.put("u_l", "_z", "k1", "note", "v1", "kak Cahyo");
    auto rows = ps.listZone("u_l", "_z");
    ASSERT_TRUE(rows.size() >= 1);
    ASSERT_EQ(rows[0].source, std::string("kak Cahyo"));
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `pwsh -File build.ps1 -Target both` then `ctest --test-dir build-msvc-full -R test_profile_source`
Expected: FAIL — `put` has no 6th param / `get` no source overload / `ProfileRow` no `source` (compile error).

- [ ] **Step 3: Implement**

`src/core/profile_store.hpp` — add to ProfileRow + signatures:
```cpp
struct ProfileRow {
    std::string zone, key, kind, content;
    long long updated_at = 0;
    std::string source = "unknown";    // provenance
};
// ...
void put(const std::string& user, const std::string& zone, const std::string& key,
         const std::string& kind, const std::string& content,
         const std::string& source = "unknown");
bool get(const std::string& user, const std::string& zone, const std::string& key,
         std::string& content_out, std::string& kind_out, std::string& source_out);
```

`src/core/profile_store.cpp`:
- In the constructor, AFTER the CREATE TABLE + index, add guarded ALTER:
```cpp
    // Provenance: add source column to legacy tables (guarded — no Migrator on persona DB).
    bool hasSource = false;
    for (auto& r : db_.query("PRAGMA table_info(profile_entries)")) {
        if (r.size() >= 2 && r[1] == "source") { hasSource = true; break; }
    }
    if (!hasSource)
        db_.run("ALTER TABLE profile_entries ADD COLUMN source TEXT NOT NULL DEFAULT 'unknown'");
```
(Implementer: confirm `Db::query` returns `vector<vector<string>>` with col-1 = column name from PRAGMA table_info; adapt indexing to the real Db API via `grep -n "query" src/core/db.hpp`.)

- Update `put` to write source:
```cpp
void ProfileStore::put(const std::string& user, const std::string& zone, const std::string& key,
                       const std::string& kind, const std::string& content,
                       const std::string& source) {
    db_.run("INSERT INTO profile_entries(user_id,zone,key,kind,content,updated_at,source) "
            "VALUES(?,?,?,?,?,strftime('%s','now'),?) "
            "ON CONFLICT(user_id,zone,key) DO UPDATE SET kind=excluded.kind, "
            "content=excluded.content, updated_at=excluded.updated_at, source=excluded.source",
            {user, zone, key, kind, content, source.empty() ? std::string("unknown") : source});
}
```
(Implementer: match the EXISTING put() INSERT/upsert syntax in profile_store.cpp — preserve its conflict-clause; only add the `source` column + param.)

- Update `get` to the 6-arg overload returning source; update `listZone` SELECT to include `source` and set `row.source`.

`CMakeLists.txt`:
```cmake
add_icmg_test(test_profile_source tests/core/test_profile_source.cpp)  # provenance: persona source column
```

- [ ] **Step 4: Run test to verify it passes**

Run: `pwsh -File build.ps1 -Target both -RunTests` then `ctest --test-dir build-msvc-full -R "test_profile_source|test_profile_store" --output-on-failure`
Expected: PASS (new 4 TESTs + existing profile_store unchanged-green).

- [ ] **Step 5: Commit**

```bash
git add src/core/profile_store.hpp src/core/profile_store.cpp tests/core/test_profile_source.cpp CMakeLists.txt
git commit -m "feat(provenance): profile_entries.source guarded ALTER + put/get/listZone (Unit P)"
```

---

### Task 4: Unit P — `profile add --source` + get/list display

**Files:**
- Modify: `src/cli/commands/profile_cmd.cpp`

**Does NOT cover:** qa-* subcommands (out of scope; only add/get/list).

- [ ] **Step 1: Verify current state**

Run: `grep -n "\\-\\-source" src/cli/commands/profile_cmd.cpp`
Expected: NO match.

- [ ] **Step 2: Implement** — parse `--source` (default "unknown"); pass to `ps.put`; display in get/list.

```cpp
// in the arg-parse loop, alongside --content:
else if (args[i] == "--source" && i + 1 < args.size()) source = args[++i];
// declare with the other locals: std::string source = "unknown";

// sub == "add": ps.put(user, zone, key, kind, content, source);

// sub == "get": fetch 6-arg, then:
//   std::cout << "[" << k << " | from: " << src << "] " << c << "\n";

// sub == "list": for each row:
//   std::cout << "  " << r.zone << "/" << r.key << " (" << r.kind
//             << " | from: " << r.source << ")\n";
```

- [ ] **Step 3: Build + smoke**

Run: `pwsh -File build.ps1` then:
`./build-msvc-full/Release/icmg.exe profile add --zone _t --key k --source "kak Cahyo" --content "hi" && ./build-msvc-full/Release/icmg.exe profile get --zone _t --key k`
Expected: output shows `from: kak Cahyo`.

- [ ] **Step 4: Commit**

```bash
git add src/cli/commands/profile_cmd.cpp
git commit -m "feat(provenance): profile add --source + get/list display [from: X] (Unit P CLI)"
```

---

### Task 5: Full gate + 5-sync + backlog drift

**Files:** none (verification)

- [ ] **Step 1: Full build + ctest**

Run: `pwsh -File build.ps1 -Target both -RunTests`
Expected: PASS; ctest target count +2 (test_memory_source, test_profile_source); mono-test +8 TESTs.

- [ ] **Step 2: 5-sync (icmg)**

```bash
icmg graph update
icmg store --topic decisions-provenance --source "kak Cahyo" "source-tracking Lapis 1 DONE local: memory + persona source column, free-text, default unknown, display [from:X]. Migration 0041 (file+embedded). Auto truth-weight (Lapis 2) deferred."
icmg verify --command "ctest --test-dir build-msvc-full -R 'test_memory_source|test_profile_source'"
```

- [ ] **Step 3: Log embedded-drift backlog**

```bash
icmg known-issue add "embedded_migrations.hpp array maxes at {34} but migrations/*.sql at 0040 -> deployed binaries (away from repo) miss migrations 35-40" --fix "Sync embedded array with migrations/0035-0040; OR generate embedded from migrations/ at build time. Separate from provenance feature."
```

- [ ] **Step 4: HOLD ship** — per cadence #30922. Bump version + docs + 7-gate when kak Cahyo oks.

---

## Self-Review

**Spec coverage:** Unit M (Task 1+2), Unit P (Task 3+4), display both, default 'unknown', migration file+embedded (Task 1), guarded ALTER PRAGMA-check (Task 3 #bootstrap-idempotent test), non-goal ranking-untouched (Task 1 "Does NOT cover"), gate+drift (Task 5). All spec sections covered.
**Placeholder scan:** No TBD/TODO. Implementer-notes point to concrete grep anchors (real verification steps), not vague placeholders — code blocks present for every code step.
**Type consistency:** `MemoryNode.source` (string) consistent Task 1↔2. `ProfileStore::put(...,source="unknown")` + `get(...,source_out)` + `ProfileRow.source` consistent Task 3↔4. `--source` flag default "unknown" consistent Task 2↔4.
