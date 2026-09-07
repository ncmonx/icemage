<p align="center">
  <img src="assets/logo.svg" alt="Icemage" width="160"/>
</p>

# Icemage <sub><sup>(`icmg`)</sup></sub>

[![release](https://img.shields.io/github/v/release/ncmonx/icemage)](https://github.com/ncmonx/icemage/releases)
[![downloads](https://img.shields.io/github/downloads/ncmonx/icemage/total)](https://github.com/ncmonx/icemage/releases)
[![last-commit](https://img.shields.io/github/last-commit/ncmonx/icemage)](https://github.com/ncmonx/icemage/commits/main)
[![tests](https://img.shields.io/badge/tests-2462%2F2462%20passing-brightgreen)](#)
[![mcp tools](https://img.shields.io/badge/MCP%20tools-43-blueviolet)](#)
[![commands](https://img.shields.io/badge/CLI%20commands-260%2B-blue)](#)
[![license](https://img.shields.io/badge/license-Elastic--2.0-blue.svg)](LICENSE)
[![OpenSSF Scorecard](https://api.securityscorecards.dev/projects/github.com/ncmonx/icemage/badge)](https://securityscorecards.dev/viewer/?uri=github.com/ncmonx/icemage)
[![sponsor](https://img.shields.io/badge/sponsor-GitHub-ea4aaa?logo=github-sponsors)](https://github.com/sponsors/ncmonx)
[![ko-fi](https://img.shields.io/badge/Ko--fi-tip-ff5e5b?logo=ko-fi)](https://ko-fi.com/ncmonx)

> **Stop burning tokens. Stop losing context. Ship faster.**

A single binary that makes Claude Code, Cursor, and every other AI coding assistant **70–90% cheaper** to run — without dumbing them down.

If you've ever watched 30K tokens evaporate on a single file read, paid for "thinking" you didn't need, or re-explained the same project context after `/clear` for the fifth time today — this is for you.

<p align="center">
  <!-- 30-second demo. Regenerate: `vhs assets/demo.tape` (see assets/demo.tape header), commit assets/demo.gif, then uncomment the <img> below. -->
  <!-- <img src="assets/demo.gif" alt="icmg in action — savings, one-shot find, slim context" width="760"/> -->
</p>

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
| Cost per AI session | **down 70 – 90 %** vs. raw | up to 95 % | — |

## ✨ What's new

- **v2.23.0** — **Token-killer pack: deep-forget, dangling guard, schemas-on-demand.** Three deterministic, zero-LLM features from the 2026-09 research pass. **`icmg forget <id> --deep`** — forgetting soft-deletes ONE node, but its content typically leaked into derived artifacts (session snapshots, wflog entries, consolidated notes); `--deep` scans live nodes for residue by directional *containment* (symmetric Jaccard under-scores a long snapshot holding a verbatim secret) — flag-only, never auto-deletes. **Dangling-reference guard** in `shrink --kind salience` — per-line selection can keep a line *using* an identifier while dropping the line *defining* it (34–60% of compressed outputs in the arXiv 2608.04569 benchmark); a post-pass pulls back first-mention lines of entities the kept lines reference (`--no-dangling-guard` opts out). **`icmg_tool_search` MCP meta-tool** — with `ICMG_MCP_PROFILE=core` the host sees 11 tools instead of 43 full schemas saturating the context window before the first query; the meta-tool ranks ALL registered tools by capability query and returns full `inputSchema` per hit, schemas-on-demand, hidden tools stay callable. 15 new tests — **2462/2462 tests ✓.**
- **v2.22.0** — **Brain v2.22: the memory learns from its own usage.** Four deterministic, zero-LLM memory upgrades from the 2026-08 research pass (Zep/Graphiti temporal-KG, Mem0, MemoryOS, coarse-to-fine grounded memory). **Time-travel recall** — `recall --as-of T` (epoch, `7d`/`24h`/`30m` ago, or `YYYY-MM-DD`) answers *what did we believe at T?* over the existing bi-temporal columns: since-superseded facts reappear before their `invalidated_at`, facts not yet valid stay hidden, and the query is side-effect-free (no cache, no frequency bump). **Retrieval-failure ledger** — `memory-health --gaps` turns recall queries that came back empty into a knowledge-gap checklist: the agent asked, the brain had nothing — most-asked first, noise skipped, `--json` for tooling. **Quick-note promotion by heat** — `memory-consolidate --promote-quick` gives `quick:<epoch>` captures the agent kept recalling (≥ 3 recalls) a permanent searchable `hot:*` topic while cold ones age out via decay. **Coarse-to-fine recall** — oversized result sets keep the strongest hits full-bodied and collapse the tail to 1-line index rows (`--get <id>` fetches detail; `--full` opts out). 23 new tests — **2447/2447 tests ✓.**
- **v2.21.1** — **Multi-daemon spam fix — dozens of idle `icmg.exe` no more.** On busy multi-user servers, concurrent hook calls raced `ensureDaemon()`: every spawned rule-daemon created the *same* named pipe successfully (`PIPE_UNLIMITED_INSTANCES`, no `FILE_FLAG_FIRST_PIPE_INSTANCE`, no cross-process lock) and then parked in `ConnectNamedPipe` forever — accumulating dozens of ~2-3 MB `icmg.exe` processes per user. Fixed belt-and-braces: `RuleDaemon::acquireSingleton()` per-user cross-process lock (Windows: named mutex `Local\icmg-rule-daemon-<user>`; POSIX: `flock` on `~/.icmg/rule-daemon.lock`) required by `run()` — the losing daemon exits quietly — plus `FILE_FLAG_FIRST_PIPE_INSTANCE` so a raced second daemon can never share the pipe even if the mutex path regresses. One-time cleanup on affected servers: `taskkill /F /IM icmg.exe`, then update. **2419/2419 tests ✓.**
- **v2.21.0** — **Brain + token trio: session-aware recall delta, contradiction sentinel, adaptive recall depth.** When the session TTL dedup suppresses memory nodes you already saw this session, `recall` now emits a single stdout line `[N prior memories still apply: #ids]` instead of silently dropping the reference — the agent keeps the pointer without re-paying the tokens for full bodies. New `icmg memory-health --contradictions` scans the memory store for node pairs that overlap heavily (Jaccard ≥ 0.6 default, `--jaccard-min` to tune) yet disagree — a negation marker on one side or a conflicting `key=value` fact; flag-only (never deletes), strongest-first, capped at `--max` (default 25), each hit suggesting the existing bi-temporal fix `icmg memory invalidate <old> --by <new>`. Verified live on a 31k-node store: caught real `icmg_version` and `prefix` conflicts at 100% overlap. And `recall --adaptive` sizes recall depth with the deterministic v2.20 intent classifier — simple task = 3 results, unknown = 7, complex = 12; an explicit `--limit` always wins. All deterministic, no LLM. **2416/2416 tests ✓.**
- **v2.20.0** — **Model-era capability pack.** As frontier models grew 1M-token windows and extended-thinking budgets, the token lever shifted from *how much to trim* toward *cache-hit rate* and *reasoning-token* cost. Five shipped: `pack --cache-aware` classifies sections (conventions/rules/graph/files = stable; task/recall/diff = volatile), orders stable-first, and wraps ONLY the byte-stable prefix (FNV-1a `prefix_hash` makes drift visible) so prompt caching (-90% cost / -85% latency) actually hits. MCP tool annotations — every tools/list entry now carries `readOnlyHint`/`destructiveHint`/`idempotentHint`/`openWorldHint` so an agent host can plan safely. New `icmg_graph_query` MCP tool: deterministic multi-hop structural search (`blast_radius` | `who_calls` | `path_between`). `pack --effort-hint` recommends an extended-thinking budget from task intent + graph fan-out. `token-ledger stats`/`otel` report cache-hit ratio + honest cost estimate, OpenTelemetry GenAI-style JSON offline. **2406/2406 tests ✓.**
```bash
curl -fsSL https://raw.githubusercontent.com/ncmonx/icemage/main/scripts/install.sh | sh
```

**One line — Windows (PowerShell):**

```powershell
irm https://raw.githubusercontent.com/ncmonx/icemage/main/scripts/install.ps1 | iex
```

The installer grabs the latest release, verifies its SHA-256, and drops `icmg` into your bin dir (`~/.local/bin` on Linux/macOS, `%USERPROFILE%\bin` on Windows). Pin a version with `ICMG_VERSION=2.1.0`, or change where it lands with `ICMG_BIN_DIR`.

<details>
<summary>Prefer a manual download?</summary>

1. **Download** the latest archive from the [Releases page](https://github.com/ncmonx/icemage/releases) — `icmg-<version>-win-x64.zip` for Windows, `icmg-<version>-linux-x64.tar.gz` for Linux, `icmg-<version>-macos-arm64.tar.gz` for macOS.
2. **Extract** it into any folder.
3. **Add that folder to your `PATH`** so `icmg` is available everywhere.

</details>

Then, in your project terminal:

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
- **Open source.** [Elastic License 2.0](LICENSE) - **source-available**. Free to use, copy, modify,
and self-host. The one limitation: you may not offer icmg to third parties as a
hosted or managed service. Everything else is fair game. Audit the binary, the release notes, and the file structure freely. Source code is held privately to keep the bug surface manageable for a solo maintainer — public reports + private fixes is the operating model.
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
Yes - [Elastic License 2.0](LICENSE): source-available, free for any use including commercial, self-hosting, and modification. The only limit is reselling icmg itself as a hosted/managed service. Want a private support arrangement or custom build? [Open a sponsorship](https://github.com/sponsors/ncmonx).

**Why is the source code repo private?**
One maintainer, no security team. Public bug reports + private fixes lets me ship hotfixes the same day without telegraphing exploitable details. The release binaries and reproducible build hash are still public.

**Does it slow my AI down?**
No. Trimming happens *before* the AI reads anything, so the AI sees a smaller, cleaner version of the same context. End-to-end interactions get faster, not slower.

**Where are the savings stored?**
In `.icmg/data.db` inside each project (small SQLite file). Run `icmg savings` to see the breakdown.

**How do I report a bug or ask for a feature?**
Open an issue at the [GitHub issues](https://github.com/ncmonx/icemage/issues) page. Real-world reproductions with `icmg savings --json` attached get triaged fastest.

---

## 🌟 Star history

<a href="https://star-history.com/#ncmonx/icemage&Date">
  <img src="https://api.star-history.com/svg?repos=ncmonx/icemage&type=Date" alt="Star history" width="600"/>
</a>

---

## 📜 License

[Elastic License 2.0](LICENSE) - **source-available**. Free to use, copy, modify,
and self-host. The one limitation: you may not offer icmg to third parties as a
hosted or managed service. Everything else is fair game.

---

## 📚 Other docs

- [CHANGELOG.md](CHANGELOG.md) — full version history
- [SECURITY.md](SECURITY.md) — vulnerability reporting
- [NOTICE](NOTICE) — third-party attributions
