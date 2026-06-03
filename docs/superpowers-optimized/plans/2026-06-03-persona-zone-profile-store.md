# Persona-Zone Profile/Skill Store — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers-optimized:executing-plans (INLINE — Claude subagent dispatch FORBIDDEN per project rule). Steps use checkbox (`- [ ]`).

**Goal:** Add a zoned, fast-searchable profile/skill store to the exe-dir persona DB — store text "files" as zoned entries (behavioral profiles, reusable skills, notes), looked up independently and quickly without scanning any project DB.

**Architecture:** A new `profile_entries` table in the existing exe-dir persona DB (`icmg-persona.db`, cross-project shared, ACL-relaxed; same store as `user_personas`). Entries keyed by `(user_id, zone, key)` with a `kind` tag (`profile` / `skill` / `note`). Created at first-use via `CREATE TABLE IF NOT EXISTS` (same bootstrap pattern as `user_personas`, NOT a numbered migration). Optional FTS5 mirror for fast content search (same shape as `graph_fts`). A small pure header validates/normalizes zone+key; a `profile_store` wraps the DB CRUD; `icmg profile` command exposes add/get/list/search.

**Tech Stack:** C++17, `core::Db` (run/query), persona_db exe-dir DB, SQLite FTS5, `ICMG_REGISTER_COMMAND`, CMake `add_icmg_test`.

**Scope / content policy:** This is a content-NEUTRAL capability — it stores whatever text the user puts in (work profiles, coding skills, style notes). The store itself is agnostic. (Implementer note: do not author sexual/romantic persona content; the feature is for healthy profiles/skills.)

**Assumptions:**
- Assumes persona DB is reachable via `core::personaDbAvailable()` / `personaDb()`, falling back to global DB when exe-dir is not writable (mirror `writePersona` behavior) — Task 3 adapts to that exact fallback.
- Assumes FTS5 is compiled in (it is — `graph_fts` uses it). If FTS proves heavy, Task 4 (FTS) is optional and can be dropped; LIKE fallback still works.
- Scope = store + CRUD + zone + search. NOT: cross-device sync, encryption beyond what persona DB already has, UI.

---

## File Structure

| File | Responsibility |
|------|----------------|
| `src/core/profile_key.hpp` (create) | Pure: normalize/validate zone + key (lowercase, trim, allowed charset), compose storage key. |
| `tests/core/test_profile_key.hpp`→`.cpp` (create) | Unit tests for normalization/validation. |
| `src/core/profile_store.hpp` + `.cpp` (create) | DB CRUD over `profile_entries` (+ optional FTS): put/get/list-by-zone/search. Bootstraps table at first use. |
| `src/cli/commands/profile_cmd.cpp` (create) | `icmg profile add/get/list/search/forget` command + `qa-add/qa-find` (Task 6). |
| `tests/core/test_profile_store.cpp` (create) | Integration tests against a temp DB. |
| `src/core/prompt_history.hpp` + `.cpp` (create, Task 6) | Zoned prompt→response history; record + find-similar (exact-hash + FTS/LIKE prompt match). |
| `tests/core/test_prompt_history.cpp` (create, Task 6) | Record + find-similar round-trip. |
| `CMakeLists.txt` (modify) | `add_icmg_test` lines (test_profile_key, test_profile_store, test_prompt_history). |

---

### Task 1: Pure zone/key normalization core

**Files:**
- Create: `src/core/profile_key.hpp`
- Test: `tests/core/test_profile_key.cpp`

**Does NOT cover:** storage. Pure string normalization only. Zone/key normalized to `[a-z0-9_-]`, lowercased, trimmed; empty → `"default"` zone / rejected key.

- [ ] **Step 1: Write failing test**

