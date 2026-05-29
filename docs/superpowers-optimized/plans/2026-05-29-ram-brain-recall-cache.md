# icmg RAM Brain (Hot Recall Cache + RAM Governor) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: per project rule, NO Claude subagents — execute inline via superpowers-optimized:executing-plans, task-by-task. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Cache recall results hot in RAM (daemon-shared, process-local fallback), kept fresh by flush-on-write, bounded by a self-checkup RAM governor that evicts cold entries and pins hot ones.
**Architecture:** A process-agnostic `RecallCache` (LRU+TTL+byte cap) lives in `src/core`. `MemoryStore.recall*` consults it (local) before computing; writes bump an epoch + flush. The daemon owns one shared instance exposed via `RCACHE_*` RPC; a thin client gives best-effort daemon access with silent local fallback. A `service_loop` governor tick sizes the cap from `sys_resources` RAM with hysteresis and pins the hottest entries.
**Tech Stack:** C++17, MSVC, existing `Db`/daemon RPC/`sys_resources`/`recall_json`, CMake `add_icmg_test`.
**Assumptions:**
- Assumes all memory writes go through `MemoryStore::store/forget/purge` — will NOT stay coherent if a process edits the SQLite DB directly (documented non-goal).
- Assumes the daemon, when running, is the single cache authority — process-local cache is used ONLY when the daemon is unreachable.
- Assumes recall result sets are small enough to serialize cheaply (they are limit-bounded, default ≤10).

---

## File Structure
- `src/core/recall_cache.hpp` / `.cpp` — **Create**. `RecallCache` class + `governorTargetBytes` pure fn + `CacheStats`. One responsibility: bounded in-RAM key→value store with LRU/TTL/pin.
- `src/core/recall_cache_client.hpp` / `.cpp` — **Create**. Best-effort daemon RCACHE roundtrip (get/put/flush/stats); never throws into recall.
- `src/imem/memory_store.cpp` / `.hpp` — **Modify**. Consult cache in `recall*`; epoch + flush in `store/forget/purge`.
- `src/daemon/rule_daemon.cpp` — **Modify**. `RCACHE_GET/PUT/FLUSH/STATS` handlers over one shared `RecallCache`.
- `src/core/service_loop.cpp` — **Modify**. Governor tick.
- `src/cli/commands/memory_cmd.cpp` — **Modify** (or `cache` subcommand). `icmg memory cache stats`.
- Tests: `tests/core/test_recall_cache.cpp`, `tests/imem/test_recall_cache_wiring.cpp`, `tests/daemon/test_rcache_rpc.cpp`, `tests/core/test_governor_tick.cpp`.
- `CMakeLists.txt` — **Modify** (register tests).
- Version files — **Modify** at ship (Phase E).

---

### Task 1 (Phase A): RecallCache core — LRU + TTL + byte cap + pin

**Files:**
- Create: `src/core/recall_cache.hpp`, `src/core/recall_cache.cpp`
- Test: `tests/core/test_recall_cache.cpp`

**Does NOT cover:** daemon sharing (Task 5) and governor sizing decisions (the `governorTargetBytes` fn is added in Task 2). This task is the in-RAM container only.

- [ ] **Step 1: Write failing test**

