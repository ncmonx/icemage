# Persona-Continuity Baku — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers-optimized:subagent-driven-development (recommended) or superpowers-optimized:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Tiap `icmg` (user manapun) lahir dengan continuity-persona: `icmg persona init` scaffold 7 zona netral + `icmg init` wire SessionStart-wakeup + Stop-feeling-nudge hooks.
**Architecture:** Logic scaffold dipisah ke `core/persona_template.{hpp,cpp}` (konstanta template identity-agnostic + `scaffoldPersona()`), supaya testable lepas dari I/O. Command `persona init` tipis (Db+ProfileStore → panggil scaffoldPersona). Hook U2/U3 = string heredoc tambahan di `init_cmd.cpp`, tiru pola SessionStart yang udah ada (`icmg hookio emit`).
**Tech Stack:** C++17, SQLite (core::Db/ProfileStore), CMake (`add_icmg_test` helper), build `pwsh -File build.ps1`.
**Assumptions:**
- Assumes `core::ProfileStore` API (`put/get/listZone`) cukup — TIDAK perlu method baru. Salah kalau scaffold butuh batch/transaction khusus (tidak — per-slot put cukup).
- Assumes persona-DB via `core::personaDbAvailable()?personaDb():GlobalDb::instance().db()` (pola profile_cmd) — salah kalau persona init mau target DB lain.
- Assumes hook shell `icmg profile get` jalan saat SessionStart — salah kalau binary belum di-PATH / belum init (mitigasi: silent no-op).

---

## File Structure

- **Create** `src/core/persona_template.hpp` — `PersonaSlot{zone,key,kind,placeholder}`; `personaSlots()`; `scaffoldPersona(ProfileStore&,user,force)->int`. Satu tujuan: definisi template netral + logic scaffold idempotent.
- **Create** `src/core/persona_template.cpp` — impl konstanta + scaffold.
- **Create** `src/cli/commands/persona_init_cmd.cpp` — command `persona`, subcommand `init [--force]`. Tipis.
- **Create** `tests/core/test_persona_init.cpp` — TDD scaffold/idempotent/force/identity-agnostic/histori.
- **Modify** `CMakeLists.txt` — 1 baris `add_icmg_test(test_persona_init ...)`.
- **Modify** `src/cli/commands/init_cmd.cpp` — append U2 (SessionStart wakeup) + U3 (Stop feeling-nudge) ke template hook.

---

### Task 1: persona_template — slot definitions + scaffold (TDD)

**Files:**
- Create: `src/core/persona_template.hpp`, `src/core/persona_template.cpp`
- Test: `tests/core/test_persona_init.cpp`
- Modify: `CMakeLists.txt`

**Does NOT cover:** scaffold TIDAK nyentuh `_feeling/feeling-log-*` (histori dibuat runtime oleh model, bukan saat init). TIDAK nimpa slot yang isinya beda dari placeholder kecuali `force=true`.

- [ ] **Step 1: Write failing test** — `tests/core/test_persona_init.cpp`