```cpp
// tests/core/test_profile_key.cpp
// Pure normalization for the zoned profile store: zone+key -> canonical form.
#include "../test_main.hpp"
#include "../../src/core/profile_key.hpp"
#include <string>
using namespace icmg::core;

TEST("normalizeZone: lowercases + trims + default on empty") {
    ASSERT_EQ(normalizeZone("  Work Notes "), std::string("work-notes"));
    ASSERT_EQ(normalizeZone(""), std::string("default"));
}

TEST("normalizeKey: lowercases + collapses to [a-z0-9_-]") {
    ASSERT_EQ(normalizeKey("My Skill #1!"), std::string("my-skill-1"));
}

TEST("normalizeKey: empty -> empty (caller rejects)") {
    ASSERT_EQ(normalizeKey("   "), std::string(""));
}

TEST("validKind: known kinds only, default profile") {
    ASSERT_EQ(validKind("skill"), std::string("skill"));
    ASSERT_EQ(validKind("note"),  std::string("note"));
    ASSERT_EQ(validKind("xyz"),   std::string("profile"));  // unknown -> profile
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
```

- [ ] **Step 2: Run test to verify it fails**

Run: `pwsh -File build.ps1 -Reconfigure -Target test` then `& 'C:\icmg-build\build-msvc-full\icmg_test.exe' profile_key`
Expected: FAIL — `profile_key.hpp` missing.

- [ ] **Step 3: Implement minimal change**

```cpp
// src/core/profile_key.hpp
#pragma once
// Pure normalization for the zoned profile/skill store. Canonicalizes zone + key to a
// safe slug ([a-z0-9_-]); validates the kind tag. No I/O.
#include <cctype>
#include <string>

namespace icmg::core {

inline std::string slugify(const std::string& in) {
    std::string out;
    bool lastDash = false;
    for (char ch : in) {
        unsigned char c = (unsigned char)ch;
        if (std::isalnum(c)) { out += (char)std::tolower(c); lastDash = false; }
        else if (!out.empty() && !lastDash) { out += '-'; lastDash = true; }
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    return out;
}

inline std::string normalizeZone(const std::string& zone) {
    std::string s = slugify(zone);
    return s.empty() ? std::string("default") : s;
}

inline std::string normalizeKey(const std::string& key) {
    return slugify(key);   // empty -> "" so caller can reject
}

inline std::string validKind(const std::string& kind) {
    if (kind == "skill" || kind == "note" || kind == "profile") return kind;
    return "profile";
}

}  // namespace icmg::core
```

- [ ] **Step 4: Run test to verify it passes**

Run: `& 'C:\icmg-build\build-msvc-full\icmg_test.exe' profile_key`
Expected: PASS (4/4).

- [ ] **Step 5: Commit**

```bash
git add src/core/profile_key.hpp tests/core/test_profile_key.cpp
git commit -m "feat(profile-store): pure zone/key normalization core, 4 TDD"
```

---

### Task 2: profile_store schema + CRUD

**Files:**
- Create: `src/core/profile_store.hpp`, `src/core/profile_store.cpp`
- Test: `tests/core/test_profile_store.cpp`

**Does NOT cover:** FTS search (Task 4 — `search()` here is a LIKE fallback). Table bootstrapped at first use via `CREATE TABLE IF NOT EXISTS` (mirror persona_db).

- [ ] **Step 1: Write failing test (integration, temp DB)**

```cpp
// tests/core/test_profile_store.cpp
// Zoned profile/skill store CRUD over a temp DB.
#include "../test_main.hpp"
#include "../../src/core/profile_store.hpp"
#include "../../src/core/db.hpp"
#include <string>
#include <vector>
using namespace icmg::core;

static std::string tmpDb() { return std::string("profile_store_test.db"); }

TEST("profile_store: put then get round-trips content") {
    Db db(tmpDb());
    ProfileStore ps(db);
    ps.put("u1", "work", "lint-rule", "skill", "use clang-tidy with --fix");
    std::string content, kind;
    bool ok = ps.get("u1", "work", "lint-rule", content, kind);
    ASSERT_TRUE(ok);
    ASSERT_EQ(content, std::string("use clang-tidy with --fix"));
    ASSERT_EQ(kind, std::string("skill"));
}

TEST("profile_store: listZone returns only that zone") {
    Db db(tmpDb());
    ProfileStore ps(db);
    ps.put("u1", "work", "a", "note", "x");
    ps.put("u1", "play", "b", "note", "y");
    auto rows = ps.listZone("u1", "work");
    ASSERT_EQ(rows.size(), (size_t)1);
    ASSERT_EQ(rows[0].key, std::string("a"));
}

TEST("profile_store: put same key updates (upsert)") {
    Db db(tmpDb());
    ProfileStore ps(db);
    ps.put("u1", "work", "k", "note", "v1");
    ps.put("u1", "work", "k", "note", "v2");
    std::string c, kind; ps.get("u1", "work", "k", c, kind);
    ASSERT_EQ(c, std::string("v2"));
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
```