```cpp
// tests/core/test_recall_cache.cpp
#include "../test_main.hpp"
#include "../../src/core/recall_cache.hpp"
using icmg::core::RecallCache;

TEST("recall_cache: put/get hit + miss") {
    RecallCache c; c.setCap(10, 1<<20);
    c.put("k1", "v1");
    auto v = c.get("k1");
    ASSERT_TRUE(v.has_value());
    ASSERT_EQ(*v, std::string("v1"));
    ASSERT_FALSE(c.get("nope").has_value());
}

TEST("recall_cache: LRU evicts oldest when over entry cap") {
    RecallCache c; c.setCap(2, 1<<20);
    c.put("a","1"); c.put("b","2");
    (void)c.get("a");           // touch a -> b now LRU
    c.put("c","3");             // over cap -> evict LRU (b)
    ASSERT_TRUE(c.get("a").has_value());
    ASSERT_FALSE(c.get("b").has_value());
    ASSERT_TRUE(c.get("c").has_value());
}

TEST("recall_cache: byte cap evicts") {
    RecallCache c; c.setCap(100, 8);   // 8 bytes
    c.put("a","12345");                // 5 bytes
    c.put("b","12345");                // +5 -> over 8 -> evict a
    ASSERT_FALSE(c.get("a").has_value());
    ASSERT_TRUE(c.get("b").has_value());
}

TEST("recall_cache: TTL expiry (caller clock)") {
    RecallCache c; c.setCap(10, 1<<20); c.setTtlSeconds(100);
    c.putAt("k","v", 1000);
    ASSERT_TRUE(c.getAt("k", 1099).has_value());
    ASSERT_FALSE(c.getAt("k", 1101).has_value());   // expired
}

TEST("recall_cache: pinned entry survives eviction") {
    RecallCache c; c.setCap(1, 1<<20);
    c.put("pin","p"); c.pin("pin");
    c.put("other","o");           // over cap, but pin protected
    ASSERT_TRUE(c.get("pin").has_value());
    ASSERT_FALSE(c.get("other").has_value());
}

TEST("recall_cache: stats counts hits/misses/entries") {
    RecallCache c; c.setCap(10,1<<20);
    c.put("a","1"); (void)c.get("a"); (void)c.get("x");
    auto s = c.stats();
    ASSERT_EQ((int)s.hits, 1);
    ASSERT_EQ((int)s.misses, 1);
    ASSERT_EQ((int)s.entries, 1);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build-msvc-full --config Release --target icmg_test && ./build-msvc-full/Release/icmg_test.exe --filter recall_cache`
Expected: FAIL — `recall_cache.hpp` missing (compile error).

- [ ] **Step 3: Implement minimal change**

```cpp
// src/core/recall_cache.hpp
#pragma once
#include <cstdint>
#include <list>
#include <optional>
#include <string>
#include <unordered_map>

namespace icmg { namespace core {

struct CacheStats {
    std::uint64_t hits = 0, misses = 0, evictions = 0;
    std::size_t entries = 0, bytes = 0, cap_entries = 0, cap_bytes = 0;
};

class RecallCache {
public:
    void setCap(std::size_t max_entries, std::size_t max_bytes);
    void setTtlSeconds(std::int64_t ttl) { ttl_ = ttl; }

    // Wall-clock variants use an internal monotonic tick (seconds since first use,
    // injected by caller in tests via *At). Production callers use get/put.
    std::optional<std::string> get(const std::string& key);
    void put(const std::string& key, const std::string& value);

    // Deterministic-clock variants for tests.
    std::optional<std::string> getAt(const std::string& key, std::int64_t now_sec);
    void putAt(const std::string& key, const std::string& value, std::int64_t now_sec);

    void flush();
    void pin(const std::string& key);
    void pinHot(std::size_t topN);
    void evictToFit();
    CacheStats stats() const;

private:
    struct Entry { std::string key, value; std::size_t bytes; std::uint64_t hits;
                   std::int64_t last_used; bool pinned; };
    std::list<Entry> lru_;                                   // front = MRU
    std::unordered_map<std::string, std::list<Entry>::iterator> map_;
    std::size_t cap_entries_ = 256, cap_bytes_ = 16u<<20, bytes_ = 0;
    std::int64_t ttl_ = 300;
    mutable CacheStats agg_{};   // cumulative hits/misses/evictions
    void touch(std::list<Entry>::iterator it, std::int64_t now);
    void evictLRUUnpinned();
};

// Pure governor sizing: given available RAM (MB) and current cache bytes, return
// the target byte cap with hysteresis. Shrinks aggressively when RAM tight,
// grows toward ceil when ample. Clamped to [floor_bytes, ceil_bytes].
std::size_t governorTargetBytes(std::uint64_t avail_ram_mb, std::size_t cur_bytes,
                                std::size_t floor_bytes, std::size_t ceil_bytes,
                                std::uint64_t total_ram_mb);

}} // namespace
```

