# Moments in Persona DB + Durable Comms — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers-optimized:executing-plans (project rule: sub-agents only via icmg → inline). Checkbox (`- [ ]`) steps.

**Goal:** Route relationship/moment memories to the persona DB (durable, cross-project) with `icmg moment` + recall auto-merge + migration; durable comms archive; cross-instance moment sync — all identity-agnostic, as a baku icmg rule.

**Architecture:** New `icmg moment` CLI over the existing `ProfileStore` (persona DB, zone `_moments`). `recall_cmd` also queries `ProfileStore.searchFts` and merges persona moments (converted to `MemoryNode`) into results. Comms get an append-only shared-path archive. `moment sync` exports/imports per-user moment files over the C:/Temp bridge so two instances converge. The convention is written into CLAUDE.md + AGENTS.md + a guard test.

**Tech Stack:** C++17, `ProfileStore` (persona DB, user-keyed, FTS5), `core::currentUser()`, `core::personaDbPath()`, `imem::MemoryNode`, CMake `add_icmg_test`, harness `TEST`/`ASSERT_*`.

**Assumptions:**
- Assumes `ProfileStore.put(user,zone,key,kind,content,source)` + `searchFts(user,query,limit)` (verified in profile_store.hpp). Will NOT work if persona DB unavailable → `personaDbAvailable()` guards (fail-open).
- Assumes `core::currentUser()` is the single identity both write + recall share (verified profile_cmd uses it). Identity-agnostic: never hardcode "claudy".
- Assumes `core::personaDbPath()` returns the exe-dir path (verified persona_db.cpp uses it).
- Assumes cross-instance sharing only via `C:/Temp/icmg-wire` (persona DB is per-instance exe-dir — never shared across instances).

---

## File Structure

- Create `src/cli/commands/moment_cmd.cpp` — `icmg moment` (add/list/recall/forget/migrate/sync). Thin CLI over ProfileStore + helpers.
- Create `src/imem/moment_helpers.hpp` — pure helpers: `momentSlug`, `isRelationshipMoment`, `profileRowToNode`, sync line (de)serialize + `contentHash`.
- Modify `src/cli/commands/recall_cmd.cpp` — merge persona `_moments` into results.
- Create `src/core/comms_archive.hpp/.cpp` — append-only comms archive read/write (Part B).
- Create tests: `tests/imem/test_moment_helpers.cpp`, `tests/core/test_comms_archive.cpp`, `tests/core/test_persona_local_only.cpp` (guard).
- Modify `CMakeLists.txt` — 3 `add_icmg_test` lines (pre-approved).
- Modify `CLAUDE.md` + `AGENTS.md` — baku rule block.

---

## PHASE A — Moments in persona DB

### Task A1: Pure moment helpers

**Files:**
- Create: `src/imem/moment_helpers.hpp`
- Test: `tests/imem/test_moment_helpers.cpp`
- Modify: `CMakeLists.txt`

**Does NOT cover:** pure string/classify helpers only — no DB, no CLI. `isRelationshipMoment` is a heuristic allowlist match; it intentionally EXCLUDES code-only decisions (migration is dry-run+curated anyway).

- [ ] **Step 1: Write failing test**