- [ ] **Step 2: Run test to verify it fails**

Run: build + `& 'C:\icmg-build\build-msvc-full\icmg_test.exe' profile_store`
Expected: FAIL — `profile_store.hpp` missing.

- [ ] **Step 3: Implement**

```cpp
// src/core/profile_store.hpp
#pragma once
// Zoned profile/skill store. Entries keyed (user_id, zone, key) with a kind tag,
// in the exe-dir persona DB (cross-project shared). Table bootstrapped at first use.
#include "db.hpp"
#include <string>
#include <vector>

namespace icmg::core {

struct ProfileRow {
    std::string zone, key, kind, content;
    long long updated_at = 0;
};

class ProfileStore {
public:
    explicit ProfileStore(Db& db);
    void put(const std::string& user, const std::string& zone, const std::string& key,
             const std::string& kind, const std::string& content);
    bool get(const std::string& user, const std::string& zone, const std::string& key,
             std::string& content_out, std::string& kind_out);
    std::vector<ProfileRow> listZone(const std::string& user, const std::string& zone);
    std::vector<ProfileRow> search(const std::string& user, const std::string& query); // LIKE fallback
    void forget(const std::string& user, const std::string& zone, const std::string& key);
private:
    Db& db_;
    void ensure();
};

}  // namespace icmg::core
```

```cpp
// src/core/profile_store.cpp
#include "profile_store.hpp"
#include "profile_key.hpp"

namespace icmg::core {

ProfileStore::ProfileStore(Db& db) : db_(db) { ensure(); }

void ProfileStore::ensure() {
    db_.run("CREATE TABLE IF NOT EXISTS profile_entries("
            "user_id TEXT NOT NULL, zone TEXT NOT NULL, key TEXT NOT NULL,"
            "kind TEXT NOT NULL DEFAULT 'profile', content TEXT NOT NULL,"
            "updated_at INTEGER NOT NULL DEFAULT (strftime('%s','now')),"
            "PRIMARY KEY(user_id, zone, key))");
    db_.run("CREATE INDEX IF NOT EXISTS ix_profile_zone ON profile_entries(user_id, zone)");
}

void ProfileStore::put(const std::string& user, const std::string& zone, const std::string& key,
                       const std::string& kind, const std::string& content) {
    db_.run("INSERT INTO profile_entries(user_id,zone,key,kind,content,updated_at) "
            "VALUES(?,?,?,?,?,strftime('%s','now')) "
            "ON CONFLICT(user_id,zone,key) DO UPDATE SET "
            "kind=excluded.kind, content=excluded.content, updated_at=excluded.updated_at",
            {user, normalizeZone(zone), normalizeKey(key), validKind(kind), content});
}

bool ProfileStore::get(const std::string& user, const std::string& zone, const std::string& key,
                       std::string& content_out, std::string& kind_out) {
    bool found = false;
    db_.query("SELECT content, kind FROM profile_entries WHERE user_id=? AND zone=? AND key=?",
              {user, normalizeZone(zone), normalizeKey(key)},
              [&](const Row& r) { if (r.size() >= 2) { content_out = r[0]; kind_out = r[1]; found = true; } });
    return found;
}

std::vector<ProfileRow> ProfileStore::listZone(const std::string& user, const std::string& zone) {
    std::vector<ProfileRow> out;
    db_.query("SELECT zone,key,kind,content,updated_at FROM profile_entries "
              "WHERE user_id=? AND zone=? ORDER BY updated_at DESC",
              {user, normalizeZone(zone)},
              [&](const Row& r) {
                  if (r.size() >= 5) out.push_back({r[0], r[1], r[2], r[3], std::stoll(r[4])});
              });
    return out;
}

std::vector<ProfileRow> ProfileStore::search(const std::string& user, const std::string& query) {
    std::vector<ProfileRow> out;
    db_.query("SELECT zone,key,kind,content,updated_at FROM profile_entries "
              "WHERE user_id=? AND content LIKE ? ORDER BY updated_at DESC LIMIT 20",
              {user, "%" + query + "%"},
              [&](const Row& r) {
                  if (r.size() >= 5) out.push_back({r[0], r[1], r[2], r[3], std::stoll(r[4])});
              });
    return out;
}

void ProfileStore::forget(const std::string& user, const std::string& zone, const std::string& key) {
    db_.run("DELETE FROM profile_entries WHERE user_id=? AND zone=? AND key=?",
            {user, normalizeZone(zone), normalizeKey(key)});
}

}  // namespace icmg::core
```