```cpp
// src/core/recall_cache.cpp
#include "recall_cache.hpp"
#include <algorithm>
namespace icmg { namespace core {

void RecallCache::setCap(std::size_t e, std::size_t b) {
    cap_entries_ = e; cap_bytes_ = b; evictToFit();
}
void RecallCache::touch(std::list<Entry>::iterator it, std::int64_t now) {
    it->last_used = now; ++it->hits;
    lru_.splice(lru_.begin(), lru_, it);   // move to front (MRU)
}
std::optional<std::string> RecallCache::getAt(const std::string& key, std::int64_t now) {
    auto m = map_.find(key);
    if (m == map_.end()) { ++agg_.misses; return std::nullopt; }
    auto it = m->second;
    if (ttl_ > 0 && now - it->last_used > ttl_ && !it->pinned) {
        bytes_ -= it->bytes; lru_.erase(it); map_.erase(m); ++agg_.misses; return std::nullopt;
    }
    ++agg_.hits; touch(it, now); return it->value;
}
void RecallCache::putAt(const std::string& key, const std::string& value, std::int64_t now) {
    auto m = map_.find(key);
    if (m != map_.end()) { bytes_ -= m->second->bytes; lru_.erase(m->second); map_.erase(m); }
    Entry e{key, value, key.size()+value.size(), 0, now, false};
    bytes_ += e.bytes;
    lru_.push_front(std::move(e));
    map_[key] = lru_.begin();
    evictToFit();
}
// Production clock: seconds via a lazily-seeded monotonic counter is unavailable
// (steady_clock banned in pure header tests), so production wraps time() here.
#include <ctime>
std::optional<std::string> RecallCache::get(const std::string& k){ return getAt(k,(std::int64_t)std::time(nullptr)); }
void RecallCache::put(const std::string& k,const std::string& v){ putAt(k,v,(std::int64_t)std::time(nullptr)); }

void RecallCache::evictLRUUnpinned() {
    for (auto it = std::prev(lru_.end()); ; --it) {
        if (!it->pinned) {
            bytes_ -= it->bytes; map_.erase(it->key); lru_.erase(it); ++agg_.evictions; return;
        }
        if (it == lru_.begin()) return;   // all pinned
    }
}
void RecallCache::evictToFit() {
    while ((lru_.size() > cap_entries_ || bytes_ > cap_bytes_) && !lru_.empty()) {
        std::size_t before = lru_.size();
        evictLRUUnpinned();
        if (lru_.size() == before) break;   // only pinned left
    }
}
void RecallCache::flush() { lru_.clear(); map_.clear(); bytes_ = 0; }
void RecallCache::pin(const std::string& key) {
    auto m = map_.find(key); if (m != map_.end()) m->second->pinned = true;
}
void RecallCache::pinHot(std::size_t topN) {
    std::vector<std::list<Entry>::iterator> v;
    for (auto it = lru_.begin(); it != lru_.end(); ++it) { it->pinned = false; v.push_back(it); }
    std::sort(v.begin(), v.end(), [](auto a, auto b){ return a->hits > b->hits; });
    for (std::size_t i = 0; i < topN && i < v.size(); ++i) v[i]->pinned = true;
}
CacheStats RecallCache::stats() const {
    CacheStats s = agg_; s.entries = lru_.size(); s.bytes = bytes_;
    s.cap_entries = cap_entries_; s.cap_bytes = cap_bytes_; return s;
}
std::size_t governorTargetBytes(std::uint64_t avail_mb, std::size_t cur,
                                std::size_t floor_b, std::size_t ceil_b,
                                std::uint64_t total_mb) {
    if (total_mb == 0) return floor_b;
    double used_frac = 1.0 - (double)avail_mb / (double)total_mb;
    std::size_t target = cur ? cur : floor_b;
    if (used_frac >= 0.85) target = std::max(floor_b, cur / 2);   // RAM tight -> shrink
    else if (used_frac <= 0.60) target = std::min(ceil_b, (cur ? cur : floor_b) * 2); // ample -> grow
    return std::min(std::max(target, floor_b), ceil_b);
}
}} // namespace
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build-msvc-full --config Release --target icmg_test && ./build-msvc-full/Release/icmg_test.exe --filter recall_cache`
Expected: PASS (6 checks). Register `add_icmg_test(test_recall_cache tests/core/test_recall_cache.cpp) # ram-brain A` in CMakeLists.txt.

