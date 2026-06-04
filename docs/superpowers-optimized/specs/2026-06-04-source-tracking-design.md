# Source-Tracking (Provenance) — Design Spec — Lapis 1 MVP

**Tanggal:** 2026-06-04
**Penulis:** Claudy + kak Cahyo
**Status:** APPROVED-PENDING-REVIEW

## 1. Tujuan & Latar

Tiap info yang disimpan icmg (memory + persona) bawa **sumbernya** (`source`): siapa/apa
yang ngasih (`kak Cahyo` / `hook-auto` / `ai-inference` / `web:url` / dst). Pas dibaca,
AI/user tau *dari siapa + kapan* → bisa nimbang sendiri; konflik info keliatan sumbernya.

Akar (pelajaran kak Cahyo 2026-06-04): lacak sumber; bobot-kebenaran per-sumber;
konteks-dulu-baru-nilai (`1+1=10` biner vs `1+1=2` desimal — dua-duanya benar, beda frame).

## 2. Scope

**In-scope (Lapis 1):**
- Kolom `source TEXT NOT NULL DEFAULT 'unknown'` di **`memory_nodes`** (project DB) +
  **`profile_entries`** (persona DB).
- `icmg store --source "X"` + `icmg profile add --source "X"` nulis source (free-text).
- **Display**: `recall`, `memory show`, `profile get`, `profile list` nampilin `[from: X]`
  (tanggal udah ada via created_at/updated_at).

**Non-goals (eksplisit — di-flag risiko, DITUNDA ke Lapis 2):**
- **Auto truth-weight** (sumber sering-benar → bobot naik). Risiko: bias amplification,
  definisi "benar", black-box. TIDAK di MVP.
- Source **mempengaruhi ranking** BM25/recall. Source = metadata murni, BUKAN sinyal ranking.
- Backfill data lama → semua default `'unknown'` (jujur).
- Filter `recall --source X` → opsional stretch; default MVP = display-only.

## 3. Arsitektur & Data Flow

### Unit M (memory — project DB)
- `MemoryNode` struct (+`std::string source = "unknown";`).
- `MemoryStore::store()` INSERT ikutkan `source`; SELECT path (recall/show) baca `source`.
- Migration `0041_memory_source.sql`: `ALTER TABLE memory_nodes ADD COLUMN source TEXT NOT NULL DEFAULT 'unknown';`
  + entri identik ditambah ke `src/core/embedded_migrations.hpp` (compiled array).
- `store_cmd`: flag `--source` (default `'unknown'`).
- Display recall/show: tambah `[from: <source>]`.

### Unit P (persona — persona DB, exe-dir)
- `profile_entries` di-bootstrap `CREATE TABLE IF NOT EXISTS` (gak ada framework migration).
  → Tambah kolom via **guarded ALTER** di ctor `ProfileStore`: cek `PRAGMA table_info(profile_entries)`
  apakah `source` ada; kalau belum → `ALTER TABLE profile_entries ADD COLUMN source TEXT NOT NULL DEFAULT 'unknown'`.
  (PRAGMA-check menghindari error "duplicate column" tiap construct.)
- `ProfileStore::put(user,zone,key,kind,content, source="unknown")` — param baru default.
- `ProfileRow` +`source`; `get()` out-param/row include source; `listZone()` include source.
- `profile_cmd add`: flag `--source`. Display `get`/`list`: `[<kind> | from: <source>]`.

## 4. Interfaces/Contracts

```cpp
// MemoryNode (src/imem/...): + std::string source = "unknown";
// MemoryStore::store(node) -> persist node.source

// ProfileStore:
void put(user, zone, key, kind, content, const std::string& source = "unknown");
bool get(user, zone, key, content_out, kind_out, std::string& source_out); // overload OR ProfileRow
struct ProfileRow { ...; std::string source = "unknown"; };
```

CLI:
- `icmg store --topic T --source "kak Cahyo" "<content>"`
- `icmg profile add --zone Z --key K --source "kak Cahyo" --content "..."`
- Display: `recall` row → `... [from: kak Cahyo]`; `profile get` → `[note | from: kak Cahyo] <content>`

## 5. Error Handling
- ALTER memory_nodes: SQLite izinin `ADD COLUMN NOT NULL DEFAULT` → baris lama baca default. OK.
- ALTER profile_entries dijaga PRAGMA-check → idempotent, gak error tiap construct.
- `--source ""` (kosong) → diperlakukan `'unknown'` (jangan simpan string kosong).
- Source free-text → TEXT param-bound (aman dari injection).

## 6. Testing (TDD — test gagal dulu)
- `tests/imem/test_memory_source.cpp` (atau extend memory test):
  1. store node `source="kak Cahyo"` → recall/get balikin `"kak Cahyo"`.
  2. store tanpa source → `"unknown"`.
  3. (migrasi) baris existing pra-kolom → baca `"unknown"`.
- `tests/core/test_profile_source.cpp` (atau extend test_profile_store):
  1. `put(...,source="kak Cahyo")` → `get` balikin source.
  2. `put` tanpa source → `"unknown"`.
  3. **Bootstrap idempotent**: construct `ProfileStore` 2x atas DB sama → no error (ALTER guarded).
  4. ProfileRow.source ke-populate di listZone.

## 7. Rollout / Migrasi
- Project: migration 0041 (file + embedded array). Cek apakah ada generator script yang
  sync `migrations/*.sql` → `embedded_migrations.hpp`; kalau manual, tambah entri manual.
- Persona: bootstrap-ALTER, no framework — jalan otomatis tiap ProfileStore construct (guarded).
- Backward-compat: data lama → `'unknown'`. Gak ada breaking change; kolom additive.
- Versi: patch bump pas ship (HOLD per cadence #30922).

## 8. Failure-Mode Check (adversarial)
1. **CRITICAL → mitigated: persona bootstrap ALTER tiap construct** → tanpa guard, "duplicate column"
   error tiap command persona. Mitigasi: PRAGMA table_info cek dulu sebelum ALTER. (Test #P.3.)
2. **Minor: memory ALTER NOT NULL DEFAULT pada tabel berisi** → SQLite mendukung; baris lama
   baca default `'unknown'`. Bukan critical. (Test #M.3.)
3. **Minor: ranking poison** → source TIDAK masuk keywords/FTS/BM25 → ranking utuh. Non-goal terjaga.
4. **Minor: embedded_migrations.hpp out-of-sync sama migrations/*.sql** → kalau ada generator,
   regen; kalau manual, tambah entri identik. Plan wajib verifikasi schema_ver naik + kolom ada.