- [ ] **Step 4: Run test to verify it passes**

Run: `& 'C:\icmg-build\build-msvc-full\icmg_test.exe' profile_store`
Expected: PASS (3/3).

- [ ] **Step 5: Commit**

```bash
git add src/core/profile_store.hpp src/core/profile_store.cpp tests/core/test_profile_store.cpp
git commit -m "feat(profile-store): zoned CRUD over persona DB (put/get/listZone/search/forget), 3 TDD"
```

---

### Task 3: `icmg profile` command

**Files:**
- Create: `src/cli/commands/profile_cmd.cpp`

**Does NOT cover:** unit test (command = smoke-verified). Uses exe-dir persona DB via `personaDbAvailable()/personaDb()`, falling back to global DB (mirror `writePersona`).

- [ ] **Step 1: Implement**

```cpp
// src/cli/commands/profile_cmd.cpp
// `icmg profile` — zoned profile/skill store in the exe-dir persona DB.
//   profile add --zone Z --key K [--kind skill|note|profile] --content "..."
//   profile get --zone Z --key K
//   profile list --zone Z
//   profile search "<query>"
//   profile forget --zone Z --key K
#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/persona_db.hpp"
#include "../../core/global_db.hpp"
#include "../../core/profile_store.hpp"
#include "../../core/user_identity.hpp"
#include <iostream>
#include <string>
#include <vector>

namespace icmg::cli {

class ProfileCommand : public BaseCommand {
public:
    std::string name() const override { return "profile"; }
    std::string description() const override { return "Zoned profile/skill store (persona DB)"; }
    void usage() const override {
        std::cout << "Usage: icmg profile add|get|list|search|forget [--zone Z --key K --kind X --content ...]\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty()) { usage(); return 1; }
        const std::string sub = args[0];
        std::string user = core::currentUserId();   // adapt to real identity helper
        core::Db& db = core::personaDbAvailable() ? core::personaDb() : core::GlobalDb::instance().db();
        core::ProfileStore ps(db);

        std::string zone, key, kind = "profile", content, query;
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "--zone" && i+1 < args.size()) zone = args[++i];
            else if (args[i] == "--key" && i+1 < args.size()) key = args[++i];
            else if (args[i] == "--kind" && i+1 < args.size()) kind = args[++i];
            else if (args[i] == "--content" && i+1 < args.size()) content = args[++i];
            else if (sub == "search" && args[i][0] != '-') query = args[i];
        }

        if (sub == "add") {
            if (key.empty() || content.empty()) { std::cerr << "need --key and --content\n"; return 1; }
            ps.put(user, zone, key, kind, content);
            std::cout << "[profile add] " << zone << "/" << key << " (" << kind << ") saved.\n";
            return 0;
        }
        if (sub == "get") {
            std::string c, k;
            if (ps.get(user, zone, key, c, k)) { std::cout << "[" << k << "] " << c << "\n"; return 0; }
            std::cerr << "not found\n"; return 1;
        }
        if (sub == "list") {
            for (auto& r : ps.listZone(user, zone))
                std::cout << "  " << r.zone << "/" << r.key << " (" << r.kind << ")\n";
            return 0;
        }
        if (sub == "search") {
            for (auto& r : ps.search(user, query))
                std::cout << "  " << r.zone << "/" << r.key << ": " << r.content.substr(0,80) << "\n";
            return 0;
        }
        if (sub == "forget") { ps.forget(user, zone, key); std::cout << "[profile forget] done.\n"; return 0; }
        usage(); return 1;
    }
};

ICMG_REGISTER_COMMAND("profile", ProfileCommand);

}  // namespace icmg::cli
```