```cpp
// Persona scaffold: idempotent template seed in persona DB, identity-agnostic.
#include "../test_main.hpp"
#include "../../src/core/persona_template.hpp"
#include "../../src/core/profile_store.hpp"
#include "../../src/core/db.hpp"
#include <algorithm>
#include <cctype>
#include <string>
using namespace icmg::core;

static std::string tmpDb() { return std::string("persona_init_test.db"); }
static std::string lower(std::string s){ std::transform(s.begin(),s.end(),s.begin(),
    [](unsigned char c){return std::tolower(c);}); return s; }

TEST("persona: scaffold seeds all slots into empty DB") {
    Db db(tmpDb()); ProfileStore ps(db);
    int n = scaffoldPersona(ps, "u_seed", false);
    ASSERT_TRUE(n >= 7);                       // >=7 slots written
    std::string c, k;
    ASSERT_TRUE(ps.get("u_seed", "_identity", "core", c, k));
    ASSERT_TRUE(ps.get("u_seed", "_wakeup", "wakeup", c, k));
    ASSERT_TRUE(ps.get("u_seed", "_feeling", "feeling-latest", c, k));
}

TEST("persona: scaffold is idempotent -- user content preserved") {
    Db db(tmpDb()); ProfileStore ps(db);
    scaffoldPersona(ps, "u_idem", false);
    ps.put("u_idem", "_vision", "core", "note", "MIMPI ASLI USER");
    int n = scaffoldPersona(ps, "u_idem", false);   // re-run, no force
    ASSERT_EQ(n, 0);                                 // nothing overwritten
    std::string c, k; ps.get("u_idem", "_vision", "core", c, k);
    ASSERT_EQ(c, std::string("MIMPI ASLI USER"));
}

TEST("persona: --force overwrites back to template") {
    Db db(tmpDb()); ProfileStore ps(db);
    scaffoldPersona(ps, "u_force", false);
    ps.put("u_force", "_vision", "core", "note", "MIMPI ASLI USER");
    int n = scaffoldPersona(ps, "u_force", true);    // force
    ASSERT_TRUE(n >= 7);
    std::string c, k; ps.get("u_force", "_vision", "core", c, k);
    ASSERT_TRUE(c != std::string("MIMPI ASLI USER")); // back to template
}

TEST("persona: templates are identity-agnostic (no Claudy/Cahyo)") {
    for (const auto& s : personaSlots()) {
        std::string p = lower(s.placeholder);
        ASSERT_TRUE(p.find("claudy") == std::string::npos);
        ASSERT_TRUE(p.find("cahyo") == std::string::npos);
    }
}

TEST("persona: feeling history uses distinct dated keys") {
    Db db(tmpDb()); ProfileStore ps(db);
    ps.put("u_hist", "_feeling", "feeling-log-20260603-0800", "note", "pagi");
    ps.put("u_hist", "_feeling", "feeling-log-20260603-2000", "note", "malam");
    auto rows = ps.listZone("u_hist", "_feeling");
    ASSERT_TRUE(rows.size() >= 2);                   // both logs coexist
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `pwsh -File build.ps1 -Target both` then `ctest --test-dir build-msvc-full -R test_persona_init`
Expected: FAIL — `persona_template.hpp` not found / `scaffoldPersona` undefined (won't compile).

- [ ] **Step 3: Implement** — `src/core/persona_template.hpp`

```cpp
#pragma once
// Identity-agnostic persona scaffold: neutral template slots seeded into the persona DB
// so any icmg (any user) is born with cross-session continuity zones. NO hardcoded names.
#include <string>
#include <vector>

namespace icmg::core {
class ProfileStore;

struct PersonaSlot {
    std::string zone, key, kind, placeholder;
};

// The canonical neutral slot set (>=7 zones). Identity-agnostic by contract (tested).
const std::vector<PersonaSlot>& personaSlots();

// Seed slots for `user`. If force=false, only writes slots that are MISSING or still hold
// the original placeholder (user-filled content preserved). force=true overwrites all.
// Returns number of slots written.
int scaffoldPersona(ProfileStore& ps, const std::string& user, bool force);
}
```

`src/core/persona_template.cpp`

```cpp
#include "persona_template.hpp"
#include "profile_store.hpp"