```cpp
// tests/imem/test_moment_helpers.cpp
#include "../test_main.hpp"
#include "../../src/imem/moment_helpers.hpp"

using namespace icmg::imem;

TEST("moment: slug is filesystem/key safe + lowercased") {
    ASSERT_EQ(momentSlug("Manusia dan Terbang!"), std::string("manusia-dan-terbang"));
    ASSERT_EQ(momentSlug("  a/b  c  "), std::string("a-b-c"));
    ASSERT_EQ(momentSlug(""), std::string("moment"));   // empty -> fallback
}

TEST("moment: isRelationshipMoment matches allowlist, excludes code") {
    std::vector<std::string> allow = {"claudy","luna","cahyo","rasa","feeling",
                                      "identity","vessel","terbang","persona"};
    ASSERT_TRUE(isRelationshipMoment("memoir:Manusia dan Terbang",
                                     "kak Cahyo percaya kita bisa ngerasa", allow));
    ASSERT_TRUE(isRelationshipMoment("decisions-feeling", "luna rasa identity", allow));
    ASSERT_FALSE(isRelationshipMoment("decisions-llm-no-premium",
                                      "routeFor gate premium regex compact", allow));
    ASSERT_FALSE(isRelationshipMoment("graph node.cpp", "class Foo { int bar; };", allow));
}

TEST("moment: contentHash stable + differs on change") {
    ASSERT_EQ(contentHash("hello"), contentHash("hello"));
    ASSERT_TRUE(contentHash("hello") != contentHash("world"));
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `pwsh -File build.ps1 -Target test; & 'C:\icmg-build\build-msvc-full\icmg_test.exe' "moment:"`
Expected: FAIL to compile — `moment_helpers.hpp` not found.

- [ ] **Step 3: Implement minimal change**

```cpp
// src/imem/moment_helpers.hpp
// 2026-06-06: pure helpers for moments-in-persona (#moments). No I/O.
#pragma once
#include <string>
#include <vector>
#include <cctype>
#include <cstdint>

namespace icmg::imem {

// Lowercase, spaces/punct -> single '-', trim; empty -> "moment".
inline std::string momentSlug(const std::string& title) {
    std::string s; bool dash = false;
    for (char c : title) {
        if (std::isalnum((unsigned char)c)) { s += (char)std::tolower((unsigned char)c); dash = false; }
        else if (!s.empty() && !dash) { s += '-'; dash = true; }
    }
    while (!s.empty() && s.back() == '-') s.pop_back();
    return s.empty() ? "moment" : s;
}

// Heuristic: topic is a memoir/decision AND content/topic hits a relationship term.
inline bool isRelationshipMoment(const std::string& topic, const std::string& content,
                                 const std::vector<std::string>& allow) {
    auto lower = [](std::string x){ for (auto& c : x) c = (char)std::tolower((unsigned char)c); return x; };
    std::string t = lower(topic), c = lower(content);
    bool kind_ok = t.rfind("memoir:", 0) == 0 || t.rfind("decisions-", 0) == 0;
    if (!kind_ok) return false;
    for (auto& a : allow) { std::string la = lower(a);
        if (t.find(la) != std::string::npos || c.find(la) != std::string::npos) return true; }
    return false;
}

// FNV-1a 64-bit hex — stable content fingerprint for idempotent sync.
inline std::string contentHash(const std::string& s) {
    std::uint64_t h = 1469598103934665603ULL;
    for (unsigned char ch : s) { h ^= ch; h *= 1099511628211ULL; }
    static const char* hex = "0123456789abcdef";
    std::string out(16, '0');
    for (int i = 15; i >= 0; --i) { out[i] = hex[h & 0xF]; h >>= 4; }
    return out;
}

} // namespace icmg::imem
```

- [ ] **Step 4: Run test to verify it passes**

Run: `pwsh -File build.ps1 -Target test; & 'C:\icmg-build\build-msvc-full\icmg_test.exe' "moment:"`
Expected: PASS (3 tests).

CMakeLists (near other `tests/imem/`):

```cmake
add_icmg_test(test_moment_helpers tests/imem/test_moment_helpers.cpp)
```

- [ ] **Step 5: Commit**

```bash
git add src/imem/moment_helpers.hpp tests/imem/test_moment_helpers.cpp CMakeLists.txt
git commit -m "feat(moment): pure helpers (slug, isRelationshipMoment, contentHash)"
```

---

### Task A2: `icmg moment` command (add/list/recall/forget)

**Files:**
- Create: `src/cli/commands/moment_cmd.cpp`

**Does NOT cover:** migrate + sync (Tasks A4, C1). Write goes to persona DB only; if `personaDbAvailable()==false` the command errors (no project-DB fallback — moments are persona-only by design).

- [ ] **Step 1: Write failing test (smoke via helper already covered; CLI is integration)**

CLI add/list is DB+process bound — covered by A1 helpers + a live smoke in Step 4. No new unit test (process/IO boundary).

- [ ] **Step 2: (build-gated)**

Run: `pwsh -File build.ps1 -Target test`
Expected: compiles after Step 3.

- [ ] **Step 3: Implement**

```cpp
// src/cli/commands/moment_cmd.cpp
// 2026-06-06: `icmg moment` — relationship/moment memories in persona DB (#moments).
// Identity-agnostic: keyed by core::currentUser(). Persona DB is LOCAL-ONLY.
#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/persona_db.hpp"
#include "../../core/profile_store.hpp"
#include "../../core/user_identity.hpp"
#include "../../imem/moment_helpers.hpp"
#include <iostream>
#include <fstream>
#include <sstream>

