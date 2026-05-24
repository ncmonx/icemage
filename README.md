<p align="center">
  <img src="assets/logo.svg" alt="Icemage" width="160"/>
</p>

# Icemage <sub><sup>(`icmg`)</sup></sub>

[![release](https://img.shields.io/github/v/release/ncmonx/icm-graph)](https://github.com/ncmonx/icm-graph/releases)
[![downloads](https://img.shields.io/github/downloads/ncmonx/icm-graph/total)](https://github.com/ncmonx/icm-graph/releases)
[![last-commit](https://img.shields.io/github/last-commit/ncmonx/icm-graph)](https://github.com/ncmonx/icm-graph/commits/main)
[![ctest](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/ncmonx/7d6a2efa9d6191e28ff3f6a26e6ba7c7/raw/ctest.json)](#)
[![mcp tools](https://img.shields.io/badge/MCP%20tools-40-blueviolet)](#)
[![license](https://img.shields.io/badge/license-Apache--2.0-blue.svg)](LICENSE)
[![OpenSSF Scorecard](https://api.securityscorecards.dev/projects/github.com/ncmonx/icm-graph/badge)](https://securityscorecards.dev/viewer/?uri=github.com/ncmonx/icm-graph)
[![OpenSSF Best Practices](https://img.shields.io/cii/level/12818?label=OpenSSF%20Best%20Practices)](https://www.bestpractices.dev/projects/12818)
[![sponsor](https://img.shields.io/badge/sponsor-GitHub-ea4aaa?logo=github-sponsors)](https://github.com/sponsors/ncmonx)
[![ko-fi](https://img.shields.io/badge/Ko--fi-tip-ff5e5b?logo=ko-fi)](https://ko-fi.com/ncmonx)

> **Stop burning tokens. Stop losing context. Ship faster.**

A small helper app that makes AI coding assistants — Claude Code, Cursor, and friends — **70 – 98 % cheaper** to run, without making them less helpful.

**40 MCP tools · 122/122 tests · single-binary · 100 % local · pure-bash hooks** (zero Python/jq dependency).

If you've ever watched a huge token bill evaporate on a single file read, paid for "thinking" you didn't need, or re-explained your project to the AI for the fifth time today — Icemage is for you.

---

## 🟢 Why Icemage

AI assistants are powerful but **wasteful by default**. Every time the AI opens a file, runs a command, or starts a new chat, it re-reads context it has seen many times and dumps full output into the conversation. Icemage sits quietly in the background and trims the noise before it ever reaches the AI:

- **Long files** → only the relevant slice
- **Noisy command output** → just the parts that matter
- **Web pages** → cached + summarised
- **Past decisions** → remembered across sessions so the AI doesn't ask twice
- **Repeated work** → results reused instead of recomputed

The AI keeps its full intelligence. Your wallet keeps more of its money.

---

## 📊 Headline numbers

| Metric | Typical | Best | Since |
|---|---|---|---|
| File-read savings | 70 – 85 % fewer tokens | up to 92 % | v0.5 |
| Test / build output | 60 – 80 % shorter | up to 90 % | v0.5 |
| **Multi-file UI propagation** (style-clone) | **30 – 50× cheaper** | up to 98 % | v1.22.0 |
| **Cross-project bundle** (port) | **8 – 12× cheaper** | up to 95 % | v1.24.0 |
| **Compressed-Write** (AI emit diff) | **70 – 95% fewer tokens** | up to 98 % | v1.25.0 |
| Web-fetch reduction | 70 – 90 % smaller | up to 95 % | v0.4 |
| Repeat-context recall | near-zero, **< 5 ms cached** | — | v1.21.8 |
| Past-chat full-text search | **< 10 ms** across months | — | v1.21.7 |
| Graph symbol lookup | **256-slot in-RAM cache** | — | v1.21.8 |
| First-prompt warmup | < 1 s | — | v1.18 |
| **Cold build time** (icmg itself) | **~50 % faster** (20 min → 9-10 min) | — | v1.26.0 |
| **MCP response filter** (verbose plugins) | **50 – 80 % smaller** | up to 90 % | v1.30.0 |
| **Auto-thinking suppress** (trivial prompts) | **~1500 tok / call saved** | — | v1.30.0 |
| **Caveman-auto** (long-prose replies) | **60 – 75 % compress** | up to 85 % | v1.30.0 |
| **Service auto-start** (UserPromptSubmit) | **0-touch warm-up** | — | v1.30.0 |
| **Path ambiguity warning** (icmg context) | wrong-file lookups → loud | — | v1.29.0 |
| **rg-wrapper + brace glob** (icmg grep/files) | flag-mirror, **{a,b}** expand | — | v1.29.0 |
| **Local LLM scaffold** (llama.cpp vendored, opt-in) | **0 cloud calls** | privacy-first | v1.31.0 |
| **Smart router** (REGEX vs LLM_LOCAL vs CACHE) | **<100 us p99** | hot-path forced regex | v1.31.0 |
| **HTTP streaming download** (model fetch + SHA256) | **400 MB - 2 GB** safe-verify | tamper-detect | v1.31.0 |
| **icmg git** wrapper (single ergonomic entry) | **Tkil-filtered** + safety-gated | enforces icmg-FIRST | v1.31.0 |
| **Python-free core** (PRECOMPACT_PY dropped) | **-200-500 ms** boot saved | single-binary | v1.31.0 |
| **pack --rerank** (LLM-reorder memory hits) | **opt-in** warm-path | router-gated | v1.32.0 |
| **PreCompact LLM summary** (warm-pool Qwen 0.5B) | **<15 s** cold | regex fallback always | v1.32.0 |
| **icmg compact-bg** (proactive memory worker) | **<3 s** warm | manual + future hook | v1.32.0 |
| **KV-cache mgmt** (LlamaRunner) | **multi-prompt safe** | no ctx overflow | v1.32.0 |
| Cost per AI session | **down 70 – 90 %** vs. raw | up to 95 % | — |

Measured on real-world sessions. Your mileage will vary with project size and habits — anyone running a busy AI agent for a day already sees meaningful savings.
---

## ✨ What's new

> **Recent releases.** Older entries archived in [`CHANGELOG.md`](CHANGELOG.md).

- **v1.38.0** - **C++20 standard bump + A7 amnesia counter live**. **M1**: bumped `CMAKE_CXX_STANDARD` from 17 → 20. Enables `std::format` / `std::expected` / concepts / ranges. Foundation for future v1.39+ modules pilot (10-50% rebuild gain per Google upstream research, but icmg adoption deferred until GCC 16 + 3rd-party lib modules mature). **A7 amnesia counter live**: Stop hook async tail extracts top-3 alnum tokens (4+ chars) from AI response, BM25-recalls `memory_nodes` last 7d via topic/content LIKE; hit logs `amnesia_events` row. Next UserPromptSubmit prepends **"AMNESIA WARNING (you may be re-asking already-decided things)"** header FIRST (before escalated_hint) — top-2 events last 24h cited. Opt-out: `ICMG_AMNESIA_QUIET=1`. Also declared (not yet wired; v1.38.1): `runPreToolUseTokenBudget` (word×1.3 forecast vs `~/.icmg/token-budget.json` cap default 50k) + `runPostToolForceCompress` (dedup + tail-cap >2 KB output). Deferred v1.38.1: token-budget + force-compress hook wire, required-pack soft warn, active drift correction, B3b service IPC, A9b auto-trigger.
- **v1.37.0** - **Bash-replacement Phase 1+2 + C2 hot-path intent cache + caveman threshold lower**. **Phase 1**: 14 new icmg cmd wrappers (`build`/`sed`/`awk`/`jq`/`zip`/`sha256`/`env`/`gh`/`mkdir`/`rmdir`/`mv`/`rm`/`slice`/`date`/`wc`) — every common bash idiom now has an icmg equivalent with Tkil filter at the caller. **Phase 2**: `ICMG_NO_BASH=1` opt-in mode (`ID=25` leash) — when set, refuses any cmd not starting with `icmg `. Forces 100% icmg-surface usage. **C2 intent cache** (`intent_cache` + `intent_backfill_queue` tables via global-db migration v3): hot path = `SELECT BY PRIMARY KEY` + FNV-64 hash + regex fallback, **<2 ms p99 guaranteed**. LLM never invoked from hot path; backfill is async (v1.38 service tick). NEW `icmg intent` admin cmd (`classify` / `regex` / `stats` / `clear`). **Caveman threshold** lowered 800 → 500 chars for long-prose prompts (per "save tokens" goal). Scaffold migrations v4/v5: `amnesia_events` + `drift_corrections` (consumer code v1.37.1). Deferred v1.37.1+: A7 amnesia counter, force auto-compress, required-pack soft warn, active drift correction, token budget enforcement.
- **v1.36.0** - **Build-speed: mono test default ON** (~50 s saved per release cycle, **1.5 GB disk reclaimed**). Root cause of slow build identified: per-exe test model linked the entire `libicmg_lib.a` (50 MB, `--whole-archive` for static registry init) into each of 74 test exes — link bottleneck ~2.5 min, ~1.5 GB disk. Solution: flip `ICMG_MONO_TEST=ON` (was opt-in since v1.29.0). Single `icmg_test.exe` (24 MB) now collects all 874 TEST() cases and links `icmg_lib` once. CMake macro `add_icmg_test` guarded — when mono ON, only collects sources into `ICMG_ALL_TEST_SOURCES` global property; no per-exe `add_executable`. Per-exe model preserved as `-DICMG_PER_EXE_TEST=ON` opt-out (for test isolation debugging when singleton state leak suspected — v1.29.0 fixed `Scorer::reset` between cases). ctest gate: 1 test ("icmg_test") containing 874 cases; verified 874 passed 0 failed end-to-end. Net release cycle ~5 min → **~2 min**.
- **v1.35.0** - **Anti-amnesia Layer 3 + selective build**: NEW `rule_violations` table (migration 0027) + `RuleTelemetry` C++ module — every PreToolUse leash `block` records to global DB so violation counters persist across sessions. **R8 auto-pin** top-3 most-violated rules to top of UserPromptSubmit header (`ESCALATED RULES (you violated these recently — DO NOT REPEAT)`) — wired FIRST in concat order, AI cannot ignore. **R3** python -c one-liner block (`ID=24`) — forces use of `icmg run sed/perl` or native Edit. NEW `icmg rule-viol` cmd (record/stats/clear) for admin audit. **ship_cmd selective build**: (1) `doBuild` skip-fresh via pure C++ filesystem mtime walk over `src/` + `third_party/llama.cpp/{src,include}` — refuses rebuild when `build/icmg.exe` newer than every tracked .cpp/.hpp/.h/.cc; (2) `doTest` skip-when-docs-only — combines `git log --since=ship.build.ts` + `git status --porcelain` to detect non-doc changes; if only README/CHANGELOG/docs/.github/scripts/*.sh/*.md → skip ctest entirely. Overrides: `ICMG_SHIP_FORCE_BUILD=1`, `ICMG_SHIP_FORCE_TEST=1`. Deferred v1.36+: A7 amnesia counter, registry SHA256 fill, LLM carry-overs C2/B3b/A9b, targeted build (Strategi 2).
- **v1.32.1** - **B1 toolchain fix + Linux build restored**: opt-in LLM build (`-DICMG_USE_LLAMA=ON`) previously crashed MSYS2/MinGW c++.exe (`0xc0000139`) on `ggml-cpu/amx/mmq.cpp` + `binary-ops.cpp` under default GGML flags. Root cause: `GGML_NATIVE=ON` (`-march=native` AVX intrinsics) × `GGML_CCACHE=ON` (wrapper) combo on GCC 15.2. Fix: inside `if(ICMG_USE_LLAMA)` block, force-disable both with `CACHE FORCE`. Override via `-DGGML_NATIVE=ON` / `-DGGML_CCACHE=ON` for known-good toolchains. Default OFF build unchanged. Also: **Linux x64 tarball restored** to public release (v1.31.0 + v1.32.0 were Win-only; v1.32.1 ships both). 122/122 ctest Win + Linux WSL.
---

## 🚀 Quick start

1. **Download** the latest installer from the [Releases page](https://github.com/ncmonx/icm-graph/releases) — `icmg-<version>-win-x64.zip` for Windows, `icmg-<version>-linux-x64.tar.gz` for Linux.
2. **Extract** the archive into any folder of your choice.
3. **Add the folder to your `PATH`** so the `icmg` command is available everywhere.
4. **Open your project** in a terminal and run:

   ```text
   icmg init
   ```

   That's it. The next time you launch Claude Code (or Cursor / Cline / Windsurf — see below), Icemage will quietly start trimming tokens.

---

## 🧰 What you'll actually use day-to-day

After install, the only command most people type is `icmg init` once per project. Everything else happens automatically. A few useful commands when you want to peek under the hood:

| Want to | Type |
|---|---|
| See how much you saved this month | `icmg savings` |
| See a chart in the terminal | `icmg savings --ascii` |
| Recall a past decision in this project | `icmg recall "<question>"` |
| Recall something from another project | `icmg cross-recall "<question>"` |
| Wake-up briefing for a fresh session | `icmg wake-up` |
| Update Icemage in place | `icmg update --apply` |
| Health-check the install | `icmg doctor` |

For the full menu run `icmg --help`.

---

## 🤖 Works with

- **Claude Code** (primary target — best-tested)
- **Cursor** — drop-in via the same hooks
- **Cline**, **Windsurf**, **OpenCode** — same approach, may need a small config nudge
- **Anything that exposes hooks or MCP** — the MCP server bundled with Icemage is reusable

---

## 🛡️ Safety + privacy

- **100 % local.** Everything Icemage knows about your projects lives in a small SQLite database next to your code. Nothing is sent to a remote server — not the project name, not the file paths, not the recalled snippets.
- **No telemetry.** Icemage doesn't phone home.
- **Open source.** [Apache-2.0](LICENSE). Audit the binary, the release notes, and the file structure freely. Source code is held privately to keep the bug surface manageable for a solo maintainer — public reports + private fixes is the operating model.
- **Tamper-evident.** Every release ships with a `sha256` sidecar so you can verify the binary you downloaded.

---

## 🩹 Honest limits

- **Windows + Linux only** for prebuilt binaries today. macOS users currently need to wait for a self-hosted runner build (planned).
- **First-time install on Windows with strict antivirus** can be slow until you let Icemage run once. After that it's fast.
- **Not a replacement for the AI.** Icemage is a token-trimming layer — it doesn't write code for you and it doesn't make a bad AI smart.

---

## 💖 Support

If Icemage saved you a few hours or a few dollars and you want to send a small thank-you, both routes work:

- [GitHub Sponsors](https://github.com/sponsors/ncmonx)
- [Ko-fi tip jar](https://ko-fi.com/ncmonx)

All revenue goes straight into more releases — there is no team behind this, just one maintainer and a long backlog of "make AI agents less wasteful" ideas.

---

## ❓ FAQ

**Does Icemage send my code anywhere?**
No. Everything is local. The only network call is when you ask Icemage to update itself or fetch a URL through `icmg fetch`.

**Can my company use it?**
Yes — Apache-2.0 licensed, free for any use including commercial. If you want a private support arrangement or a custom build, [open a sponsorship](https://github.com/sponsors/ncmonx).

**Why is the source code repo private?**
One maintainer, no security team. Public bug reports + private fixes lets me ship hotfixes the same day without telegraphing exploitable details. The release binaries and reproducible build hash are still public.

**Does it slow my AI down?**
No. Trimming happens *before* the AI reads anything, so the AI sees a smaller, cleaner version of the same context. End-to-end interactions get faster, not slower.

**Where are the savings stored?**
In `.icmg/data.db` inside each project (small SQLite file). Run `icmg savings` to see the breakdown.

**How do I report a bug or ask for a feature?**
Open an issue at the [GitHub issues](https://github.com/ncmonx/icm-graph/issues) page. Real-world reproductions with `icmg savings --json` attached get triaged fastest.

---

## 🌟 Star history

<a href="https://star-history.com/#ncmonx/icm-graph&Date">
  <img src="https://api.star-history.com/svg?repos=ncmonx/icm-graph&type=Date" alt="Star history" width="600"/>
</a>

---

## 📜 License

[Apache-2.0](LICENSE).

---

## 📚 Other docs

- [CHANGELOG.md](CHANGELOG.md) — full version history
- [SECURITY.md](SECURITY.md) — vulnerability reporting
- [NOTICE](NOTICE) — third-party attributions