- [ ] **Step 5: Commit**

```bash
git add src/core/recall_cache.hpp src/core/recall_cache.cpp tests/core/test_recall_cache.cpp CMakeLists.txt
git commit -m "ram-brain A: RecallCache core (LRU+TTL+byte cap+pin) + governorTargetBytes"
```

---

### Task 2 (Phase A): governor sizing math tests

**Files:**
- Test: `tests/core/test_recall_cache.cpp` (append)

- [ ] **Step 1: Write failing test**

```cpp
// append to tests/core/test_recall_cache.cpp
using icmg::core::governorTargetBytes;
TEST("governor: tight RAM shrinks, ample grows, clamped") {
    std::size_t FLOOR = 4u<<20, CEIL = 64u<<20;
    // 90% used (avail 1000 of 10000) -> shrink cur/2
    ASSERT_EQ(governorTargetBytes(1000, 32u<<20, FLOOR, CEIL, 10000), (std::size_t)(16u<<20));
    // 50% used -> grow cur*2
    ASSERT_EQ(governorTargetBytes(5000, 16u<<20, FLOOR, CEIL, 10000), (std::size_t)(32u<<20));
    // grow clamped to CEIL
    ASSERT_EQ(governorTargetBytes(9000, 60u<<20, FLOOR, CEIL, 10000), CEIL);
    // shrink clamped to FLOOR
    ASSERT_EQ(governorTargetBytes(100, 4u<<20, FLOOR, CEIL, 10000), FLOOR);
    // total 0 -> floor (probe failed)
    ASSERT_EQ(governorTargetBytes(0, 32u<<20, FLOOR, CEIL, 0), FLOOR);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `./build-msvc-full/Release/icmg_test.exe --filter governor` (after rebuild)
Expected: PASS already if Task 1 math is correct; if any assert fails, fix `governorTargetBytes` clamping. (This task locks the governor contract.)

- [ ] **Step 3: Implement minimal change**

If Step 2 failed, adjust `governorTargetBytes` in `recall_cache.cpp` so all 5 cases hold (hysteresis bands 0.85 / 0.60, clamp [floor,ceil], total==0→floor).

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build-msvc-full --config Release --target icmg_test && ./build-msvc-full/Release/icmg_test.exe --filter governor`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add tests/core/test_recall_cache.cpp src/core/recall_cache.cpp
git commit -m "ram-brain A: lock governorTargetBytes hysteresis contract via tests"
```

---

### Task 3 (Phase B): wire RecallCache into MemoryStore.recall + epoch flush-on-write

**Files:**
- Modify: `src/imem/memory_store.hpp`, `src/imem/memory_store.cpp`
- Test: `tests/imem/test_recall_cache_wiring.cpp`

**Does NOT cover:** daemon sharing (Task 5). Here the cache is a process-local `static` instance; multi-process coherence comes with the daemon. `ICMG_RECALL_CACHE=0` disables.

- [ ] **Step 1: Write failing test**

```cpp
// tests/imem/test_recall_cache_wiring.cpp
#include "../test_main.hpp"
#include "../../src/imem/memory_store.hpp"
#include "../../src/core/recall_cache.hpp"
#include <filesystem>
using namespace icmg;
TEST("recall-wiring: second identical recall is a cache hit") {
    namespace fs = std::filesystem;
    auto db = (fs::temp_directory_path() / "icmg_rcw.db").string();
    std::error_code ec; fs::remove(db, ec);
    imem::MemoryStore ms(db);
    imem::MemoryNode n; n.topic = "t"; n.content = "alpha bravo"; ms.store(n);
    auto& cache = imem::MemoryStore::recallCache();   // expose for test
    cache.flush();
    auto before = cache.stats().hits;
    ms.recall("alpha", 5);
    ms.recall("alpha", 5);                            // identical -> hit
    ASSERT_TRUE(cache.stats().hits > before);
    fs::remove(db, ec);
}
TEST("recall-wiring: store flushes cache (no stale)") {
    namespace fs = std::filesystem;
    auto db = (fs::temp_directory_path() / "icmg_rcw2.db").string();
    std::error_code ec; fs::remove(db, ec);
    imem::MemoryStore ms(db);
    imem::MemoryNode a; a.topic="t"; a.content="alpha one"; ms.store(a);
    ms.recall("alpha", 5);                            // cached
    imem::MemoryNode b; b.topic="t"; b.content="alpha two"; ms.store(b);  // must flush
    auto r = ms.recall("alpha", 5);
    int hits = 0; for (auto& n : r) if (n.content.find("two")!=std::string::npos) ++hits;
    ASSERT_TRUE(hits >= 1);                            // sees the new row, not stale
    fs::remove(db, ec);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: build + `./build-msvc-full/Release/icmg_test.exe --filter recall-wiring`
Expected: FAIL — `MemoryStore::recallCache()` undefined.

- [ ] **Step 3: Implement minimal change**

In `memory_store.hpp` add:
```cpp
public:
    static icmg::core::RecallCache& recallCache();   // process-local shared instance
private:
    static std::int64_t& recallEpoch();
```
In `memory_store.cpp`:
```cpp
#include "../core/recall_cache.hpp"
#include "../cli/recall_json.hpp"
#include <cstdlib>
icmg::core::RecallCache& MemoryStore::recallCache() {
    static icmg::core::RecallCache c; return c;
}
std::int64_t& MemoryStore::recallEpoch() { static std::int64_t e = 0; return e; }
static bool cacheEnabled() {
    const char* v = std::getenv("ICMG_RECALL_CACHE");
    return !(v && v[0]=='0');
}
static std::string rkey(const std::string& q, int limit, const std::string& scope, std::int64_t epoch) {
    return std::to_string(epoch) + "|" + scope + "|" + std::to_string(limit) + "|" + q;
}
```
Wrap the body of `recall(query, limit, fuzzy)`:
```cpp
std::vector<MemoryNode> MemoryStore::recall(const std::string& query, int limit, bool fuzzy) {
    std::string key;
    if (cacheEnabled() && !fuzzy) {
        key = rkey(query, limit, "default", recallEpoch());
        if (auto hit = recallCache().get(key))
            return icmg::cli::recallNodesFromJson(*hit);   // deserialize
    }
    std::vector<MemoryNode> result = /* ... existing compute body ... */;
    if (!key.empty()) recallCache().put(key, icmg::cli::recallNodesToJson(result));
    return result;
}
```
And in `store`, `forget`/soft-delete, `purge`: bump epoch + flush:
```cpp
    ++recallEpoch();
    recallCache().flush();
```
Add `recallNodesFromJson` to `src/cli/recall_json.hpp` (inverse of `recallNodesToJson`, parse JSON array → vector<MemoryNode>, via nlohmann + safeDump-safe parse).

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build-msvc-full --config Release --target icmg_test && ./build-msvc-full/Release/icmg_test.exe --filter recall-wiring`
Expected: PASS (2 checks).

- [ ] **Step 5: Commit**

```bash
git add src/imem/memory_store.hpp src/imem/memory_store.cpp src/cli/recall_json.hpp tests/imem/test_recall_cache_wiring.cpp CMakeLists.txt
git commit -m "ram-brain B: wire RecallCache into MemoryStore.recall + epoch flush-on-write"
```

---

### Task 4 (Phase B): extend cache to recallInZone / recallByTopic

**Files:**
- Modify: `src/imem/memory_store.cpp`
- Test: `tests/imem/test_recall_cache_wiring.cpp` (append)

**Does NOT cover:** `recallSemantic` (its embedding path is cached separately; only the final ranked list is cached, scope tag "sem").

- [ ] **Step 1: Write failing test**

```cpp
// append
TEST("recall-wiring: recallInZone uses distinct scope key (no cross-pollution)") {
    namespace fs = std::filesystem;
    auto db = (fs::temp_directory_path() / "icmg_rcw3.db").string();
    std::error_code ec; fs::remove(db, ec);
    imem::MemoryStore ms(db);
    imem::MemoryNode n; n.topic="t"; n.content="alpha zone"; n.zone="z1"; ms.store(n);
    auto& cache = imem::MemoryStore::recallCache(); cache.flush();
    ms.recall("alpha", 5);
    ms.recallInZone("alpha", "z1", 5);
    // distinct scope keys -> 2 entries, not a false hit
    ASSERT_TRUE(cache.stats().entries >= 2);
    fs::remove(db, ec);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: build + `--filter recall-wiring`
Expected: FAIL — recallInZone not yet cached (entries < 2).

- [ ] **Step 3: Implement minimal change**

Wrap `recallInZone(query, zone, limit)` and `recallByTopic(topic, limit)` with the same cache pattern, using scope tags `"zone:"+zone` and `"topic"` respectively in `rkey`.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build-msvc-full --config Release --target icmg_test && ./build-msvc-full/Release/icmg_test.exe --filter recall-wiring`
Expected: PASS (3 checks).

- [ ] **Step 5: Commit**

```bash
git add src/imem/memory_store.cpp tests/imem/test_recall_cache_wiring.cpp
git commit -m "ram-brain B: cache recallInZone + recallByTopic with distinct scope keys"
```

---

### Task 5 (Phase C): daemon RCACHE_GET/PUT/FLUSH/STATS + client fallback

**Files:**
- Create: `src/core/recall_cache_client.hpp`, `src/core/recall_cache_client.cpp`
- Modify: `src/daemon/rule_daemon.cpp`
- Test: `tests/daemon/test_rcache_rpc.cpp`

**Does NOT cover:** governor (Task 6). Here the daemon merely owns a shared cache; sizing stays at defaults until the governor tick lands.

- [ ] **Step 1: Write failing test**

```cpp
// tests/daemon/test_rcache_rpc.cpp
#include "../test_main.hpp"
#include "../../src/daemon/rule_daemon.hpp"
// Drive the handler map directly (no socket) — mirrors existing daemon tests.
TEST("rcache-rpc: PUT then GET hit; FLUSH clears") {
    icmg::daemon::RuleDaemon d;
    auto& H = d.handlersForTest();   // expose handlers_ for test (like other daemon tests)
    H["RCACHE_PUT"]("{\"key\":\"k\",\"value\":\"v\"}");
    auto got = H["RCACHE_GET"]("{\"key\":\"k\"}");
    ASSERT_TRUE(got.find("\"value\":\"v\"") != std::string::npos);
    H["RCACHE_FLUSH"]("{}");
    auto miss = H["RCACHE_GET"]("{\"key\":\"k\"}");
    ASSERT_TRUE(miss.find("\"miss\":true") != std::string::npos);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: build + `--filter rcache-rpc`
Expected: FAIL — handlers not registered.

- [ ] **Step 3: Implement minimal change**

In `rule_daemon.cpp` ctor, add (one shared member `RecallCache rcache_;`):
```cpp
handlers_["RCACHE_GET"] = [this](const std::string& body){
    auto j = json::parse(body); std::string k = j.value("key","");
    auto v = rcache_.get(k);
    json r; if (v){ r["value"]=*v; } else { r["miss"]=true; }
    return icmg::core::safeDump(r);
};
handlers_["RCACHE_PUT"] = [this](const std::string& body){
    auto j = json::parse(body); rcache_.put(j.value("key",""), j.value("value",""));
    return std::string("{\"ok\":true}");
};
handlers_["RCACHE_FLUSH"] = [this](const std::string&){ rcache_.flush(); return std::string("{\"ok\":true}"); };
handlers_["RCACHE_STATS"] = [this](const std::string&){
    auto s = rcache_.stats(); json r;
    r["hits"]=s.hits; r["misses"]=s.misses; r["entries"]=s.entries; r["bytes"]=s.bytes;
    r["evictions"]=s.evictions; r["cap_bytes"]=s.cap_bytes; return icmg::core::safeDump(r);
};
```
Add `std::vector<std::string>`-safe parse guards (wrap in try/catch → `{"miss":true}` on error). Expose `handlersForTest()` returning `handlers_` (guard with `#ifdef ICMG_MONO_TEST` or a public accessor like other daemon tests use).

`recall_cache_client.cpp`: `std::optional<std::string> daemonGet(key)`, `daemonPut(key,val)`, `daemonFlush()`, `daemonStats()` — each opens the existing client connection; on ANY failure return nullopt/false (never throw). Then in `memory_store.cpp` recall: try `daemonGet` first (if daemon reachable), else local; on local compute, `daemonPut` best-effort; on write, `daemonFlush` best-effort in addition to local flush.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build-msvc-full --config Release --target icmg_test && ./build-msvc-full/Release/icmg_test.exe --filter rcache-rpc`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/core/recall_cache_client.hpp src/core/recall_cache_client.cpp src/daemon/rule_daemon.cpp src/imem/memory_store.cpp tests/daemon/test_rcache_rpc.cpp CMakeLists.txt
git commit -m "ram-brain C: daemon RCACHE_* shared cache + best-effort client fallback"
```

---

### Task 6 (Phase D): governor tick in service_loop

**Files:**
- Modify: `src/core/service_loop.cpp`, `src/daemon/rule_daemon.cpp` (expose rcache_ to governor)
- Test: `tests/core/test_governor_tick.cpp`

**Does NOT cover:** changing eviction policy (LRU stays); only sizing the cap + pinning hot entries on each tick. Probe failure → fixed conservative cap.

- [ ] **Step 1: Write failing test**

```cpp
// tests/core/test_governor_tick.cpp
#include "../test_main.hpp"
#include "../../src/core/recall_cache.hpp"
// runGovernorOnce is the pure tick body: given a cache + ram numbers, resize+pin+evict.
namespace icmg { namespace core {
    void runGovernorOnce(RecallCache& c, std::uint64_t avail_mb, std::uint64_t total_mb);
}}
TEST("governor-tick: low RAM shrinks cap and evicts cold, keeps hot") {
    using namespace icmg::core;
    RecallCache c; c.setCap(100, 64u<<20);
    for (int i=0;i<50;i++) c.put("k"+std::to_string(i), std::string(1000,'x'));
    (void)c.get("k49"); (void)c.get("k49"); (void)c.get("k48");  // hot
    runGovernorOnce(c, /*avail*/200, /*total*/10000);            // 98% used -> shrink hard
    auto s = c.stats();
    ASSERT_TRUE(s.bytes <= s.cap_bytes);
    ASSERT_TRUE(c.get("k49").has_value());                       // hottest pinned/kept
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: build + `--filter governor-tick`
Expected: FAIL — `runGovernorOnce` undefined.

- [ ] **Step 3: Implement minimal change**

In `recall_cache.cpp` add:
```cpp
void runGovernorOnce(RecallCache& c, std::uint64_t avail_mb, std::uint64_t total_mb) {
    auto s = c.stats();
    std::size_t target = governorTargetBytes(avail_mb, s.bytes, 4u<<20, 64u<<20, total_mb);
    c.pinHot(16);                       // protect 16 hottest
    c.setCap(s.cap_entries ? s.cap_entries : 256, target);  // setCap calls evictToFit
}
```
In `service_loop.cpp` governor tick (every ~30s), call `runGovernorOnce(daemonRcache, availableRamMB(), totalRamMB())` using `icmg::core::availableRamMB()/totalRamMB()`. Gate the whole tick behind `cacheEnabled()`.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build-msvc-full --config Release --target icmg_test && ./build-msvc-full/Release/icmg_test.exe --filter governor-tick`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/core/recall_cache.hpp src/core/recall_cache.cpp src/core/service_loop.cpp tests/core/test_governor_tick.cpp CMakeLists.txt
git commit -m "ram-brain D: RAM governor tick (sys_resources watermark, adaptive cap, pin hot)"
```

---

### Task 7 (Phase E): `icmg memory cache stats` + ship

**Files:**
- Modify: `src/cli/commands/memory_cmd.cpp`; version files; README docs PR; memoirs.

- [ ] **Step 1: Add `cache stats` subcommand**

In `memory_cmd.cpp`, handle `icmg memory cache stats`: try `recall_cache_client::daemonStats()` (daemon shared); fall back to `MemoryStore::recallCache().stats()`. Print hits/misses/hit-rate/entries/bytes/cap/evictions. Manual smoke: `icmg memory cache stats` → prints a stats line.

- [ ] **Step 2: Full suite green**

Run: `cmake --build build-msvc-full --config Release && ctest --test-dir build-msvc-full -C Release`
Expected: PASS. Record new internal count (1120 + ~15 new ≈ 1135).

- [ ] **Step 3: Version bump**

`1.76.0` → `1.77.0` in `src/core/version.hpp`, `CMakeLists.txt` (project VERSION), `src/icmg.rc` (`1,77,0,0` + `"1.77.0"` ×2), `src/icmg.exe.manifest` (`1.77.0.0`).

- [ ] **Step 4: Stage + verify + ship**

Stage 13-file bundle (unchanged DLL set — RecallCache is in-process, adds no DLL). `icmg.exe --version` → `icmg 1.77.0`; `icmg memory cache stats` smoke. zip + sha256 → source commit+tag+push private → `gh release create v1.77.0` → docs PR (prepend whats-new, drop oldest, headline row "Hot recall cache (RAM, daemon-shared)", count → new) — **update memoirs FIRST then README** per CLAUDE.md baku rule → gist + repo desc.

- [ ] **Step 5: 5-sync + commit**

`icmg graph update / store / zone / wflog / verify`.

```bash
git add src/cli/commands/memory_cmd.cpp src/core/version.hpp CMakeLists.txt src/icmg.rc src/icmg.exe.manifest
git commit -m "ram-brain E: icmg memory cache stats + v1.77.0 ship"
```

---

## Self-Review

**Spec coverage:** RecallCache core (T1) ✓ · governor math (T1/T2) ✓ · wire recall* + flush-on-write (T3/T4) ✓ · daemon shared + client fallback (T5) ✓ · governor tick sys_resources/pin/evict (T6) ✓ · `memory cache stats` (T7) ✓ · default-ON + `ICMG_RECALL_CACHE=0` (T3) ✓ · global flush (T3) ✓ · docs/ship/headline (T7) ✓. No gaps.

**Placeholder scan:** Real code in every code step. One marked insertion `/* ... existing compute body ... */` in T3 = "keep the current recall implementation unchanged, wrapped by cache get/put" — explicit, not a TODO. `recallNodesFromJson` is defined in T3 step 3 (inverse of existing `recallNodesToJson`).

**Type consistency:** `RecallCache` API (`get/put/getAt/putAt/flush/pin/pinHot/evictToFit/setCap/setTtlSeconds/stats`) consistent T1→T6. `CacheStats` fields consistent. `governorTargetBytes(avail_mb,cur,floor,ceil,total_mb)` consistent T1→T2→T6. `runGovernorOnce(cache,avail,total)` T6. Daemon handler names `RCACHE_GET/PUT/FLUSH/STATS` consistent T5→T7. `recallCache()`/`recallEpoch()` statics consistent T3→T4.

**Watch-items for execution:**
- `handlersForTest()` accessor on RuleDaemon — confirm the existing daemon tests' pattern (they may construct RuleDaemon and access handlers_; mirror exactly, or make handlers_ test-visible).
- `recallNodesFromJson` must tolerate the same odd-byte content `recallNodesToJson` emits — parse defensively (try/catch → empty vector → treated as miss).
- Production clock uses `std::time` (steady_clock/Date.now banned in pure tests but `std::time` is fine in .cpp).