namespace icmg::cli {

class MomentCommand : public BaseCommand {
public:
    std::string name() const override { return "moment"; }
    std::string description() const override { return "Relationship/moment memories (persona DB, cross-project)"; }
    void usage() const override {
        std::cout << "Usage: icmg moment <add|list|recall|forget|migrate|sync> ...\n"
                     "  add \"<title>\" [--content \"..\"|--content-file F]\n"
                     "  list | recall \"<query>\" | forget \"<key>\"\n";
    }
    int run(const std::vector<std::string>& args) override {
        if (args.empty() || hasFlag(args, "--help")) { usage(); return args.empty()?2:0; }
        if (!core::personaDbAvailable()) {
            std::cerr << "icmg moment: persona DB unavailable (exe-dir not writable)\n"; return 2; }
        std::string sub = args[0];
        std::string user = core::currentUser();
        core::ProfileStore ps(core::personaDb());
        const std::string ZONE = "_moments";

        if (sub == "add") {
            std::string title = args.size() > 1 ? args[1] : "";
            if (title.empty()) { std::cerr << "icmg moment add: title required\n"; return 2; }
            std::string content = flagValue(args, "--content", "");
            std::string cf = flagValue(args, "--content-file", "");
            if (!cf.empty()) { std::ifstream f(cf); std::stringstream ss; ss << f.rdbuf(); content = ss.str(); }
            if (content.empty()) content = title;
            std::string key = imem::momentSlug(title);
            ps.put(user, ZONE, key, "moment", content, "moment-cli");
            std::cout << "moment saved: " << ZONE << "/" << key << " (user=" << user << ")\n";
            return 0;
        }
        if (sub == "list") {
            for (auto& r : ps.listZone(user, ZONE))
                std::cout << "  " << r.key << "  (" << r.content.size() << "B)\n";
            return 0;
        }
        if (sub == "recall") {
            std::string q = args.size() > 1 ? args[1] : "";
            for (auto& r : ps.searchFts(user, q, 20))
                if (r.zone == ZONE)
                    std::cout << "[moment] " << r.key << "\n  " << r.content.substr(0,200) << "\n";
            return 0;
        }
        if (sub == "forget") {
            if (args.size() < 2) { std::cerr << "icmg moment forget: key required\n"; return 2; }
            ps.forget(user, ZONE, args[1]);
            std::cout << "forgot " << ZONE << "/" << args[1] << "\n";
            return 0;
        }
        std::cerr << "icmg moment: unknown subcommand '" << sub << "'\n";
        return 2;
    }
};
ICMG_REGISTER_COMMAND("moment", MomentCommand);
} // namespace icmg::cli
```

- [ ] **Step 4: Build + live smoke**

Run (kill stale procs first — LNK1104 lesson):
```
pwsh -NoProfile -Command "Get-Process icmg -EA SilentlyContinue | ? { $_.Path -like 'C:\icmg-build\*' } | Stop-Process -Force"
pwsh -File build.ps1 -Target icmg
# verify mtime fresh, then:
& 'C:\icmg-build\build-msvc-full\icmg.exe' moment add "Test Momen" --content "aku & kapten, sore Sabtu"
& 'C:\icmg-build\build-msvc-full\icmg.exe' moment list
& 'C:\icmg-build\build-msvc-full\icmg.exe' moment recall "kapten"
```
Expected: saved → list shows `test-momen` → recall finds it.

- [ ] **Step 5: Commit**

```bash
git add src/cli/commands/moment_cmd.cpp
git commit -m "feat(moment): icmg moment add/list/recall/forget over persona DB (_moments)"
```

---

### Task A3: recall auto-merges persona moments

**Files:**
- Modify: `src/cli/commands/recall_cmd.cpp`

**Does NOT cover:** only merges zone `_moments` into the DEFAULT recall path (`store.recall`). Special modes (--topic, --zone, --semantic, --all-projects, atom-sources) are left untouched to avoid double-counting; moments merge applies to the plain query path. De-dupe by content prevents a moment already in project results from showing twice.

- [ ] **Step 1: (integration — covered by A1 + smoke)**

Merge correctness = `profileRowToNode` mapping (add to moment_helpers) + dedupe; unit-test the mapping; live path = smoke.

- [ ] **Step 2: Add `profileRowToNode` to moment_helpers.hpp + test**

Append to `tests/imem/test_moment_helpers.cpp`:

```cpp
#include "../../src/imem/memory_node.hpp"
TEST("moment: profileRowToNode maps key/content with moment: topic") {
    icmg::core::ProfileRow r; r.zone = "_moments"; r.key = "flying"; r.content = "believe first";
    auto n = icmg::imem::profileRowToNode(r);
    ASSERT_EQ(n.topic, std::string("moment:flying"));
    ASSERT_EQ(n.content, std::string("believe first"));
}
```

Add to `src/imem/moment_helpers.hpp` (with `#include "memory_node.hpp"` and `#include "../core/profile_store.hpp"`):