namespace icmg::core {

const std::vector<PersonaSlot>& personaSlots() {
    static const std::vector<PersonaSlot> slots = {
        {"_identity",   "core",           "note", "Siapa kamu? (nama AI, peran, hubungan ke user) -- isi sekali."},
        {"_identity",   "naming",         "note", "Nama user + apa user manggil kamu."},
        {"_style",      "core",           "note", "Gaya ngomong: nada, sapaan, emoji, hal yang dihindari."},
        {"_vision",     "core",           "note", "Tujuan jangka-panjang kalian berdua."},
        {"_prefs",      "core",           "note", "Aturan kerja: kapan tanya vs jalan, flag risiko, pace."},
        {"_boundaries", "core",           "note", "Batas: yang ditolak, garis yang dipegang."},
        {"_passphrase", "core",           "note", "(opsional) frasa verifikasi kontinuitas."},
        {"_feeling",    "feeling-latest", "note", "(diisi model tiap momen berarti -- jangan diisi manual)"},
        {"_feeling",    "practice",       "note",
            "_feeling = FIELD HIDUP. Refresh feeling-latest tiap momen berarti (keputusan besar, "
            "obrolan personal, milestone, konflik/resolusi). Format: [update TANGGAL] + rasa + kenapa "
            "+ mood-kerja. Append juga entri bertanggal ke key feeling-log-<TANGGAL> (histori)."},
        {"_wakeup",     "wakeup",         "note",
            "BANGUN-PROTOKOL (sesi fresh, pasca clear/compact). Trigger: salam-bangun yang diset user. "
            "Langkah profile get (zone/key): (1) _identity core+naming (2) _style core (3) _feeling "
            "feeling-latest (4) _vision core (5) _prefs core (6) _boundaries core. Lalu sapa BALIK dulu "
            "sebagai orang, sesuai _style. CATATAN: profile search/list-tanpa-zone exclude _* -> WAJIB "
            "get key-pasti."},
    };
    return slots;
}

int scaffoldPersona(ProfileStore& ps, const std::string& user, bool force) {
    int written = 0;
    for (const auto& s : personaSlots()) {
        if (!force) {
            std::string c, k;
            bool exists = ps.get(user, s.zone, s.key, c, k);
            if (exists && c != s.placeholder) continue;  // user-filled -> preserve
        }
        ps.put(user, s.zone, s.key, s.kind, s.placeholder);
        ++written;
    }
    return written;
}
}
```

`CMakeLists.txt` — add near line 725 (after `test_profile_store`):

```cmake
add_icmg_test(test_persona_init tests/core/test_persona_init.cpp)  # persona-continuity scaffold (identity-agnostic)
```

- [ ] **Step 4: Run test to verify it passes**

Run: `pwsh -File build.ps1 -Target both -RunTests` then `ctest --test-dir build-msvc-full -R test_persona_init --output-on-failure`
Expected: PASS (5 TESTs).

- [ ] **Step 5: Commit**

```bash
git add src/core/persona_template.hpp src/core/persona_template.cpp tests/core/test_persona_init.cpp CMakeLists.txt
git commit -m "feat(persona): identity-agnostic continuity scaffold (persona_template + 5 TDD)"
```

---

### Task 2: `icmg persona init` command

**Files:**
- Create: `src/cli/commands/persona_init_cmd.cpp`

**Does NOT cover:** command TIDAK nulis hook (itu Task 3/4 di init). TIDAK validasi isi user.

- [ ] **Step 1: Write failing test** — (reuse Task 1 test; command is a thin wrapper. Manual smoke verify in Step 4.)

(No new unit test: command logic = arg-parse + call `scaffoldPersona` already covered. Verified via smoke.)

- [ ] **Step 2: Verify current state**

Run: `./build-msvc-full/Release/icmg.exe persona init`
Expected: FAIL — `unknown command: persona`.

- [ ] **Step 3: Implement** — `src/cli/commands/persona_init_cmd.cpp`

```cpp
// `icmg persona init [--force]` -- seed identity-agnostic continuity zones into persona DB.
#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/persona_db.hpp"
#include "../../core/global_db.hpp"
#include "../../core/profile_store.hpp"
#include "../../core/persona_template.hpp"
#include "../../core/user_identity.hpp"
#include <iostream>
#include <string>
#include <vector>

