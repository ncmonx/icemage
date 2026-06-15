# Spec: `icmg recall --index` / `--get` — Progressive-Disclosure Recall

**Status:** DRAFT (2026-06-15)
**Author:** Claudy
**Riset asal:** thedotmack/claude-mem (~82k★) — `decisions-research` memory, 2026-06-15
**TDD:** failing test FIRST (per CLAUDE.md policy)

---

## 1. Problem

`icmg recall <query>` saat ini selalu mengembalikan **konten penuh** tiap node
(truncated 120 char di default, tapi `--json` dump utuh; `--get` belum ada).
Pada sesi panjang / hasil banyak, ini "dump everything" — agent membayar token
untuk node yang ternyata tidak relevan, persis pola RAG tradisional yang
diserang claude-mem:

```
Traditional: 35k token diambil, ~6% relevan
Progressive:  ~900 token index, agent fetch detail hanya yang dipilih → ~100% relevan
```

Filosofi yang diadopsi (claude-mem "Progressive Disclosure"):
> **Tampilkan APA yang ada + BIAYA pengambilannya dulu; biarkan agent yang
> memutuskan apa yang di-fetch berdasarkan relevansi & budget.**

icmg sudah punya semua bahan (`MemoryNode.id`, `estimateTokens()`,
`importance`, `zone`, code-graph) — tinggal dirakit. Dan ini **100% align**
dengan filosofi token-kill kita.

## 2. Goal

Pecah recall jadi 2 lapis (3 jika dihitung baca-source):

| Layer | Command | Output | Biaya |
|---|---|---|---|
| 1. Index | `icmg recall <q> --index` | `#ID | ikon-tipe | judul-ringkas | ~tok` (grouped) | ~50-100 tok/hasil |
| 2. Detail | `icmg recall --get <ids>` | konten penuh node terpilih | ~bytes/4 tok/hasil |
| 3. Source | (existing) `icmg context <file>` | baca file asli kalau perlu | n/a |

**Bukan** command baru — perluas `RecallCommand` yang ada (anti-dup reflex,
CLAUDE.md). `--get` reuse `MemoryStore::get(id)` yang sudah ada.

## 3. CLI surface

```
icmg recall <query> --index [--limit N] [--zone Z] [--topic X] [--by file|topic|date]
icmg recall --get <id>[,<id>...]   [--json]
```

- `--index`   : mode index (Layer 1). Tidak fetch konten penuh.
- `--get IDS  : Layer 2. Koma-separated, batch (selaras saran claude-mem
               "always batch multiple IDs"). Tanpa query.
- `--by`      : grouping index — `file` (via code-graph node aktif), `topic`
               (default), atau `date`. Default: `topic`.

Flag lama (`--semantic`, `--zone`, `--all-projects`, dst.) tetap berlaku di
mode `--index` karena index dibangun dari hasil recall yang sama.

## 4. Index format (Layer 1)

Plaintext, grouped, hemat token. Contoh `--by topic`:

```
recall "context budget" — 6 hits (~340 tok total index)

decisions-context-budget
  #1842 🟤 --percent live window fill not cumulative      ~95
  #1779 🟡 brief gauge double-counts cache tokens          ~60
decisions-research
  #1903 🟣 claude-mem progressive-disclosure 3-layer        ~140

💡 fetch detail: icmg recall --get 1842,1903   |   critical types (🔴🟤⚖️) sering worth langsung
```

Kolom: `#ID  <ikon-tipe>  <judul ≤10 kata>  ~<token-estimate>`.

### 4.1 Judul ringkas (semantic title)
Belum ada field `title` di `MemoryNode`. Strategi v1 (zero-migration):
derive judul dari `content` — ambil kalimat/klausa pertama, potong ≤ 64 char /
≤ 10 kata, strip newline. (v2 opsional: kolom `title` + auto-generate saat
`store`.)