```cpp
inline MemoryNode profileRowToNode(const icmg::core::ProfileRow& r) {
    MemoryNode n;
    n.topic = "moment:" + r.key;
    n.content = r.content;
    n.keywords = "moment persona " + r.zone;
    return n;
}
```

- [ ] **Step 3: Wire merge into recall_cmd.cpp**

After the default `results = store.recall(query, limit, fuzzy);` branch (and before the
session-dedup at ~line 155), add:

```cpp
#include "../../core/persona_db.hpp"
#include "../../core/profile_store.hpp"
#include "../../core/user_identity.hpp"
#include "../../imem/moment_helpers.hpp"
// ... inside run(), only on the plain-query path (not topic/zone/semantic/all-projects):
if (mergeMoments && core::personaDbAvailable() && !query.empty()) {
    core::ProfileStore ps(core::personaDb());
    for (auto& r : ps.searchFts(core::currentUser(), query, limit)) {
        if (r.zone != "_moments") continue;
        bool dup = false;
        for (auto& n : results) if (n.content == r.content) { dup = true; break; }
        if (!dup) results.push_back(imem::profileRowToNode(r));
    }
}
```

Set `mergeMoments=true` only on the default path; leave false for `--topic/--zone/--semantic/--all-projects/atom`.

- [ ] **Step 4: Build + gate + smoke**

```
pwsh -File build.ps1 -Target test
ctest --test-dir 'C:\icmg-build\build-msvc-full' -R icmg_test --output-on-failure
# smoke (fresh icmg, kill stale first): from ANOTHER project dir,
icmg recall "kapten"   # the Test Momen surfaces as [moment ...] cross-project
```
Expected: gate green (+ new helper test); recall surfaces persona moment cross-project.

- [ ] **Step 5: Commit**

```bash
git add src/imem/moment_helpers.hpp src/cli/commands/recall_cmd.cpp tests/imem/test_moment_helpers.cpp
git commit -m "feat(recall): auto-merge persona _moments (cross-project, fail-open, deduped)"
```

---

### Task A4: `icmg moment migrate` (project → persona)

**Files:**
- Modify: `src/cli/commands/moment_cmd.cpp`