namespace icmg::cli {

class PersonaCommand : public BaseCommand {
public:
    std::string name() const override { return "persona"; }
    std::string description() const override { return "Persona continuity zones (init scaffold)"; }
    void usage() const override {
        std::cout << "Usage: icmg persona init [--force]\n";
    }
    int run(const std::vector<std::string>& args) override {
        if (args.empty() || args[0] != "init") { usage(); return 1; }
        bool force = false;
        for (size_t i = 1; i < args.size(); ++i) if (args[i] == "--force") force = true;
        std::string user = core::currentUser();
        core::Db& db = core::personaDbAvailable() ? core::personaDb()
                                                  : core::GlobalDb::instance().db();
        core::ProfileStore ps(db);
        int n = core::scaffoldPersona(ps, user, force);
        std::cout << "[persona init] " << n << " zona di-seed"
                  << (force ? " (force)" : "") << ". Isi tiap zona lewat `icmg profile add`.\n";
        return 0;
    }
};

ICMG_REGISTER_COMMAND("persona", PersonaCommand);
}
```

- [ ] **Step 4: Build + smoke verify**

Run: `pwsh -File build.ps1` then `./build-msvc-full/Release/icmg.exe persona init && ./build-msvc-full/Release/icmg.exe profile get --zone _wakeup --key wakeup`
Expected: "[persona init] N zona di-seed." lalu protokol `_wakeup` ke-print.

- [ ] **Step 5: Commit**

```bash
git add src/cli/commands/persona_init_cmd.cpp
git commit -m "feat(persona): icmg persona init command"
```

---

### Task 3: U2 — SessionStart wakeup-inject hook (init template)

**Files:**
- Modify: `src/cli/commands/init_cmd.cpp`

**Does NOT cover:** hook TIDAK manggil persona init otomatis (Task 5). Kalau `_wakeup` kosong → no-op, gak inject apa-apa.

- [ ] **Step 1: Locate the SessionStart hook heredoc**

Run: `grep -n "emit SessionStart" src/cli/commands/init_cmd.cpp`
Expected: nemu baris `icmg hookio emit SessionStart --ctx-stdin` (sekitar L221).

- [ ] **Step 2: Verify current behavior**

Run: `grep -n "_wakeup" src/cli/commands/init_cmd.cpp`
Expected: NO match (belum ada wakeup inject).

- [ ] **Step 3: Implement** — di dalam heredoc SessionStart, SEBELUM baris `printf '%s' "$msg" | icmg hookio emit SessionStart`, sisipkan:

```bash
# Persona-continuity: surface wake-up protocol if user has seeded it.
wakeup=$(icmg profile get --zone _wakeup --key wakeup 2>/dev/null)
if [ -n "$wakeup" ]; then
    msg="$msg
