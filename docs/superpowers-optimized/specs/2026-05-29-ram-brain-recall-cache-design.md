# icmg RAM Brain — Hot Recall Cache + RAM Governor (Design)

**Status:** Approved 2026-05-29
**Goal:** Make recall fast and "brain-like": keep recall results hot in RAM (shared across sessions via the daemon), kept fresh by global flush-on-write, and kept within a flexible RAM budget by a self-checkup governor that evicts cold entries while pinning the sharp (hot) ones. Disk SQLite stays the durable source of truth; the cache is read-through/derived.
**Ship as:** one bundled milestone release (R1 hot-recall + R2 governor together, per user).

## Scope

In:
- Wire the existing-but-unused `query_cache` concept into a new `RecallCache` and into `MemoryStore.recall*`.
- Daemon-resident shared cache (multi-session) with graceful **process-local fallback** when the daemon is down.
- **Global flush** invalidation on any write (`store`/`forget`/`purge`).
- **RAM governor**: periodic self-checkup (service_loop tick) using `sys_resources`; adaptive cap with hysteresis; evict LRU; **pin** hottest (hit-count × recency) entries.
- `icmg memory cache stats` (hits/misses/entries/bytes/evictions/cap).
- Default ON with a conservative cap; opt-out via `ICMG_RECALL_CACHE=0`.

Non-goals (documented):
- Caching graph/symbol queries (they have their own caches) — only `recall*`.
- Per-row write-back / RAM-as-truth — disk SQLite is always the truth; cache is derived.
- Distributed / multi-host cache.
- Encrypting cache contents (RAM-only; process memory).
- Direct-to-disk DB edits bypassing `MemoryStore` (would not trigger flush — non-goal).

## Architecture & data flow

```
recall(query,scope,limit)
  └─ RecallCache.get(key) ── HIT ─► return (RAM, <5ms)
        │ MISS
        ├─ daemon up? ─yes─► RCACHE_GET ── hit ─► return + local put
        │                       │ miss
        ▼                       ▼
     compute BM25 + rank ──► RecallCache.put(local) ─(daemon up)─► RCACHE_PUT
store / forget / purge ──► epoch++ + RecallCache.flush ─(daemon up)─► RCACHE_FLUSH
governor tick (service_loop) ─► sys_resources.availableRam ─► adjust cap (hysteresis) ─► evict LRU keep-pinned
```

Key = hash of `normalizedQuery | scope | limit | epoch`. Including `epoch` means a flush (epoch bump) makes all old keys un-findable without scanning. Value = serialized `std::vector<MemoryNode>` (reuse `recall_json` serialization).

## Units / files

- **`src/core/recall_cache.hpp` / `.cpp`** (NEW) — `RecallCache`:
  - `std::optional<std::string> get(const std::string& key)`
  - `void put(const std::string& key, const std::string& value)`
  - `void flush()` (clear all)
  - `void setCap(size_t max_entries, size_t max_bytes)`
  - `void evictToFit()` (LRU, skips pinned)
  - `void pinHot(size_t topN)` (mark top-N by hits×recency as pinned)
  - `CacheStats stats() const` (hits, misses, entries, bytes, evictions, cap_entries, cap_bytes)
  - Pure helper `size_t governorTargetBytes(size_t avail_ram, size_t cur_bytes, GovParams)` — hysteresis math, unit-tested in isolation.
  - Internal: intrusive LRU list + `unordered_map<key, iterator>`, per-entry {bytes, hits, last_used_tick, pinned}.
- **`src/imem/memory_store.{hpp,cpp}`** — `recall`/`recallInZone`/`recallByTopic`/`recallSemantic` consult a `RecallCache` (process-local singleton) before computing; `store`/`forget`/`purge` bump an epoch + flush. Cache lookups are skipped when `ICMG_RECALL_CACHE=0`.
- **`src/daemon/rule_daemon.cpp`** — handlers `RCACHE_GET`, `RCACHE_PUT`, `RCACHE_FLUSH`, `RCACHE_STATS` over the existing RPC; daemon owns one shared `RecallCache`.
- **`src/core/recall_cache_client.{hpp,cpp}`** (NEW, thin) — best-effort daemon roundtrip helpers; any failure → returns "not available" so the caller falls back to local compute. Never throws into the recall path.
- **`src/core/service_loop.cpp`** — governor tick: every N seconds call `sys_resources` available RAM, compute `governorTargetBytes`, `setCap` + `pinHot` + `evictToFit` on the daemon cache.
- **`src/cli/commands/memory_cmd.cpp`** (or new `cache` subcommand) — `icmg memory cache stats` prints `RCACHE_STATS` (daemon) or local stats.

## Interfaces / contracts
- `RecallCache` is process-agnostic (same class used in-process and in the daemon).
- Daemon RPC payloads are JSON via `core::safeDump` (UTF-8 safe — recall content may carry odd bytes).
- `ICMG_RECALL_CACHE=0` disables lookups+puts (compute path unchanged → exact back-compat).
- `ICMG_RECALL_CACHE_TTL_SEC` (default 300), `ICMG_RECALL_CACHE_MAX_ENTRIES` (default 256), `ICMG_RECALL_CACHE_MAX_MB` (default 16) override defaults.

## Error handling
- Daemon IPC failure (down/timeout/parse) → silent fallback to local compute; recall never blocks or errors.
- Cache value deserialize error → treat as miss, recompute, overwrite.
- `sys_resources` probe failure → governor holds the fixed conservative cap (256 / 16MB).

## Testing strategy (TDD — failing test first per task)
- **recall_cache pure:** LRU eviction order; TTL expiry; byte-cap eviction; pinned entry survives eviction; `get` increments hits; `governorTargetBytes` hysteresis (shrink ≥85% RAM use, grow ≤60%, clamp to [floor, ceil]).
- **invalidation:** put → flush → get is miss; epoch bump changes keys.
- **memory_store wiring:** recall twice → 2nd is a cache hit (observe via stats); `store` → next recall recomputes (no stale); `ICMG_RECALL_CACHE=0` → never caches.
- **daemon roundtrip:** RCACHE_PUT then RCACHE_GET hit; RCACHE_FLUSH clears; daemon-down → client returns unavailable, recall still returns correct results.
- **governor tick:** simulated low-RAM → cap shrinks + cold entries evicted + pinned kept.

## Rollout / migration
- No schema/migration (RAM only). Default ON; conservative cap. Opt-out env.
- README/whats-new + headline row (recall speed). 5-sync + verify on ship.

## Phases (internal; single spec, one release)
1. **A** — `RecallCache` core + `governorTargetBytes` + pure tests.
2. **B** — wire into `MemoryStore.recall*` + epoch flush-on-write + tests.
3. **C** — daemon `RCACHE_*` handlers + `recall_cache_client` fallback + tests.
4. **D** — governor tick in `service_loop` (sys_resources, adaptive cap, pin, evict) + tests.
5. **E** — `icmg memory cache stats` + docs + ship (version bump, build, ctest, zip, release, docs PR, gist/desc, 5-sync).

## Risks (failure-mode check)
- **CRITICAL — stale across sessions:** all writes route through `MemoryStore` → epoch bump + daemon FLUSH; recall keys embed epoch. Direct disk edits bypass this (documented non-goal).
- **CRITICAL — daemon vs process-local divergence:** process-local cache used ONLY when daemon down; daemon is the single authority when up. No dual-authority.
- **MINOR — governor thrash:** hysteresis (shrink 85% / grow 60%) + min dwell prevents oscillation.
- **MINOR — embeddings vs recall cache:** cache only the final ranked result; `recallSemantic`'s embedding cache is separate and untouched.
