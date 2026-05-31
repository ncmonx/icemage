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

**40 MCP tools · 1254/1254 tests · single-binary · 100 % local · pure-bash hooks** (zero Python/jq dependency).

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
| **Semantic atom recall** (`recall --atoms`) | **fact-level hits, not blobs** | — | v1.79.0 |
| **Auto-bisect** (`icmg bisect`) | **first-bad commit in ~log2(N) tests** | — | v1.84.0 |
| Past-chat full-text search | **< 10 ms** across months | — | v1.21.7 |
| Graph symbol lookup | **256-slot in-RAM cache** | — | v1.21.8 |
| First-prompt warmup | < 1 s | — | v1.18 |
| **Cold build time** (icmg itself) | **~50 % faster** (20 min → 9-10 min) | — | v1.26.0 |
| **MCP response filter** (verbose plugins) | **50 – 80 % smaller** | up to 90 % | v1.30.0 |
| **Auto-thinking suppress** (trivial prompts) | **~1500 tok / call saved** | — | v1.30.0 |
| **Sayless-auto** (long-prose replies) | **60 – 75 % compress** | up to 85 % | v1.30.0 |
| **Service auto-start** (UserPromptSubmit) | **0-touch warm-up** | — | v1.30.0 |
| **Path ambiguity warning** (icmg context) | wrong-file lookups → loud | — | v1.29.0 |
| **rg-wrapper + brace glob** (icmg grep/files) | flag-mirror, **{a,b}** expand | — | v1.29.0 |
| **Local AI model** (built-in, opt-in) | **0 cloud calls** | privacy-first | v1.31.0 |
| **Smart router** (REGEX vs LLM_LOCAL vs CACHE) | **<100 us p99** | hot-path forced regex | v1.31.0 |
| **HTTP streaming download** (model fetch + SHA256) | **400 MB - 2 GB** safe-verify | tamper-detect | v1.31.0 |
| **icmg git** wrapper (single ergonomic entry) | **Tkil-filtered** + safety-gated | enforces icmg-FIRST | v1.31.0 |
| **Python-free core** (PRECOMPACT_PY dropped) | **-200-500 ms** boot saved | single-binary | v1.31.0 |
| **pack --rerank** (LLM-reorder memory hits) | **opt-in** warm-path | router-gated | v1.32.0 |
| **PreCompact LLM summary** (warm-pool Qwen 0.5B) | **<15 s** cold | regex fallback always | v1.32.0 |
| **icmg compact-bg** (proactive memory worker) | **<3 s** warm | manual + future hook | v1.32.0 |
| **Smarter local AI memory** | **multi-prompt safe** | no overflow | v1.32.0 |
| **Code graph viz + report** (`icmg graph viz`) | **interactive D3 + god-nodes** | — | v1.71.0 |
| **Secret scanner** (`icmg scan`) | **21 detectors, CI-gate** | redact-by-default | v1.68.0 |
| **MCP server hardening** (token + rate-limit + path-guard) | **abuse / RCE-safe** | — | v1.72.0 |
| **Post-compact memory re-anchor** | **rules survive compaction** | auto on `init` | v1.73.0 |
| **Scripted-safe `icmg run`** (non-interactive guard) | **no hang on destructive** | `--yes`/env opt-in | v1.74.0 |
| **Clean self-upgrade** (idempotent Defender step) | **no phantom B: drive popup** | `--no-defender` opt-out | v1.75.0 |
| **Encryption-at-rest** (`icmg encrypt`, SQLCipher AES-256) | **opt-in full-DB encrypt** | BM25 recall intact | v1.76.0 |
| **Hot recall cache** (RAM, daemon-shared) | **< 5 ms repeat recall** | self-governing RAM | v1.77.0 |
| **Init + upgrade hardening** (no 30-min hang, no stale-proc lock) | **`init` returns immediately** | detached imports + lock-guard | v1.78.4 |
| Cost per AI session | **down 70 – 90 %** vs. raw | up to 95 % | — |

Measured on real-world sessions. Your mileage will vary with project size and habits — anyone running a busy AI agent for a day already sees meaningful savings.
---

## ✨ What's new

- **v1.86.0** - **Compiler cache is now permanent: rebuilds are 50-80% faster out of the box**. The MSVC build previously relied on `ccache`, which ships ABI-broken in many MSYS2 installs (crashes with `0xC0000139` and gets silently skipped, so every build recompiled from scratch). v1.86 makes **sccache** (Mozilla's Rust-based compiler cache, MSVC-native) the primary cache: installed to a stable `C:/Tools/sccache`, on the user PATH, with a persistent 10 GB cache dir, and detected first by CMake (ccache stays as fallback only). Incremental rebuilds now reuse cached object files across sessions instead of recompiling the whole tree. Full automated suite passes (1254 checks).
- **v1.86.0** - **`icmg memory cache stats` gets a JSON mode and honest hit-rate reporting**. The recall-cache stats command now goes through a unified cache-metrics normalizer: hit-rate shows `n/a` when there have been no requests yet (instead of a misleading 0.0%), and a new `--json` flag emits machine-readable output for dashboards and scripts. First of the v1.84 token-efficiency primitives wired into a live command surface. Full automated suite passes (1254 checks).
- **v1.84.0** - **Token-efficiency mega bundle: faster cache, smarter truncation, backend routing**. Nine internal optimizations adopted from production CLI tools (claude-code, openclaude): (1) djb2-hashed LRU cache keys (~100x faster than SHA256 lookups); (2) query fingerprinting to skip duplicate recalls within a session; (3) pre-flight token estimation (bytes/4) so oversized payloads are caught before work; (4) **microcompaction** - a new Stage 0 in the token-kill pipeline that surgically keeps the last 40 KB of giant command outputs instead of dropping or truncating blindly; (5) a diminishing-returns loop guard that stops iterative work once it plateaus; (6) cache-break detection via state hashing; (7) write coalescing to batch DB writes; (8) a latency/reliability/cost backend scorer for embedding-backend selection; (9) a unified cache-metrics normalizer for honest cross-layer hit-rate reporting. All additive and opt-in - default behavior unchanged. Full automated suite passes (1254 checks, +40).
- **v1.83.0** - **`icmg init` now registers your project automatically, and cross-project memory recall is always ready**. Previously you had to run `icmg project add <name> <path>` manually before cross-project recall would surface results from other projects. v1.83 wires project registration directly into `icmg init`: after bootstrapping a new project, it checks the global registry and, if the current directory is not yet registered, adds it automatically — no extra command, no admin rights, best-effort so init never fails. Combined with the existing `icmg recall --all-projects` and `icmg cross-recall`, this means that as soon as you initialize a project, its memory is available to every other icmg-aware session on the same machine. Full automated suite passes (1214 checks).
- **v1.82.0** - **`icmg cron add/list/remove` replaces Windows Task Scheduler per-job, and the background daemon now runs cron jobs automatically**. Previously, scheduled tasks (like weekly memory hygiene) required registering entries in Windows Task Scheduler via `icmg cron install` — one OS task per job, requiring admin rights, not portable across machines. v1.82 replaces this with a SQLite-backed scheduler: `icmg cron add "memory prune-old" --every 60` registers the job in `~/.icmg/global.db` without touching the OS; `icmg cron list` and `icmg cron remove` manage the registry. The background daemon now drains due jobs automatically every 60 seconds — no Task Scheduler entry needed per job (only the daemon itself needs one autostart entry). Jobs are executed as argv arrays (no shell), and any chore containing shell metacharacters is rejected at both registration and execution time. Full automated suite passes (1211 checks).

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