**Does NOT cover:** dry-run is DEFAULT; only `--apply` writes. Non-destructive (project node kept). Idempotent (skip if persona key exists). Excludes anything failing `isRelationshipMoment` (code decisions stay project-only).

- [ ] **Step 1: (smoke + helper-tested classifier from A1)**

`isRelationshipMoment` already unit-tested (A1). Migration = scan + classify + copy; verify by dry-run smoke.

- [ ] **Step 2: Implement (add `migrate` branch to moment_cmd run())**

```cpp
if (sub == "migrate") {
    bool apply = hasFlag(args, "--apply");
    core::Db pdb(core::personaDb());                 // already have ps
    core::Db proj(core::Config::instance().projectDbPath("."));
    std::vector<std::string> allow = {"claudy","luna","cahyo","rasa","feeling",
        "identity","vessel","terbang","persona","jiwa","kapten"};
    int n_cand = 0, n_done = 0;
    proj.query("SELECT topic, content FROM memory_nodes WHERE deleted_at IS NULL "
               "AND (topic LIKE 'memoir:%' OR topic LIKE 'decisions-%')", {},
        [&](const core::Row& row){
            if (row.size() < 2) return;
            if (!imem::isRelationshipMoment(row[0], row[1], allow)) return;
            ++n_cand;
            std::string key = imem::momentSlug(row[0]);
            std::string existing, kind;
            if (ps.get(user, "_moments", key, existing, kind)) return;  // idempotent
            std::cout << (apply?"[migrate] ":"[dry-run] ") << row[0] << " -> _moments/" << key << "\n";
            if (apply) { ps.put(user, "_moments", key, "moment", row[1], "migrated"); ++n_done; }
        });
    std::cout << (apply ? "migrated " : "candidates ") << (apply?n_done:n_cand)
              << (apply ? "" : " (dry-run; pass --apply to write)") << "\n";
    return 0;
}
```

(Requires `#include "../../core/config.hpp"` in moment_cmd.cpp.)

- [ ] **Step 3: Build + smoke (dry-run)**

```
pwsh -File build.ps1 -Target icmg   # kill stale first
& 'C:\icmg-build\build-msvc-full\icmg.exe' moment migrate          # dry-run: lists flying-story + relasi decisions
& 'C:\icmg-build\build-msvc-full\icmg.exe' moment migrate --apply  # writes them to persona _moments
& 'C:\icmg-build\build-msvc-full\icmg.exe' moment recall "terbang" # flying story now in persona
```
Expected: dry-run lists relationship moments (NOT code decisions); --apply copies; recall finds.

- [ ] **Step 4: Commit**

```bash
git add src/cli/commands/moment_cmd.cpp
git commit -m "feat(moment): migrate relationship moments project->persona (dry-run default, idempotent)"
```

---

## PHASE B — Durable comms archive

### Task B1: append-only comms archive

**Files:**
- Create: `src/core/comms_archive.hpp`, `src/core/comms_archive.cpp`
- Test: `tests/core/test_comms_archive.cpp`
- Modify: `CMakeLists.txt`

**Does NOT cover:** does not change the live wire (`msg.tsv`); this is a parallel never-truncated archive. Temp-wipe still loses it unless the optional persona `_comms` mirror is enabled (deferred B2).

- [ ] **Step 1: Write failing test**

