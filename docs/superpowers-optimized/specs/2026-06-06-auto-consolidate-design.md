# Auto-Consolidate Memory Zones — Design

> Date: 2026-06-06
> Status: APPROVED pending user spec-review (kak Cahyo)
> Author: Claudy (brain-side) — sole project owner; luna = idea-source (feature #6)
> Origin: zone `default` at 28k+ entries; the `> 7` consolidation hint spams every
> store and is never actionable; bloat degrades BM25 recall quality.

## Problem

`icmg memory consolidate --zone X` (collapse near-duplicates: cosine ≥0.92 with embedder,
Jaccard ≥0.85 fallback; soft-delete loser, sum frequency) exists but is **manual**. Two
issues:
1. Zones grow unbounded (28k+) → recall noise; nobody runs consolidate by hand.
2. `store_cmd.cpp:88` emits the hint whenever `zone_count > 7` — fires on **every store** in
   any non-trivial zone, so it is pure noise (28k is always > 7) and never prompts action.

## Goal

Threshold-based **auto-consolidate**: when a zone crosses a meaningful size and a cooldown
has elapsed, consolidation runs automatically (opt-in) in the background — never on the hot
path, never surprising, fully reversible (consolidate already soft-deletes). Also fix the
noisy hint to be rate-limited and threshold-meaningful.

## Decisions (locked with kak Cahyo, 2026-06-06)

- **Opt-in, default OFF** — config `memory.auto_consolidate` (default false). No surprise mutation.
- **Background detached after store** — when over threshold + cooldown, spawn a detached
  `icmg memory consolidate --zone <z>` (non-blocking; not the hot path). No cron dependency.
- **Threshold default 1000** — config `memory.auto_consolidate_threshold` (overridable).
- **Fix the hint** — rate-limited (once per cooldown, not every store) + same threshold.

## Scope

- New pure helper `src/imem/auto_consolidate.hpp` — decision functions (testable, no I/O).
- Per-zone cooldown timestamp marker (file under `.icmg/`, no DB migration).
- Rewire the hint block in `src/cli/commands/store_cmd.cpp` to: auto-run (if enabled) OR
  rate-limited hint (if disabled), replacing the unconditional `> 7` hint.
- Detached spawn of the existing consolidate command.
- Config keys read via existing `Config` (no schema change).

## Non-goals

- Hard delete / destructive merge — consolidate stays soft-delete only (reversible).
- LLM summarize-merge — consolidate is near-duplicate collapse only.
- Cross-zone or whole-DB consolidation in one pass — per-zone only.
- Cron/scheduler integration — background-detached chosen instead (no install dependency).
- Tuning the cosine/Jaccard thresholds — reuse consolidate's existing defaults.

## Architecture & Data Flow

### 1. `src/imem/auto_consolidate.hpp` (new, pure — header-only)

```cpp
namespace icmg::imem {

// Should we auto-run consolidate for this zone now?
// True iff count >= threshold AND cooldown elapsed since last_run.
inline bool shouldAutoConsolidate(int zone_count, int threshold,
                                  long long last_run_ts, long long now_ts,
                                  long long cooldown_s) {
    if (threshold <= 0) return false;
    if (zone_count < threshold) return false;
    return (now_ts - last_run_ts) >= cooldown_s;
}

// Should we show the (rate-limited) consolidation hint? Same gate, used when
// auto-consolidate is DISABLED. Rate-limited so it does not spam every store.
inline bool shouldShowHint(int zone_count, int threshold,
                           long long last_hint_ts, long long now_ts,
                           long long cooldown_s) {
    if (threshold <= 0) return false;
    if (zone_count < threshold) return false;
    return (now_ts - last_hint_ts) >= cooldown_s;
}

// Filesystem-safe marker filename for a zone (alnum + '_' kept, others -> '_').
inline std::string zoneMarkerName(const std::string& zone) {
    std::string s = "consolidate-";
    for (char c : zone)
        s += (std::isalnum((unsigned char)c) || c == '_') ? c : '_';
    s += ".ts";
    return s;
}

} // namespace icmg::imem
```

### 2. Cooldown marker (per-zone file)

- Path: `.icmg/<zoneMarkerName(zone)>` (e.g. `.icmg/consolidate-default.ts`).
- Content: a single epoch-seconds integer = last auto-run OR last-hint time.
- Read: parse int; missing/corrupt → treat as 0 (cooldown elapsed).
- Write: overwrite with current epoch seconds. **Written BEFORE spawn** (optimistic) to
  prevent concurrent stores from each spawning (thundering-herd guard).
- No DB migration — file marker keeps blast radius minimal.

### 3. Rewire `store_cmd.cpp` hint block (replaces the `zone_count > 7` block)

```
after store() succeeds and zone_count computed:
  threshold = Config.getInt("memory.auto_consolidate_threshold", 1000)
  cooldown  = Config.getInt("memory.auto_consolidate_cooldown_s", 86400)  // 24h
  now       = epoch_seconds()
  marker    = .icmg/zoneMarkerName(zone)
  last_ts   = readMarker(marker)            // 0 if absent

  if Config.getBool("memory.auto_consolidate", false):
      if shouldAutoConsolidate(zone_count, threshold, last_ts, now, cooldown):
          writeMarker(marker, now)          // before spawn (herd guard)
          spawnDetached("icmg memory consolidate --zone <z>")
          note: "[auto-consolidate] zone '<z>' (<n>) -> background consolidate started"
  else:
      if shouldShowHint(zone_count, threshold, last_ts, now, cooldown):
          writeMarker(marker, now)          // rate-limit the hint too
          hint = "zone '<z>' has <n> entries; consider 'icmg memory consolidate --zone <z>'"
```

The old unconditional `zone_count > 7` hint is removed.

### 4. Detached spawn

Use the existing detached/background exec mechanism (same approach `compact-bg` uses for
fire-and-forget). Non-blocking: store returns immediately; consolidate runs in its own
process. Verified at plan time (`core::exec_utils` detached launch).

## Interfaces / Contracts

- `shouldAutoConsolidate(count, threshold, last_run_ts, now_ts, cooldown_s) -> bool` — pure.
- `shouldShowHint(...) -> bool` — pure, same signature shape.
- `zoneMarkerName(zone) -> string` — pure, filesystem-safe.
- Config keys (read-only, defaults baked): `memory.auto_consolidate` (bool=false),
  `memory.auto_consolidate_threshold` (int=1000), `memory.auto_consolidate_cooldown_s` (int=86400).
- Marker file I/O: tiny read/write helpers (in store_cmd or a small util).

## Error Handling

- Marker read failure / corrupt → last_ts = 0 (cooldown treated elapsed; safe — at worst one
  extra run, still cooldown-guarded after the write).
- Detached spawn failure → log to stderr, do not fail the store (store already succeeded).
- Consolidate itself failing in background → its own problem; soft-delete means no data loss.

## Testing Strategy (TDD — failing test first)

- `tests/imem/test_auto_consolidate.cpp`:
  - shouldAutoConsolidate: below threshold → false; at/above + cooldown elapsed → true;
    above but within cooldown → false; threshold<=0 → false.
  - shouldShowHint: same matrix.
  - zoneMarkerName: alnum/underscore kept; slash/space/dot → '_'; stable + safe.
- Marker round-trip test (write then read returns same ts; missing → 0; corrupt → 0).
- store_cmd integration: behavior is the spawn/hint branch — covered by the pure helpers +
  a marker-state assertion; the actual detached spawn is not unit-tested (process boundary),
  documented as a manual smoke step.

## Rollout / Migration

- Additive. No DB migration (file marker). Default OFF → zero behavior change unless opted in.
- The hint becomes rate-limited immediately (noise reduction) even with auto OFF — this is a
  visible improvement on its own.
- Smoke: set `memory.auto_consolidate=true` + low threshold, store into a big zone → observe
  detached consolidate run once, marker written, no second run within cooldown.

## Failure-Mode Check (adversarial)

1. **Thundering herd** — many concurrent stores cross threshold simultaneously, each spawns
   consolidate before any marker write. **Severity: Minor** — mitigated by writing the marker
   BEFORE spawn; the window is microseconds and consolidate is idempotent (re-collapsing
   already-collapsed dupes is a no-op). Accept.
2. **store is hook-spawned (near-hot)** — adds marker read + (existing) count query per store.
   **Severity: Minor** — marker read is a tiny file; count query already ran for the old hint;
   spawn only on threshold+cooldown. No new hot-path LLM/embedder work in store itself.
3. **Background consolidate on 28k = minutes + ONNX load** — resource spike. **Severity: Minor**
   — detached (non-blocking) + 24h cooldown prevents thrash; soft-delete = safe.
4. **Zone name path-injection in marker filename** — **Severity: was Critical → dissolved** by
   `zoneMarkerName` sanitization (only alnum + `_`).

No Critical failure modes remain.