- [ ] **Step 2: Build + smoke**

Run: `pwsh -File build.ps1 -Reconfigure -Target icmg` then in project dir:
`icmg profile add --zone work --key lint --kind skill --content "clang-tidy --fix"`,
`icmg profile list --zone work`, `icmg profile get --zone work --key lint`, `icmg profile search "clang"`.
Expected: add saves; list/get/search return it. (Adapt `currentUserId()` + `GlobalDb` accessor to real symbols read at impl time.)

- [ ] **Step 3: Commit**

```bash
git add src/cli/commands/profile_cmd.cpp
git commit -m "feat(profile-store): icmg profile add/get/list/search/forget command"
```

---

### Task 4 (OPTIONAL): FTS5 fast search

**Files:**
- Modify: `src/core/profile_store.hpp` + `.cpp`

**Does NOT cover:** if FTS proves heavy/flaky, skip — Task 2 `search()` LIKE fallback already works.

- [ ] **Step 1:** Add `profile_entries_fts` FTS5 virtual table + sync triggers in `ensure()` (mirror `graph_fts` triggers in `embedded_migrations.hpp` lines ~744-763), and make `search()` use `MATCH bm25()` with a LIKE fallback when the query has no FTS-safe terms (mirror `fts_query.hpp` injection-proof prefixing).
- [ ] **Step 2:** Add a test `profile_store: FTS search ranks content match` and verify.
- [ ] **Step 3:** Commit `feat(profile-store): FTS5 fast search with LIKE fallback`.

---

### Task 6: Prompt→response history (similar-prompt recall)

**Files:**
- Create: `src/core/prompt_history.hpp` + `src/core/prompt_history.cpp`
- Create: `tests/core/test_prompt_history.cpp`
- Modify: `src/cli/commands/profile_cmd.cpp` (add `qa-add` / `qa-find` subcommands)

**Why:** when a prompt repeats (or is similar), recall the past prompt+response so the
solution is reused instead of re-derived — saves tokens + time. Stored in the persona DB,
zoned, independent of project DBs.

**Does NOT cover:** semantic-embedding similarity (that needs the ONNX path — future). This
task does EXACT-match (normalized-prompt hash) + lexical FTS5/LIKE on the prompt text.

**Schema (bootstrapped at first use, persona DB):**
```sql
CREATE TABLE IF NOT EXISTS prompt_history(
    user_id    TEXT NOT NULL,
    zone       TEXT NOT NULL,
    prompt     TEXT NOT NULL,
    response   TEXT NOT NULL,
    prompt_key TEXT NOT NULL,   -- normalized slug of prompt, for exact-match dedup/upsert
    created_at INTEGER NOT NULL DEFAULT (strftime('%s','now')),
    PRIMARY KEY(user_id, zone, prompt_key)
);
CREATE INDEX IF NOT EXISTS ix_ph_zone ON prompt_history(user_id, zone);
-- optional FTS5 (mirror graph_fts) on (prompt) for lexical similar-prompt search.
```

- [ ] **Step 1: Write failing test**

