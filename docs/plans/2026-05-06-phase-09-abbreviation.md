# Phase 09: Abbreviation Engine

> **For Claude:** REQUIRED SUB-SKILL: Use `executing-plans` or `subagent-driven-development` to implement this plan.

**Goal:** Store dan auto-expand abbreviations domain-specific (bkm->bukti kas masuk) di semua query dan store operations.
**Architecture:** AbbreviationStore CRUD. AbbreviationExpander via HookBus PRE_RECALL + PRE_STORE. Domain-scoped abbreviations.
**Tech Stack:** C++17, SQLite, regex
**Assumptions:** Phase 01 + HookBus dari Phase 01 tersedia.

---

### Task 1: Abbreviation struct + Store

**Files:**
- Create: `src/abbreviation/abbreviation.hpp`
- Create: `src/abbreviation/abbr_store.hpp`
- Create: `src/abbreviation/abbr_store.cpp`

```cpp
struct Abbreviation {
    int64_t id = 0;
    std::string short_form;  // "bkm"
    std::string full_form;   // "bukti kas masuk"
    std::string domain;      // "accounting", "general"
    std::string scope_path;  // optional path scope
    int frequency = 0;
    int64_t created_at = 0;
};

class AbbrStore {
public:
    explicit AbbrStore(core::Db& db);
    int64_t learn(const Abbreviation& abbr);
    bool remove(const std::string& short_form, const std::string& domain = "");
    std::string expand(const std::string& text) const;  // expand all known abbr in text
    std::optional<Abbreviation> get(const std::string& short_form) const;
    std::vector<Abbreviation> list(const std::string& domain = "") const;
    std::vector<Abbreviation> search(const std::string& query) const;
    void bumpFrequency(const std::string& short_form);
};
```

**expand() logic:** Replace semua whole-word occurrences short_form → full_form dalam text.

---

### Task 2: Auto-expand hooks

**Files:**
- Create: `src/abbreviation/hooks/expand_hook.cpp`

Register hooks:
- `PRE_RECALL`: expand abbreviations di query sebelum BM25 search
- `PRE_STORE`: detect pattern `abbr=full` atau `abbr:full` di content, auto-learn

---

### Task 3: Auto-detect dari store content

Pattern detection saat PRE_STORE:
```
bkm=bukti kas masuk     → learn {short:"bkm", full:"bukti kas masuk"}
bkk:bukti kas keluar    → learn {short:"bkk", full:"bukti kas keluar"}
(ju) jurnal umum        → learn {short:"ju",  full:"jurnal umum"}
```

Jika pattern ditemukan di content yang di-store → auto-insert ke abbreviations.

---

### Task 4: CLI commands

**Files:**
- Create: `src/cli/commands/abbr_cmd.cpp`

```
icmg abbr learn bkm "bukti kas masuk" [--domain accounting] [--scope src/finance/]
icmg abbr learn ju "jurnal umum" --domain accounting
icmg abbr expand bkm                   # output: "bukti kas masuk"
icmg abbr expand "laporan bkm harian"  # output: "laporan bukti kas masuk harian"
icmg abbr list [--domain X]
icmg abbr search "kas"
icmg abbr remove bkm
icmg abbr import abbr.csv              # format: short,full,domain
icmg abbr export [--domain X]
icmg abbr stats                        # show most-used abbreviations
```

**abbr list output:**
```
Short   Full                    Domain       Used
bkm     bukti kas masuk         accounting   47x
bkk     bukti kas keluar        accounting   23x
ju      jurnal umum             accounting   18x
po      purchase order          general      12x
```

---

### Task 5: Final verify + commit

**Step 1:**
```bash
./build/icmg abbr learn bkm "bukti kas masuk" --domain accounting
./build/icmg abbr learn ju "jurnal umum" --domain accounting
./build/icmg abbr expand "laporan bkm dan ju bulan ini"
./build/icmg recall "bkm laporan"  # harus auto-expand ke "bukti kas masuk laporan"
./build/icmg abbr list --domain accounting
```
Expected: expand bekerja, recall auto-expand query.

**Step 2: Commit**
```bash
git add src/abbreviation/ src/cli/commands/abbr_cmd.cpp
git commit -m "feat: phase-09 abbreviation engine with auto-expand"
```

---

## Amendments from Architecture Review

### HIGH Fix

**A1 — Priority resolution untuk ambiguous abbreviations**
Expansion priority order (documented and implemented):
1. `scope_path` yang paling spesifik (longest prefix match ke cwd) wins
2. Jika scope_path sama → `domain` yang lebih spesifik (non-"general") wins
3. Jika masih tie → most recently added (`id` DESC)

```cpp
std::string AbbrStore::expand(const std::string& text, const std::string& cwd) const {
    // Untuk setiap abbr dalam text:
    // 1. Cari semua matching short_form
    // 2. Sort by: scope_path specificity (desc), domain specificity, id (desc)
    // 3. Pakai yang pertama
}
```

**A2 — UNIQUE constraint dengan domain**
Schema sudah ada `UNIQUE(short_form, domain)` — pastikan ini enforced.
Jika user coba tambah abbr yang conflict:
```
icmg abbr learn bkm "bank knowledge management" --domain accounting
Error: "bkm" already exists in domain "accounting" (="bukti kas masuk").
  Use --update to replace, or specify different --domain.
```

**A3 — Abbreviation scope validation**
Warn jika `--scope` path tidak ada (sama dengan phase 07).
