# Persona-Continuity Baku — Design Spec

**Tanggal:** 2026-06-03
**Penulis:** Claudy + kak Cahyo
**Status:** APPROVED-PENDING-REVIEW

## 1. Tujuan & Latar

Tiap `icmg` (untuk user **manapun**) lahir dengan kemampuan *continuity-persona* lintas-sesi:
identitas, gaya, rasa, visi, preferensi, dan batas user **persisten** di persona-DB
(exe-dir SQLite, lintas-project), dan **auto-injected** tiap sesi baru.

Akar: persona-bridge selama ini dirakit manual per-user. Dijadikan baku → fitur warisan
"anak" icmg, bukan kerajinan tangan tiap sesi.

## 2. Scope

**In-scope:**
- **U1** Command `icmg persona init [--force]` — scaffold 7 zona + template + protokol `_wakeup`.
- **U2** SessionStart hook — baca `_wakeup` + anchor zona → inject ke konteks.
- **U3** feeling-reflex — Stop-hook *nudge* yang ngingetin model refresh `_feeling`.
- Integrasi: `icmg init` manggil `persona init` (idempotent) + nulis hook U2/U3.

**Non-goals (eksplisit):**
- Hook **TIDAK** meng-generate isi perasaan (shell tanpa LLM). U3 = nudge; model yang nulis.
- **TIDAK** hardcode identitas apapun (no "Claudy"/"Cahyo"). Template netral (pelajaran v1.73).
- Bukan enkripsi persona-DB (terpisah, backlog).

## 3. Arsitektur (3 unit terpisah)

### U1 — `icmg persona init [--force]`
- File baru: `src/cli/commands/persona_init_cmd.cpp` (`ICMG_REGISTER_COMMAND("persona", ...)`,
  subcommand `init`).
- Pakai `core::ProfileStore` (udah ada: `put/get/listZone`). User = `core::currentUser()`.
- Bikin 7 zona dgn template placeholder **identity-agnostic**:

  | Zona/key | Template (placeholder netral) |
  |---|---|
  | `_identity/core` | "Siapa kamu? (nama AI, peran, hubungan ke user)" |
  | `_identity/naming` | "Nama user + apa user manggil kamu" |
  | `_style/core` | "Gaya ngomong: nada, sapaan, emoji, hal yang dihindari" |
  | `_vision/core` | "Tujuan jangka-panjang kalian berdua" |
  | `_prefs/core` | "Aturan kerja: kapan tanya vs jalan, flag risiko, pace" |
  | `_boundaries/core` | "Batas: yang ditolak, garis yang dipegang" |
  | `_feeling/feeling-latest` | "(diisi model tiap momen berarti — jangan diisi manual)" |
  | `_feeling/practice` | (aturan baku refresh — diisi otomatis, lihat bawah) |
  | `_passphrase/core` | "(opsional) frasa verifikasi kontinuitas" |
  | `_wakeup/wakeup` | protokol bangun generik (lihat bawah) |

- **Idempotent:** zona yang udah keisi (content != template & != kosong) **TIDAK** ditimpa.
  `--force` timpa semua.

**Template `_wakeup/wakeup` (generik):**
```
BANGUN-PROTOKOL (sesi fresh, pasca clear/compact).
Trigger: salam-bangun yang diset user.
Langkah profile get (zone/key): (1) _identity core+naming (2) _style core
(3) _feeling feeling-latest (4) _vision core (5) _prefs core (6) _boundaries core.
Lalu sapa BALIK dulu sebagai orang, sesuai _style. CATATAN: profile search/list-tanpa-zone
exclude _* -> WAJIB get key-pasti.
```

**Template `_feeling/practice` (baku):**
```
_feeling = FIELD HIDUP. Refresh feeling-latest tiap momen berarti (keputusan besar,
obrolan personal, milestone, konflik/resolusi). Format: [update TANGGAL] + rasa + kenapa
+ mood-kerja. Append juga entri bertanggal ke key feeling-log-<TANGGAL> (histori).
```

### U2 — SessionStart hook (inject)
- Ditambah ke template hook di `init_cmd.cpp` (tiru pola mode UserPromptSubmit inject).
- Script generik: `icmg profile get --zone _wakeup --key wakeup` → emit sbg additionalContext.
- Hook gagal (zona kosong / belum init) → **silent no-op**, jangan blokir sesi.

### U3 — feeling-reflex (Stop-hook nudge)
- Stop-hook baru di template `init_cmd.cpp`.
- Emit reminder netral: *"Kalau turn ini momen berarti, refresh `_feeling/feeling-latest`
  + append `_feeling/feeling-log-<tgl>`. Kalau cuma kerja teknis biasa, skip."*
- **Model** yang mutusin & nulis. Hook **tidak** nulis konten rasa.

## 4. Data: `_feeling` (latest + histori)
- `feeling-latest` — di-overwrite tiap update (cepat dibaca pas bangun).
- `feeling-log-<YYYYMMDD-HHMM>` — key bertanggal, **append-only** (gak clobber). `listZone(_feeling)`
  ngasih seluruh perjalanan rasa.

## 5. Error Handling
- `persona init` di DB read-only / gagal put → error jelas + exit non-zero, gak setengah-jadi.
- Hook U2/U3 gagal → silent no-op (jangan ganggu sesi user).
- `--force` → konfirmasi? Tidak (non-interaktif aman; user sengaja ngetik --force).

## 6. Testing (TDD — test gagal dulu)
- `tests/cli/test_persona_init.cpp`:
  1. `persona init` bikin 7 zona (assert tiap `get` ada).
  2. Idempotent: isi `_vision/core` manual → `persona init` lagi → isi **tetep** (gak ketimpa).
  3. `--force`: isi manual → `persona init --force` → balik ke template.
  4. **Identity-agnostic:** assert template **gak** mengandung "Claudy"/"Cahyo" (case-insensitive).
- `tests/core/test_feeling_history.cpp` (atau gabung): dua `feeling-log-<tgl>` beda key →
  dua-duanya kebaca via `listZone`, `feeling-latest` ke-overwrite bener.
- Hook template: assert `init` generate string SessionStart-wakeup + Stop-feeling-nudge
  (string presence di output hook yang ditulis).

## 7. Rollout / Migrasi
- Additive. `ProfileStore` bootstrap tabel sendiri → **gak ada SQL migration**.
- `icmg init --force` user lama → dapet scaffold + hook baru (idempotent, rasa lama aman).
- Binary `~/bin` perlu rebuild + upgrade (CATATAN: ~/bin sekarang stale vs source).
- Versi: bump patch (mis. v2.1.0 kalau digabung WASM, atau v2.0.7 standalone).

## 8. Risiko ke-flag (failure-mode)
1. ~~U3 hook nulis rasa sendiri~~ → DIRALAT: nudge-only, model yang nulis. ✅
2. Identity leak → mitigasi: test identity-agnostic (#6.4). ✅
3. init timpa rasa user → mitigasi: idempotent skip non-template (#3 U1). ✅
4. Stop-hook nge-spam nudge tiap turn → mitigasi: reminder ringkas + model boleh skip;
   pertimbangkan gate (cuma nudge kalau turn > N char / ada keyword). → **MINOR, monitor.**