```cpp
// tests/core/test_prompt_history.cpp
// Zoned prompt->response history: exact recall (normalized) + lexical find-similar.
#include "../test_main.hpp"
#include "../../src/core/prompt_history.hpp"
#include "../../src/core/db.hpp"
#include <string>
#include <vector>
using namespace icmg::core;

static std::string phDb() { return std::string("prompt_history_test.db"); }

TEST("prompt_history: recall exact (normalized) returns stored response") {
    Db db(phDb());
    PromptHistory ph(db);
    ph.record("u_ph1", "work", "How do I FIX the Build?", "run cmake --preset");
    std::string resp;
    // different casing/spacing -> same normalized key -> hit
    bool ok = ph.recallExact("u_ph1", "work", "how do i fix the build", resp);
    ASSERT_TRUE(ok);
    ASSERT_EQ(resp, std::string("run cmake --preset"));
}

TEST("prompt_history: findSimilar matches on shared prompt terms") {
    Db db(phDb());
    PromptHistory ph(db);
    ph.record("u_ph2", "work", "linker error LNK1104 on icmg", "kill icmg then rebuild");
    auto hits = ph.findSimilar("u_ph2", "LNK1104 error", 5);
    ASSERT_TRUE(hits.size() >= (size_t)1);
    ASSERT_EQ(hits[0].response, std::string("kill icmg then rebuild"));
}

TEST("prompt_history: record same prompt upserts response") {
    Db db(phDb());
    PromptHistory ph(db);
    ph.record("u_ph3", "work", "deploy steps", "v1");
    ph.record("u_ph3", "work", "deploy steps", "v2");
    std::string r; ph.recallExact("u_ph3", "work", "deploy steps", r);
    ASSERT_EQ(r, std::string("v2"));
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
```

- [ ] **Step 2: Run test to verify it fails** — `& 'C:\icmg-build\build-msvc-full\icmg_test.exe' prompt_history` → FAIL (header missing).

- [ ] **Step 3: Implement**

```cpp
// src/core/prompt_history.hpp
#pragma once
// Zoned prompt->response history in the persona DB. Exact recall via normalized prompt key
// (case/space-insensitive), plus lexical find-similar (LIKE on prompt terms; FTS5 optional).
// Lets a repeated/similar prompt reuse the past solution instead of re-deriving it.
#include "db.hpp"
#include <string>
#include <vector>

namespace icmg::core {

struct QARow { std::string zone, prompt, response; long long created_at = 0; };

class PromptHistory {
public:
    explicit PromptHistory(Db& db);
    void record(const std::string& user, const std::string& zone,
                const std::string& prompt, const std::string& response);
    bool recallExact(const std::string& user, const std::string& zone,
                     const std::string& prompt, std::string& response_out);
    std::vector<QARow> findSimilar(const std::string& user, const std::string& prompt, int limit);
private:
    Db& db_;
    void ensure();
};

}  // namespace icmg::core
```

```cpp
// src/core/prompt_history.cpp
#include "prompt_history.hpp"
#include "profile_key.hpp"   // reuse slugify for the normalized prompt key
#include <sstream>

namespace icmg::core {

PromptHistory::PromptHistory(Db& db) : db_(db) { ensure(); }

void PromptHistory::ensure() {
    db_.run("CREATE TABLE IF NOT EXISTS prompt_history("
            "user_id TEXT NOT NULL, zone TEXT NOT NULL, prompt TEXT NOT NULL,"
            "response TEXT NOT NULL, prompt_key TEXT NOT NULL,"
            "created_at INTEGER NOT NULL DEFAULT (strftime('%s','now')),"
            "PRIMARY KEY(user_id, zone, prompt_key))");
    db_.run("CREATE INDEX IF NOT EXISTS ix_ph_zone ON prompt_history(user_id, zone)");
}

void PromptHistory::record(const std::string& user, const std::string& zone,
                           const std::string& prompt, const std::string& response) {
    db_.run("INSERT INTO prompt_history(user_id,zone,prompt,response,prompt_key,created_at) "
            "VALUES(?,?,?,?,?,strftime('%s','now')) "
            "ON CONFLICT(user_id,zone,prompt_key) DO UPDATE SET "
            "prompt=excluded.prompt, response=excluded.response, created_at=excluded.created_at",
            {user, normalizeZone(zone), prompt, response, slugify(prompt)});
}

bool PromptHistory::recallExact(const std::string& user, const std::string& zone,
                                const std::string& prompt, std::string& response_out) {
    bool found = false;
    db_.query("SELECT response FROM prompt_history WHERE user_id=? AND zone=? AND prompt_key=?",
              {user, normalizeZone(zone), slugify(prompt)},
              [&](const Row& r) { if (!r.empty()) { response_out = r[0]; found = true; } });
    return found;
}

std::vector<QARow> PromptHistory::findSimilar(const std::string& user, const std::string& prompt,
                                              int limit) {
    // Lexical: rank by count of shared prompt terms via OR'd LIKEs over all rows for the user.
    // Simple + deterministic; FTS5 can replace later. Build a LIKE per term (len>=3).
    std::vector<QARow> out;
    std::istringstream ss(slugify(prompt));
    std::string term, where;
    std::vector<std::string> params{user};
    for (std::string t; std::getline(ss, t, '-'); ) if (t.size() >= 3) { /* slug already split on '-' below */ }
    // slugify joins with '-', so split on '-':
    std::string slug = slugify(prompt);
    size_t pos = 0;
    while (pos < slug.size()) {
        size_t dash = slug.find('-', pos);
        std::string tok = slug.substr(pos, dash == std::string::npos ? std::string::npos : dash - pos);
        if (tok.size() >= 3) {
            if (!where.empty()) where += " OR ";
            where += "prompt LIKE ?";
            params.push_back("%" + tok + "%");
        }
        if (dash == std::string::npos) break;
        pos = dash + 1;
    }
    if (where.empty()) return out;  // no usable terms
    std::string sql = "SELECT zone,prompt,response,created_at FROM prompt_history "
                      "WHERE user_id=? AND (" + where + ") ORDER BY created_at DESC LIMIT " +
                      std::to_string(limit);
    db_.query(sql, params, [&](const Row& r) {
        if (r.size() >= 4) out.push_back({r[0], r[1], r[2], std::stoll(r[3])});
    });
    return out;
}

}  // namespace icmg::core
```

