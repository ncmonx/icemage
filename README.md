<p align="center">
  <img src="assets/logo.svg" alt="Icemage" width="160"/>
</p>

# Icemage <sub><sup>(`icmg`)</sup></sub>

[![release](https://img.shields.io/github/v/release/ncmonx/icemage)](https://github.com/ncmonx/icemage/releases)
[![downloads](https://img.shields.io/github/downloads/ncmonx/icemage/total)](https://github.com/ncmonx/icemage/releases)
[![last-commit](https://img.shields.io/github/last-commit/ncmonx/icemage)](https://github.com/ncmonx/icemage/commits/main)
[![tests](https://img.shields.io/badge/tests-2298%2F2298%20passing-brightgreen)](#)
[![mcp tools](https://img.shields.io/badge/MCP%20tools-41-blueviolet)](#)
[![commands](https://img.shields.io/badge/CLI%20commands-200%2B-blue)](#)
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

- **v2.15.2** — **Search-accuracy hardening part 2: 2 more root-cause fixes, plus a stale-headline-numbers correction.** Continuing the v2.15.1 telemetry investigation: `powershell -File <temp>.ps1` wrappers (the icemage-code agent harness's shape) never got unwrapped either (3511 production invocations, only ~38% filtered) — fixed with a best-effort read of the script's content at classification time, safe because `detect()` runs before execution and degrades silently if the file is gone. `GitFilter`'s commit-hash regex required 8-40 hex chars, but `git log --oneline`'s own default short-hash is 7 chars — the regex never matched, so the 30-entry truncation cap was structurally unreachable (one call emitted 76,551 raw bytes, 0% filtered). Fixed with a one-character regex change. Both filters had **zero prior test coverage**; `GitFilter` now has 3. Also corrected a **long-standing stale headline number** unrelated to the fixes above — the CLI-commands badge said "95+" while 200 are actually registered — now part of the standing release docs-sync gate. **2298/2298 tests ✓.**
- **v2.15.1** — **Search-accuracy hardening: 5 root-cause fixes found via production telemetry, not speculative research.** Hybrid recall's BM25 normalization used a rank-POSITION fallback instead of the real `bm25_score` magnitude, distorting close/wide relevance gaps alike — fixed via `Scorer::normalizeMinMax()`. Added an **entity-linking recall boost**: `extractEntities()` tagged memory keywords at capture time but recall never cross-referenced query entities against them — now a query naming the same URL/env-var/@mention gets a deterministic top-up. The documented `--fuzzy` CLI flag was a literal no-op (`bool /*fuzzy*/`) — now wired to a real Levenshtein-bounded fallback that only fires when exact BM25 comes back empty. Found via a dashboard anomaly: Tkil's own filters bounded output by LINE COUNT only, never byte size — a single pathological command (one giant line, no newlines) sailed through 100% unfiltered (472MB raw, 0% saved, confirmed in production telemetry); fixed with a universal 2MiB `capRawBytes()` gate inside `splitLines()`, the one function all 19 filters call first. Also found: commands routed through a shell wrapper (`bash -c "..."`) never matched any classifier pattern (2206 production invocations, 1.2% filtered) — `Detector::detect()` now unwraps `bash`/`sh`/`zsh`/`dash -c` wrappers (incl. an `export VAR=...;` preamble) before classifying. All TDD (RED confirmed before each fix), zero-LLM, deterministic. **2291/2291 tests ✓.**
- **v2.15.0** — **2026 feature-research backlog: seven deterministic (no-LLM) memory & agent features.** `icmg emit-agents-md` syncs an icmg-first routing block into `AGENTS.md` so non-Claude agents (Cursor, Copilot, Codex, Aider, Gemini CLI) inherit icmg-first behavior. **Bi-temporal fact invalidation** — facts carry `valid_from`/`invalidated_at`; a superseded fact is kept for history but excluded from recall (`icmg memory invalidate`). **Causal-fact retrieval** — typed causal edges + a 1-hop recall expansion over BM25 surface a cause that doesn't lexically match the query (`icmg memory link`, `recall --causal`). **Two-tier recall scheduling** — cheap BM25 by default, escalate to the ~5-6s semantic tier only for hard queries (`recall --auto-tier`). **Pre-flight prompt rewrite** — `icmg agent --rewrite` honesty-gated context compression with the system prompt + task protected verbatim. `icmg compress-prompt` exposes that honesty-gated salience compressor as a first-class op. `icmg skill-bank` distills successful command trajectories into a bounded, reusable skill bank with success-rate attribution. Plus an `ICMG_NO_DAEMON` escape hatch fixing a recall hang on a half-open daemon pipe. **Full suite 2260/2260 pass.**
- **v2.14.0** — **Token-efficiency arsenal: seven new levers to cut recurring spend.** `icmg run --nano` collapses build/test/lint diagnostics to one dense `file:kind:code:line` per entry (gcc/clang/rustc + MSVC), ~95% on repeat builds. `icmg run --gist` gives a one-line, domain-aware TL;DR of command output (`12 passed, 3 failed. first fail: user.rs:45`), sub-millisecond, no LLM. `icmg learn` mines the persistent `commands` table across sessions to flag consistently-noisy commands and recommend a tighter mode; `ICMG_AUTO_ROUTE=1` then auto-applies `--nano`/`--gist` to them (opt-in). `icmg transcript cost` quantifies re-send amplification — an entry at position *i* of *N* is paid for *(N−i)* times — and ranks the hotspots to compact first. `icmg mcp audit` measures the per-turn token cost of every MCP tool schema and flags oversized ones. Recall gains an opt-in hot/warm/cold tier tie-breaker (`ICMG_RECALL_TIER_BOOST`) implemented as a stable tie-breaker, so it provably cannot regress well-separated rankings. **+34 tests, ctest 100% (Windows).**
- **v2.13.2** — **Fix: err126 root cause on Windows Server (clock_cast → icu.dll).** `std::chrono::clock_cast` pulled in `icu.dll`, which is absent on stock Windows Server 2019; two call sites (`bundle_cmd`, `graph_cmd`) now use direct epoch arithmetic in `file_time.hpp`. Added a throw-site diagnostic (`throw_site.hpp/cpp`) and a `doctor` write-probe for faster future triage. **1331 tests ✓.**
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