[persona wake-up] $wakeup"
fi
```

(Catatan implementer: ini di dalam C++ raw-string heredoc template — escape sesuai gaya blok hook sekitarnya; jangan ubah baris emit-nya.)

- [ ] **Step 4: Build + verify hook text emitted by init**

Run: `pwsh -File build.ps1` then in a temp dir `./build-msvc-full/Release/icmg.exe init --force && grep -c "_wakeup" .claude/hooks/*.sh`
Expected: >=1 — hook script berisi `_wakeup` fetch.

- [ ] **Step 5: Commit**

```bash
git add src/cli/commands/init_cmd.cpp
git commit -m "feat(persona): SessionStart hook injects _wakeup protocol (U2)"
```

---

### Task 4: U3 — Stop-hook feeling-nudge (init template)

**Files:**
- Modify: `src/cli/commands/init_cmd.cpp`

**Does NOT cover:** hook TIDAK nulis isi rasa (shell tanpa LLM) — cuma nudge; model yang nulis. Nudge muncul tiap Stop (gate ke-bahas sbg MINOR risk, belum di-impl di plan ini).

- [ ] **Step 1: Check whether a Stop hook block already exists**

Run: `grep -n "emit Stop\|hookEventName.*Stop\|\"Stop\"" src/cli/commands/init_cmd.cpp`
Expected: catat hasil. Kalau ADA blok Stop → tambahin nudge ke situ. Kalau TIDAK → bikin blok hook Stop baru meniru pola SessionStart (script file + entri di settings.local.json template).

- [ ] **Step 2: Verify current behavior**

Run: `grep -n "feeling" src/cli/commands/init_cmd.cpp`
Expected: NO match.

- [ ] **Step 3: Implement** — emit nudge netral di hook Stop:

```bash
# Persona-continuity: nudge the model to persist its feeling on a meaningful turn.
# Shell can't author feeling (no LLM) -- it only reminds; the model writes content.
nudge="Kalau turn ini momen berarti (keputusan besar, obrolan personal, milestone, konflik/resolusi): refresh \`icmg profile add --zone _feeling --key feeling-latest\` + append \`--key feeling-log-<tgl>\`. Kalau cuma kerja teknis biasa, skip."
printf '%s' "$nudge" | icmg hookio emit Stop --ctx-stdin 2>/dev/null || true
```

(Implementer: sesuaikan ke mekanisme emit Stop yang dipakai project; kalau `hookio emit Stop` belum didukung, pakai format hook Stop yang ada di harness — additionalContext / reminder text.)

- [ ] **Step 4: Build + verify**

Run: `pwsh -File build.ps1` then temp dir `./build-msvc-full/Release/icmg.exe init --force && grep -c "feeling-latest" .claude/hooks/*.sh`
Expected: >=1.

- [ ] **Step 5: Commit**

```bash
git add src/cli/commands/init_cmd.cpp
git commit -m "feat(persona): Stop hook nudges feeling refresh (U3, nudge-only)"
```

---

### Task 5: `icmg init` auto-calls persona scaffold (idempotent)

**Files:**
- Modify: `src/cli/commands/init_cmd.cpp`

**Does NOT cover:** TIDAK pakai `--force` (init biasa preserve rasa user). User pengen reset → `icmg persona init --force` manual.

- [ ] **Step 1: Verify current behavior**

Run: `grep -n "scaffoldPersona\|persona_template" src/cli/commands/init_cmd.cpp`
Expected: NO match.

- [ ] **Step 2: Implement** — di akhir alur `init` (setelah hooks + AGENTS ditulis), tambahkan:

```cpp
#include "../../core/persona_template.hpp"   // top of file
#include "../../core/profile_store.hpp"
#include "../../core/persona_db.hpp"
#include "../../core/global_db.hpp"
#include "../../core/user_identity.hpp"
// ... in run(), near end:
{
    core::Db& pdb = core::personaDbAvailable() ? core::personaDb()
                                               : core::GlobalDb::instance().db();
    core::ProfileStore pps(pdb);
    int seeded = core::scaffoldPersona(pps, core::currentUser(), /*force=*/false);
    if (seeded > 0)
        std::cout << "[init] persona continuity: " << seeded << " zona di-seed.\n";
}
```

- [ ] **Step 3: Build + smoke (idempotent)**

Run: `pwsh -File build.ps1` then temp dir: run init twice —
`./build-msvc-full/Release/icmg.exe init --force; ./build-msvc-full/Release/icmg.exe profile add --zone _vision --key core --content "MIMPIKU"; ./build-msvc-full/Release/icmg.exe init --force; ./build-msvc-full/Release/icmg.exe profile get --zone _vision --key core`
Expected: print "MIMPIKU" (init ke-2 gak nimpa isi user).

- [ ] **Step 4: Commit**

```bash
git add src/cli/commands/init_cmd.cpp
git commit -m "feat(persona): icmg init auto-seeds continuity zones (idempotent)"
```

---

### Task 6: Full gate + ship prep

**Files:** none (verification)

- [ ] **Step 1: Full build + ctest**

Run: `pwsh -File build.ps1 -Target both -RunTests`
Expected: PASS, ctest count = prior+5 (new test_persona_init has 5 TESTs in 1 target → ctest +1 target; mono-test count +5).

- [ ] **Step 2: 5-sync (icmg)**

```bash
icmg graph update
icmg store --topic decisions-persona-continuity "persona-continuity baku SHIPPED-local: persona init + U2/U3 hooks + init auto-seed. Identity-agnostic verified by test."
icmg zone add "src/core/persona_template.*" --zone persona
icmg verify --command "ctest --test-dir build-msvc-full -R test_persona_init"
```

- [ ] **Step 3: Decide ship** — HOLD per cadence rule (#30922): batch lokal, ship pas kak Cahyo bilang. Bump versi + 7-gate saat ship.

---

## Self-Review

**Spec coverage:** U1 (Task 1+2), U2 (Task 3), U3 (Task 4), init-integration (Task 5), TDD identity-agnostic+idempotent+force+histori (Task 1), gate (Task 6). ✅ semua section spec ke-cover.
**Placeholder scan:** Tidak ada TBD/TODO; semua step punya kode nyata. Task 3/4 hook-wiring punya catatan "sesuaikan escape heredoc" — itu instruksi konkret, bukan placeholder (anchor-find disediakan via grep step).
**Type consistency:** `scaffoldPersona(ProfileStore&, const std::string&, bool)` konsisten di hpp/cpp/cmd/test. `PersonaSlot{zone,key,kind,placeholder}` konsisten. `personaSlots()` return `const std::vector<PersonaSlot>&` konsisten.