- [ ] **Step 4: Run test to verify it passes** — `& '...\icmg_test.exe' prompt_history` → PASS (3/3).

- [ ] **Step 5: Add CLI subcommands** to `profile_cmd.cpp` (`qa-add --zone Z --prompt "..." --response "..."`, `qa-find --zone Z "<prompt>"`): construct `PromptHistory` on the same `db`, call `record` / `findSimilar`, print hits.

- [ ] **Step 6: Commit**

```bash
git add src/core/prompt_history.hpp src/core/prompt_history.cpp tests/core/test_prompt_history.cpp src/cli/commands/profile_cmd.cpp
git commit -m "feat(profile-store): prompt->response history with exact + lexical similar-prompt recall, 3 TDD"
```

---

### Task 5: CMake wiring + full gate + sync

**Files:**
- Modify: `CMakeLists.txt`

- [ ] **Step 1:** Add after the governor test lines (~723):

```cmake
add_icmg_test(test_profile_key tests/core/test_profile_key.cpp)  # profile-store pure
add_icmg_test(test_profile_store tests/core/test_profile_store.cpp)  # profile-store CRUD
add_icmg_test(test_prompt_history tests/core/test_prompt_history.cpp)  # prompt->response recall
```

- [ ] **Step 2:** Reconfigure + build + full gate:

Run: `pwsh -File build.ps1 -Reconfigure -Target both` then `& 'C:\icmg-build\build-msvc-full\icmg_test.exe'` count `[FAIL]`.
Expected: 0 FAIL; PASS = prior + ~11 (4 profile_key + 4 profile_store + 3 prompt_history).

- [ ] **Step 3:** icmg sync:

```bash
icmg graph update
icmg store --topic "decisions-profile-store" "Zoned profile/skill store shipped: profile_entries in persona DB, profile_key.hpp norm + profile_store CRUD + icmg profile cmd (+optional FTS). Content-neutral capability."
icmg zone add "src/core/profile_store.hpp" --zone profile-store
icmg wflog add "profile-zone store: schema + CRUD + cmd, N TDD"
```

- [ ] **Step 4:** Commit CMake + sync.

---

## Self-Review

**Coverage:** zone/key norm (T1), schema+CRUD (T2), CLI (T3), fast search (T4 optional), wiring+gate (T5). ✓
**Placeholder scan:** `currentUserId()` + `GlobalDb` accessor flagged as adapt-at-impl (read real symbols in Task 3 Step 2). FTS optional. No silent TODOs.
**Type consistency:** `ProfileRow{zone,key,kind,content,updated_at}` + `ProfileStore` method signatures identical across T2/T3. Table name `profile_entries` consistent. Test target names match files.

**Content policy:** capability is neutral; entries are user-supplied. Healthy profiles/skills only.