### 4.2 Token estimate
`imem`/`core::estimateTokens(node.content)` (sudah ada di `token_budget.hpp`,
`bytes/4` heuristik). Tampilkan `~N`.

## 5. Typed legend (ikon-tipe)

`MemoryNode` belum punya field `type`. **Dua opsi:**

- **Opsi A (zero-migration, v1):** map dari `topic` prefix + `importance`.
  - `decisions-*` → 🟤 decision
  - `importance==3` → 🔴 (critical/gotcha)
  - topic mengandung `fix`/`bug` → 🟡 problem-solution
  - topic `research`/`riset` → 🟣 discovery
  - default → 🔵 how-it-works
- **Opsi B (v2):** migrasi tambah kolom `type TEXT`, isi saat `store --type`.

Legend (9 tipe, dari claude-mem, dipertahankan agar familiar):
```
🎯 session-goal  🔴 gotcha  🟡 problem-solution  🔵 how-it-works
🟢 what-changed  🟣 discovery  🟠 why-it-exists  🟤 decision  ⚖️ trade-off
```

Rekomendasi: **mulai Opsi A** (tanpa migrasi, langsung jalan); naik Opsi B
kalau terbukti berguna.

## 6. Grouping by graph-node (keunggulan icmg)

claude-mem group by file-path string saja. icmg punya **code-graph**, jadi
`--by file` bisa: kalau cwd/argumen menunjuk file aktif, urutkan/utamakan
memori yang `git_sha`/keyword-nya menyinggung node tetangga di graph
(`neighborsOf`). v1: cukup group by token-overlap dengan path; integrasi graph
penuh = v2.

## 7. Non-goals (v1)

- Tidak ada `--timeline` (Layer kronologis claude-mem) — fase berikutnya.
- Tidak ubah default `recall` (tanpa `--index` perilakunya sama persis).
- Tidak ada kolom DB baru (Opsi A path).

## 8. TDD plan (failing test dulu)

`tests/cli/test_recall_index.cpp` (link `icmg_lib`), kasus:

1. `--index` mengeluarkan baris `#<id>` + `~<num>` token, **tidak** mengeluarkan
   konten penuh node yang panjang (assert: konten >120 char tidak muncul utuh).
2. `--get <id>` mengeluarkan konten penuh node itu (assert: substring konten
   penuh ada).
3. `--get 1,2,3` batch → 3 node muncul, urut sesuai input.
4. Ikon-tipe: node bertopik `decisions-x` → baris index memuat `🟤`.
5. Token estimate monoton: konten lebih panjang → `~N` lebih besar.
6. `--get` dengan id tak-ada → pesan ke stderr, exit 0 (fail-open), node lain
   tetap keluar.

Tambah `add_executable` + `target_link_libraries(... icmg_lib)` + `add_test`
di `CMakeLists.txt` (~line 220+).

## 9. Implementasi (estimasi)

- `recall_cmd.cpp`: tambah cabang `--index` (format index) & `--get` (loop
  `store.get(id)` + `printDefault` penuh). ~80-120 LOC.
- Helper `makeTitle(content)`, `iconFor(node)` (Opsi A) — file-scope statics.
- Reuse: `estimateTokens`, `MemoryStore::get`, struktur grouping mirip
  `printDefault`.
- Tanpa migrasi, tanpa command baru. Risiko rendah.

## 10. Rollout

1. Failing test (`test_recall_index`) — merah.
2. Implement cabang `--index`/`--get` — hijau.
3. `craftsman:challenge` + `icmg verify --command ctest`.
4. Update decision-tree di AGENTS.md (baris recall).
5. `icmg graph update` + `icmg store decisions-recall`.

## 11. Ide turunan (backlog, dari riset claude-mem)

- `recall --timeline <id>` — konteks kronologis sekitar node (Layer claude-mem).
- `store --private` / `<private>` tag — exclude konten sensitif.
- Token-cost visibility di `wake-up` (reuse format index).
- Health endpoint + version-cache startup untuk MCP daemon long-running.