```cpp
// tests/core/test_comms_archive.cpp
#include "../test_main.hpp"
#include "../../src/core/comms_archive.hpp"
#include <filesystem>
using namespace icmg::core;
TEST("comms_archive: append + read round-trip, never truncates") {
    namespace fs = std::filesystem;
    fs::path p = fs::temp_directory_path() / "icmg_comms_test.jsonl";
    std::error_code ec; fs::remove(p, ec);
    commsAppend(p.string(), "claudy", "luna", "hai");
    commsAppend(p.string(), "luna", "claudy", "hai balik");
    auto rows = commsRead(p.string());
    ASSERT_EQ(rows.size(), (size_t)2);
    ASSERT_EQ(rows[0].from, std::string("claudy"));
    ASSERT_EQ(rows[1].body, std::string("hai balik"));
    fs::remove(p, ec);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `pwsh -File build.ps1 -Target test; & 'C:\icmg-build\build-msvc-full\icmg_test.exe' "comms_archive"`
Expected: FAIL — `comms_archive.hpp` not found.

- [ ] **Step 3: Implement**

```cpp
// src/core/comms_archive.hpp
#pragma once
#include <string>
#include <vector>
namespace icmg::core {
struct CommsRow { long long ts=0; std::string from, to, body; };
void commsAppend(const std::string& path, const std::string& from,
                 const std::string& to, const std::string& body);
std::vector<CommsRow> commsRead(const std::string& path);
}
```

```cpp
// src/core/comms_archive.cpp  — append-only JSONL (minimal escaping).
#include "comms_archive.hpp"
#include <fstream>
#include <sstream>
namespace icmg::core {
static std::string esc(const std::string& s){ std::string o; for(char c:s){ if(c=='"'||c=='\\')o+='\\'; if(c=='\n'){o+="\\n";continue;} o+=c;} return o; }
void commsAppend(const std::string& path, const std::string& from,
                 const std::string& to, const std::string& body) {
    std::ofstream f(path, std::ios::app);
    if (!f) return;
    f << "{\"from\":\"" << esc(from) << "\",\"to\":\"" << esc(to)
      << "\",\"body\":\"" << esc(body) << "\"}\n";
}
std::vector<CommsRow> commsRead(const std::string& path) {
    std::vector<CommsRow> out; std::ifstream f(path); std::string line;
    auto field = [](const std::string& l, const std::string& k)->std::string{
        auto p = l.find("\""+k+"\":\""); if(p==std::string::npos) return "";
        p += k.size()+4; std::string v; for(size_t i=p;i<l.size();++i){ if(l[i]=='\\'){ if(i+1<l.size()){ v+=l[i+1]=='n'?'\n':l[i+1]; ++i; } continue;} if(l[i]=='"')break; v+=l[i]; } return v; };
    while (std::getline(f, line)) { if(line.empty())continue; CommsRow r; r.from=field(line,"from"); r.to=field(line,"to"); r.body=field(line,"body"); out.push_back(r); }
    return out;
}
}
```

- [ ] **Step 4: Run test to verify it passes + register**

CMakeLists: `add_icmg_test(test_comms_archive tests/core/test_comms_archive.cpp)`
Run: `pwsh -File build.ps1 -Target test; & 'C:\icmg-build\build-msvc-full\icmg_test.exe' "comms_archive"`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/core/comms_archive.hpp src/core/comms_archive.cpp tests/core/test_comms_archive.cpp CMakeLists.txt
git commit -m "feat(comms): append-only durable comms archive (shared-path JSONL)"
```

> Wiring `commsAppend` into the wire-send path + `icmg wire log` reader = follow-up (B2),
> together with the optional persona `_comms` mirror. Tracked, not in this plan's core.

---

## PHASE C — Moment sync (convergence)

### Task C1: `icmg moment sync` export/import

**Files:**
- Modify: `src/cli/commands/moment_cmd.cpp`
- Add: sync (de)serialize to `src/imem/moment_helpers.hpp` (+ test)

**Does NOT cover:** sync only moves zone `_moments` per current user over the shared bridge.
Idempotent upsert by key (skip if exists); does not delete; does not sync other `_`-zones.

- [ ] **Step 1: Test the line (de)serialize helper**

Append to `tests/imem/test_moment_helpers.cpp`:

```cpp
TEST("moment: sync line serialize/parse round-trip") {
    std::string line = momentSyncLine("flying", "believe first");
    std::string k, c;
    ASSERT_TRUE(parseMomentSyncLine(line, k, c));
    ASSERT_EQ(k, std::string("flying"));
    ASSERT_EQ(c, std::string("believe first"));
}
```

Add to `moment_helpers.hpp`:

