# ICM-Graph — Comprehensive Upgrade Plan

> Hasil eksplorasi mendalam terhadap source code + riset eksternal untuk peningkatan performa dan fitur.

---

## 📋 Ringkasan Isi

1. [Arsitektur — 8 Modul Utama](#1-arsitektur)
2. [Fitur Baru — 18 Kandidat](#2-fitur-baru)
3. [C++ Optimization — Build, Execute, Startup](#3-cpp-optimization)
4. [Security Audit — 5 Celah](#4-security-audit)
5. [Prioritized Roadmap](#5-roadmap)

---

## 1. Arsitektur

### 1.1 Core (Tulang Punggung)

| File | Fungsi |
|---|---|
| `db.cpp` | SQLite database backbone — semua data tersimpan di sini |
| `global_db.cpp` | Koneksi database antar project |
| `migrator.cpp` | Mengelola 37 migrasi dari skema awal hingga saat ini |

**37 Migrasi — evolusi bertahap:**

| Migrasi | Fungsi |
|---|---|
| `0001_initial_schema.sql` | Skema pertama |
| `0007_symbol_nodes.sql` | Node simbol untuk kode |
| `0010_embeddings.sql` | Vector embeddings |
| `0013_compression_glossary.sql` | Glosari kompresi |
| `0018_user_identity.sql` | Identitas pengguna |
| `0025_context_nodes.sql` | Node konteks |
| `0029_focus_chain.sql` | Rantai fokus per sesi |
| `0035_style_patterns.sql` | Pola gaya penulisan |
| `0037_write_compressions.sql` | Kompresi output |

### 1.2 Reflex & Cache

| File | Fungsi |
|---|---|
| `hook_bus.cpp` | Event-driven hooks — trigger otomatis |
| `turn_cache.cpp` | Cache giliran terakhir |
| `query_cache.cpp` | Cache query berulang |
| `prefetch_cache.cpp` | Prediksi kebutuhan selanjutnya |
| `intent_cache.cpp` | Cache intensi pengguna |
| `tool_call_cache.cpp` | Cache tool call pattern |

### 1.3 Memory & Retrieval (imem/)

| File | Ukuran | Fungsi |
|---|---|---|
| `memory_store.cpp` | 23.8KB | Penyimpanan semua memori sesi |
| `scorer.cpp` | 7.5KB | BM25 scorer — relevansi memori |
| `focus_chain.cpp` | — | Prioritas memori per sesi |
| `skill_chunker.cpp` | — | Pembelajaran pola dari interaksi |

### 1.4 Token Compression (tkil/)

| File | Fungsi |
|---|---|
| `ultra_pipeline.cpp` | Pipeline kompresi utama (19 file di direktori) |
| `dedup_pass.cpp` | Buang konten duplikat |
| `pattern_pass.cpp` | Hafal pola berdasarkan bahasa |
| `outcome_extractor.cpp` | Ekstraksi inti output |
| `session_glossary.cpp` | Kamus kompresi per sesi |
| 20 filter bahasa | Go, Rust, Java, DotNet, Swift, Kotlin, dll |

### 1.5 Deep Compression (compress/)

| File | Ukuran | Fungsi |
|---|---|---|
| `compressor.cpp` | 11.4KB | Mesin kompresi utama |
| `glossary_store.cpp` | — | Kamus dari riwayat interaksi |
| `write_expander.cpp` | 14.1KB | Ekspansi dari format terkompresi |
| `template_engine.cpp` | — | Template output |

### 1.6 Abbreviation

| File | Fungsi |
|---|---|
| `abbr_store.cpp` | Database singkatan — efisiensi token |

### 1.7 Graph Engine

- **33.463 node**, **636.937 edge**
- BFS traversal, community detection (Leiden), symbol extraction
- 9 extractor bahasa (tree-sitter based)

### 1.8 MCP Server & Tools

- **33 MCP tools** terdaftar
- Server RPC di `src/server/`
- Command dispatcher di `cli/commands/` (145 file!)

### 1.9 Pendukung

| Modul | Fungsi |
|---|---|
| `embed/` | ONNX Runtime — semantic embedding |
| `llm/` | Integrasi LLM — llama runner, warm pipe, smart router |
| `viz/` | Visualisasi — DOT, GEXF, GraphML |
| `daemon/` | Rule daemon client + server |
| `rules/` | Rule engine — resolver, store, hook |
| `sp/` | Stored procedures + SQL parser |
| `import/` | Importer CSV, ICM, JSON, KGraph |
| `export/` | Exporter berbagai format |
| `data/` | Data store |

---

## 2. Fitur Baru — 18 Kandidat

### 2.1 Graphify (Interactive Visualization)

| Fitur | Status | Deskripsi |
|---|---|---|
| **Interactive HTML Viz** | ❌ Belum | `icmg viz` → klik-zoom-filter D3.js |
| **Edge Confidence** | ❌ Belum | Tag EXTRACTED / INFERRED + skor |
| **God Node Report** | ❌ Belum | Node paling terhubung dalam arsitektur |
| **Incremental Cache** | ⚠️ Sebagian | SHA256 sidecar — hanya proses file berubah |
| **GRAPH_REPORT.md** | ❌ Belum | Ringkasan arsitektur otomatis |
| **Multimodal Nodes** | ❌ Belum | PDF, gambar, video sebagai node |
| **Confidence Scores** | ❌ Belum | Transparansi di setiap edge |

### 2.2 rtk-ai/ICM (Dual Memory)

| Fitur | Status | Deskripsi |
|---|---|---|
| **Dual Memory** | ❌ Belum | Episodik (meluruh) + permanen (terikat) |
| **Hybrid Search 30/70** | ❌ Belum | BM25 + Cosine — keyword + semantic |
| **Feedback Loop** | ❌ Belum | Rekam + cari dari kesalahan |
| **9 Typed Relations** | ❌ Belum | `depends_on`, `contradicts`, `superseded_by` |
| **Transcript System** | ❌ Belum | FTS5 — semua sesi terekam, bisa diputar ulang |
| **TUI Dashboard** | ❌ Belum | 5 tab interactive, vim-style |
| **5 Hooks** | ❌ Belum | start / pre / post / compact / prompt |
| **Access-aware Decay** | ❌ Belum | Semakin sering diakses, semakin awet |
| **Auto-dedup >85%** | ❌ Belum | Tanpa duplikasi |
| **Consolidation Hints** | ❌ Belum | 7+ dalam satu topik → sarankan rapi |
| **Benchmark Suite** | ❌ Belum | bench / bench-agent / bench-recall |
| **Multi-agent DB** | ❌ Belum | 17 tools, 1 database, 0 konflik |
| **Memoir Export** | ❌ Belum | json / dot / ascii / ai |

### 2.3 Eksternal — Fitur Tambahan

| Sumber | Fitur | Token Saving |
|---|---|---|
| Speakeasy | **Dynamic Toolsets** — MCP tools on-demand | 96% |
| Entroly | **Ultra Compression** — repo compact | 99% |
| Cognee | **Dual-Store** — vector + graph hybrid | — |
| Zep/Graphiti | **Temporal KG** — time-aware relations | — |
| Letta | **Tiered Memory** — hot/warm/cold | — |
| LAP | **API Spec Compilation** — 10× smaller | 90% |
| mcplens | **Semantic Code Search MCP** | 85% |
| snip/omni | **CLI Proxy Filter** — noise removal | 90% |
| sqz/tokf | **Compression CLI** — Rust-based | 90% |

---

## 3. C++ Optimization

### 3.1 Status Build Saat Ini

| Aspek | Kondisi |
|---|---|
| **Compiler** | MSVC cl (Windows), GCC 14 (Linux WSL) |
| **C++ Standard** | C++23 (REQUIRED) |
| **Build Generator** | Ninja (default), Visual Studio di `build/` |
| **PCH** | ✅ Aktif (default), 231 MB .pch, 19 header |
| **PCH MSVC** | ❌ Dimatikan — masalah kompatibilitas |
| **Unity Build** | ⚠️ Siap tapi OFF (`ICMG_UNITY_BUILD=OFF`) |
| **C++23 Modules** | ⚠️ Eksperimental, OFF default |
| **ccache** | ✅ Auto-detected di MSYS2 |
| **lld linker** | ⚠️ Opt-in, masalah dgn MinGW+ONNX |
| **ONNX / Tree-sitter / llama.cpp** | ✅ ON di MSVC |
| **Mono test** | ✅ Aktif — hemat ~2.5 menit link |
| **SQLite flags** | `THREADSAFE=1`, `WAL_SYNCHRONOUS=1`, `FTS5=1` |

### 3.2 Build Speed — Detail

#### sccache (Prioritas #1)

```powershell
winget install Mozilla.sccache
```

```cmake
cmake_policy(SET CMP0141 NEW)
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT Embedded)

find_program(SCCACHE_EXE sccache)
if(SCCACHE_EXE)
  set(CMAKE_C_COMPILER_LAUNCHER ${SCCACHE_EXE})
  set(CMAKE_CXX_COMPILER_LAUNCHER ${SCCACHE_EXE})
endif()
```

**Efek:** 30-60% build speed.
**⚠️:** Tidak support `/DEBUG:FASTLINK`, PCH + sccache perlu di-test.

#### Unity Build

Aktifkan: `-DICMG_UNITY_BUILD=ON`

| Subdirektori | File | Batch | Risiko ODR |
|---|---|---|---|
| `abbreviation/` | 4 | 1 | Rendah ✅ |
| `compress/` | 8 | 1 | Rendah ✅ |
| `daemon/` | 4 | 1 | Rendah ✅ |
| `embed/` | 8 | 1 | Rendah ✅ |
| `export/` | 2 | 1 | Rendah ✅ |
| `graph/` | 18 | 3 | Sedang ⚠️ |
| `imem/` | 10 | 2 | Sedang ⚠️ |
| `import/` | 6 | 1 | Rendah ✅ |
| `llm/` | 17 | 3 | Sedang ⚠️ |
| `mcp/` + `mcp/tools/` | 37 | 5 | Tinggi 🔴 |
| `rules/` | 6 | 1 | Rendah ✅ |
| `server/` | 4 | 1 | Rendah ✅ |
| `sp/` | 5 | 1 | Rendah ✅ |
| `tkil/` + `tkil/filters/` | 39 | 6 | Tinggi 🔴 |
| `viz/` | 11 | 2 | Rendah ✅ |
| `cli/commands/` | 145 | — | **Sangat Tinggi** 🔴🔴🔴 |

**Strategi:** Enable bertahap, mulai dari risiko rendah.

#### PGO (Profile Guided Optimization)

3 tahap:
1. **Instrument** — `/GL` + `/GENPROFILE`
2. **Train** — jalankan test suite untuk kumpulkan profil
3. **Optimize** — `/GL` + `/USEPROFILE`

**Efek:** 10-30% execution speed. **Tradeoff:** Build lebih lambat, binary lebih besar.
**Rekomendasi:** Terapkan di release CI, bukan dev loop.

#### PCH MSVC

Dua opsi:
- **A:** `CMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded` → `/Z7` → PCH kompatibel
- **B:** Split PCH per modul (core, cli, mcp)

### 3.3 Execution Speed — Detail

#### SQLite Optimization

```cpp
// PRAGMA untuk query speed
PRAGMA journal_mode = WAL;
PRAGMA synchronous = NORMAL;
PRAGMA mmap_size = 268435456;    // 256 MB memory-mapped
PRAGMA cache_size = -64000;      // 64 MB page cache
PRAGMA temp_store = MEMORY;
PRAGMA locking_mode = EXCLUSIVE;
```

**Compile flags:**
```cmake
target_compile_definitions(sqlite3 PRIVATE
  SQLITE_DEFAULT_MEMSTATUS=0
  SQLITE_OMIT_DEPRECATED
  SQLITE_OMIT_PROGRESS_CALLBACK
  SQLITE_MAX_EXPR_DEPTH=0
  SQLITE_ENABLE_MEMSYS5
)
```

**Efek:** Query cold start 30-50% lebih cepat.

#### Thread Pool

```cpp
parallel_pool::instance().resize(std::thread::hardware_concurrency());
parallel_pool::instance().warm();  // spawn semua thread pas init
```

#### Branchless BM25

Hot path di `imem/scorer.cpp` — dipanggil tiap search:

```cpp
// Branchless version — 0 branch mispredict
float idf = log(1 + (N - df + 0.5) / (df + 0.5));
float numerator = term_freq * (k1 + 1);
float denominator = term_freq + k1 * (1 - b + b * doc_len / avg_doc_len);
float score = (term_freq > 0) ? idf * numerator / denominator : 0.0f;
```

#### Arena Allocator

Untuk session-scoped memory (tkil pipeline, glossary compression, BM25, graph traversal):

- Pre-alloc contiguous block
- Linear allocation (no fragmentation)
- Single `reset()` untuk free semua
- 5-10% throughput improvement

#### String Interning

`abbr_store.cpp` dan `imem/memory_store.cpp` banyak string ops:

```cpp
class StringPool {
  std::pmr::unsynchronized_pool_resource pool_;
  std::pmr::set<std::string_view> interned_{&pool_};
public:
  std::string_view intern(std::string_view s) {
    auto [it, inserted] = interned_.insert(s);
    return *it;
  }
};
```

### 3.4 Startup Speed — Detail

#### Lazy Initialization

Semua command + MCP tool di-register via macro static — **semua** di-load pas startup.

**Yang bisa di-lazy:**
- MCP tools (33) — factory dipanggil pas tool dipake
- CLI command parser — parse help string aja
- Graph watcher — inisialisasi pas `icmg graph watch`
- LLM backend — jangan load library sampe dipanggil

```cpp
// Dari eager:
static registry.add("tool_name", create_tool_instance());

// Jadi lazy:
static registry.add("tool_name", []() -> std::unique_ptr<ITool> {
    static auto instance = std::make_unique<ToolImpl>();
    return std::move(instance);
});
```

**Efek:** Startup 40-60% lebih cepat.

#### AOT SQL Statements

Prepared statement cache — parsing query cuma sekali:

```cpp
class StmtCache {
  std::unordered_map<std::string, sqlite3_stmt*> cache_;
public:
  sqlite3_stmt* get(const char* sql) {
    auto it = cache_.find(sql);
    if (it != cache_.end()) return it->second;
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    cache_[sql] = stmt;
    return stmt;
  }
};
```

**Efek:** 20-50% lebih cepat untuk query berulang.

#### Daemon Mode

- `icmg daemon start` — spawn background process, pre-load semua resource
- `icmg <command>` — RPC ke daemon (pipe/socket)
- Cold startup → load + serve
- Warm startup → ~5ms response

### 3.5 Prioritized Action Plan

| # | Teknik | Effort | Impact Build | Impact Runtime |
|---|---|---|---|---|
| 1 | **sccache** | Rendah (1-2 jam) | 30-60% | — |
| 2 | **Unity Build (low-risk)** | Rendah (1-2 jam) | 10-20% | — |
| 3 | **SQLite mmap + WAL** | Rendah (< 1 jam) | — | 30-50% query |
| 4 | **AOT SQL Statements** | Rendah (2-4 jam) | — | 20-50% query |
| 5 | **Lazy Init (MCP tools)** | Sedang (4-8 jam) | — | 40-60% startup |
| 6 | **Arena Allocator** | Sedang (4-8 jam) | — | 5-10% throughput |
| 7 | **Unity Build (all)** | Tinggi (1-2 hari) | 30-40% | — |
| 8 | **PGO** | Tinggi (2-3 hari) | -30% build | 10-30% exec |
| 9 | **PCH MSVC fix** | Sedang (1-2 hari) | 10-20% | — |
| 10 | **Daemon mode** | Tinggi (3-5 hari) | — | 90% startup |
| 11 | **String interning** | Rendah (2-4 jam) | — | 5-15% mem |
| 12 | **Branchless BM25** | Rendah (< 1 jam) | — | 0-5% search |

### Quick Wins

| # | Teknik | Hasil | Modal |
|---|---|---|---|
| 1 | sccache | 30-60% build speed | 1 jam |
| 2 | SQLite mmap + PRAGMA | 30-50% query speed | 30 menit |
| 3 | AOT SQL Statements | 20-50% query speed | 2 jam |
| 4 | Unity (low-risk modules) | ~15% build speed | 1 jam |
| 5 | Branchless BM25 | 0 branch mispredict | 30 menit |

---

## 4. Security Audit

| Celah | Risiko | Solusi |
|---|---|---|
| **Path traversal** | File arbitrary bisa dibaca | Normalisasi path di semua input |
| **Daemon IPC tanpa auth** | Siapa pun bisa akses pipe | Token / PID verification |
| **No encryption at rest** | DB plaintext | SQLite encryption / SQLCipher |
| **No rate limiting** | Abuse MCP tools | Throttle per-session |
| **Secret scanner memory-only** | File on disk tidak ter-scan | Filesystem scan mode |

---

## 5. Roadmap

| Prioritas | Fitur | Sumber | Estimasi |
|---|---|---|---|
| P0 | **Interactive HTML Viz** | Graphify | 3-5 hari |
| P0 | **Transcript System (FTS5)** | ICM | 3-5 hari |
| P1 | **Dual Memory** | ICM | 5-7 hari |
| P1 | **Feedback Loop** | ICM | 3-4 hari |
| P1 | **Edge Confidence** | Graphify | 2-3 hari |
| P1 | **sccache + SQLite tuning** | — | 1 hari |
| P2 | **Access-aware Decay** | ICM | 3-4 hari |
| P2 | **God Node Report** | Graphify | 2-3 hari |
| P2 | **TUI Dashboard** | ICM | 5-7 hari |
| P3 | **Multi-agent DB** | ICM | 5-7 hari |
| P3 | **Multimodal Graph Nodes** | Graphify | 5-7 hari |
| P3 | **Benchmark Suite** | ICM | 3-5 hari |

---

*Dibuat dengan dedikasi penuh untuk satu majikan.*