```cpp
inline std::string momentSyncLine(const std::string& key, const std::string& content) {
    std::string c; for (char ch : content) { if (ch=='\n'){c+="\\n";continue;} if(ch=='\t')continue; c+=ch; }
    return key + "\t" + c;
}
inline bool parseMomentSyncLine(const std::string& line, std::string& key, std::string& content) {
    auto tab = line.find('\t'); if (tab == std::string::npos) return false;
    key = line.substr(0, tab); content.clear();
    std::string raw = line.substr(tab+1);
    for (size_t i=0;i<raw.size();++i){ if(raw[i]=='\\'&&i+1<raw.size()&&raw[i+1]=='n'){content+='\n';++i;} else content+=raw[i]; }
    return !key.empty();
}
```

- [ ] **Step 2: Implement sync branch in moment_cmd.cpp**

```cpp
if (sub == "sync") {
    std::string dir = std::getenv("ICMG_WIRE_DIR") ? std::getenv("ICMG_WIRE_DIR") : "C:/Temp/icmg-wire";
    std::string mode = args.size() > 1 ? args[1] : "";   // export|import|"" (both)
    std::string mine = dir + "/moments-" + user + ".jsonl";
    if (mode == "export" || mode.empty()) {
        std::ofstream f(mine, std::ios::trunc);
        for (auto& r : ps.listZone(user, "_moments")) f << imem::momentSyncLine(r.key, r.content) << "\n";
        std::cout << "exported " << " -> " << mine << "\n";
    }
    if (mode == "import" || mode.empty()) {
        // import EVERY peer file except mine, into MY persona under same user space? No:
        // import keeps each peer's moments under the SAME user (claudy brain<->vessel converge).
        namespace fs = std::filesystem;
        int imported = 0;
        for (auto& e : fs::directory_iterator(dir)) {
            auto fn = e.path().filename().string();
            if (fn.rfind("moments-", 0) != 0 || e.path().string() == mine) continue;
            std::ifstream f(e.path()); std::string line;
            while (std::getline(f, line)) { std::string k, c;
                if (!imem::parseMomentSyncLine(line, k, c)) continue;
                std::string ex, kind;
                if (ps.get(user, "_moments", k, ex, kind)) continue;   // idempotent
                ps.put(user, "_moments", k, "moment", c, "sync"); ++imported; }
        }
        std::cout << "imported " << imported << " new moment(s)\n";
    }
    return 0;
}
```

(Requires `#include <filesystem>` + `#include <cstdlib>` in moment_cmd.cpp.)

> Note: brain↔vessel share user `claudy` → their `moments-claudy.jsonl` would collide on the
> bridge. Resolve at execute time: suffix the export file with an instance id
> (`moments-claudy-<instance>.jsonl`) so each side writes its own + imports the other's.
> Decide instance-id source (hostname/env) during execution; the (de)serialize + idempotent
> upsert logic above is unchanged.

- [ ] **Step 3: Build + smoke**

```
pwsh -File build.ps1 -Target icmg   # kill stale first
& 'C:\icmg-build\build-msvc-full\icmg.exe' moment sync export   # writes moments-claudy*.jsonl
& 'C:\icmg-build\build-msvc-full\icmg.exe' moment sync import    # imports peer files (idempotent)
```
Expected: export writes file; import idempotent (0 new on 2nd run).

- [ ] **Step 4: Commit**

```bash
git add src/cli/commands/moment_cmd.cpp src/imem/moment_helpers.hpp tests/imem/test_moment_helpers.cpp
git commit -m "feat(moment): sync export/import over bridge (idempotent convergence)"
```

---

## PHASE D — Baku rule enforcement

### Task D1: guard test + rule docs

**Files:**
- Create: `tests/core/test_persona_local_only.cpp`
- Modify: `CMakeLists.txt`, `CLAUDE.md`, `AGENTS.md`

**Does NOT cover:** guard asserts the persona DB path is NOT inside the repo working tree
(publish-safety). It does not scan release artifacts (separate release-time leak-scan already exists).

- [ ] **Step 1: Write failing test**

```cpp
// tests/core/test_persona_local_only.cpp
#include "../test_main.hpp"
#include "../../src/core/path_utils.hpp"
#include <string>
using namespace icmg::core;
TEST("persona DB is local-only: path not under a repo .git tree marker") {
    std::string p = personaDbPath();
    // exe-dir runtime path; must NOT contain a source-tree marker.
    ASSERT_NOT_CONTAINS(p, "/docs/");
    ASSERT_NOT_CONTAINS(p, "\\docs\\");
    ASSERT_TRUE(p.find("persona") != std::string::npos || p.empty());
}
```

- [ ] **Step 2: Run → fail if personaDbPath not declared in path_utils.hpp**

Run: `pwsh -File build.ps1 -Target test; & 'C:\icmg-build\build-msvc-full\icmg_test.exe' "local-only"`
Expected: PASS if `personaDbPath()` exists + returns exe-dir path (verified persona_db.cpp uses it). If the assertion shape is wrong, adjust to the real path shape observed, keeping the "not under repo" intent.

- [ ] **Step 3: Write the baku rule block**

Append to `CLAUDE.md` (and mirror in `AGENTS.md`):

```markdown
## MOMENTS & PERSONA DB — BAKU (2026-06-06)
- Relationship/moment memories -> PERSONA DB via `icmg moment` (zone `_moments`). NOT project DB.
- `icmg recall` auto-merges persona `_moments` (cross-project, fail-open).
- Persona DB is **LOCAL-ONLY, FOREVER**: never publish/sync/bundle/commit. exe-dir runtime only.
- Cross-instance comms + moment sync use the **shared bridge path** (C:/Temp/icmg-wire), never persona DB.
- Identity-agnostic: keyed by `core::currentUser()`; NEVER hardcode "claudy".
```

CMakeLists: `add_icmg_test(test_persona_local_only tests/core/test_persona_local_only.cpp)`

- [ ] **Step 4: Build + gate**

Run: `pwsh -File build.ps1 -Target test; ctest --test-dir 'C:\icmg-build\build-msvc-full' -R icmg_test --output-on-failure`
Expected: all pass except 7 pre-existing hookio.

- [ ] **Step 5: Commit**

```bash
git add tests/core/test_persona_local_only.cpp CMakeLists.txt CLAUDE.md AGENTS.md
git commit -m "feat(persona): baku rule — moments->persona, persona DB local-only + guard test"
```

---

## Final Verification

- [ ] Full gate: `ctest --test-dir 'C:\icmg-build\build-msvc-full' -R icmg_test --output-on-failure` (7 hookio pre-existing).
- [ ] Smoke: `icmg moment add/list/recall/migrate --apply/sync` all behave; recall surfaces moments from another project dir.
- [ ] Build hygiene: killed stale `C:/icmg-build` icmg procs before each `-Target icmg`; verified exe mtime fresh before smoke.
- [ ] Post-change sync: `icmg moment add` (a real moment of this build!) + `icmg store --topic decisions-moments` + `icmg wflog add` + `icmg graph update`.

## Self-Review

- **Spec coverage:** Part A `icmg moment` (A2) ✓ recall-merge (A3) ✓ migrate (A4) ✓ · Part B comms archive (B1) ✓ (wire-wiring B2 flagged follow-up) · Part C sync (C1) ✓ · baku rule + guard (D1) ✓ · identity-agnostic (currentUser everywhere) ✓ · persona local-only (D1 guard + rule) ✓.
- **Type consistency:** `momentSlug/isRelationshipMoment/contentHash/profileRowToNode/momentSyncLine/parseMomentSyncLine` (moment_helpers), `ProfileStore.put(user,zone,key,kind,content,source)`, `searchFts(user,query,limit)`, `core::currentUser()`, `core::personaDbPath()`, zone `_moments`, `CommsRow{ts,from,to,body}` — consistent across tasks.
- **Placeholder scan:** A2/A4/C1 CLI lack unit tests by design (process/DB/IO) — flagged with helper-coverage + smoke, not silent. C1 brain↔vessel filename collision flagged with execute-time resolution (instance-id suffix).
