<p align="center">
  <img src="assets/logo.svg" alt="Icemage" width="160"/>
</p>

# Icemage <sub><sup>(`icmg`)</sup></sub>

[![release](https://img.shields.io/github/v/release/ncmonx/icemage)](https://github.com/ncmonx/icemage/releases)
[![downloads](https://img.shields.io/github/downloads/ncmonx/icemage/total)](https://github.com/ncmonx/icemage/releases)
[![last-commit](https://img.shields.io/github/last-commit/ncmonx/icemage)](https://github.com/ncmonx/icemage/commits/main)
[![tests](https://img.shields.io/badge/tests-1925%2F1925%20passing-brightgreen)](#)
[![mcp tools](https://img.shields.io/badge/MCP%20tools-41-blueviolet)](#)
[![commands](https://img.shields.io/badge/CLI%20commands-95%2B-blue)](#)
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

- **v2.8.0** - **Three KV-cache-aware token-saving wins: icmg now tells you when your prompt stops caching, keeps its tool list stable, and turns spilled output into an actionable pointer.** Grounded in the context-engineering finding that KV-cache hit rate is the #1 cost lever (cached input is billed about 10% of fresh), this release closes the measure-optimize-measure loop. A new **cache-hit advisor** (`icmg savings --cache-advisor`) reads the token ledger as a *trend* -- it splits per-turn samples into a prior vs recent half and compares the mean hit-rate, so a meaningful drop flags that **volatile content leaked into the cached prefix** (a per-turn timestamp, a memory-inject that changes every turn) and is busting the cache, with the concrete fix (move volatile content to the end of the user turn, keep the prefix append-only). It auto-prints on a degrading verdict; on real data it reports things like `cache-hit stable at 94%, prefix consistent`. Second, a **stable tool list**: `Registry::keys()` iterated an unordered map, so its order varied between builds -- and because the MCP server advertises that tool list every connection (and those definitions sit in the AI client's cached prompt prefix), a reshuffled order silently busts the client's KV-cache; `keys()` is now sorted once, so every enumeration is stable by default. Third, **filesystem-as-context**: when output exceeds the byte budget it spills to a temp file, but the footer used to be a dead end (`N bytes spilled to <path>`); it is now a **just-in-time pointer** that names the path, states the full line count, and tells you exactly how to retrieve it (`Read <path>, use offset/limit to page`), so a large observation lives on disk while the context holds only a cheap, actionable reference. Full automated suite passes (1925 checks).
- **v2.7.0** - **Find files faster, see skills in full, and a thinking-compression directive that no longer reads as "think less".** `icmg find` gains **`--name <partial>`**, a fuzzy *filename* locator that walks paths only (no file bodies read) and ranks by a tiered score (exact stem > basename prefix > substring > path substring > in-order subsequence; shortest-name tiebreak; case-insensitive) -- about **2.6x faster** than the content search in a debug build, and more on larger trees. Add **`--open`** to also print the top match inline (numbered, byte-capped) so locate-and-read is one turn, or **`--recent`** to rank newest-modified first; a path-encoding fix also clears a real Windows crash (`error 1113`) when the walk meets a file whose name has non-codepage characters. Skills are **no longer truncated** -- indexing kept only the description plus the first 500 characters in a skill's context node, so reading it back via `icmg context skill-<name>` showed a cut-off body; it now stores the full skill text. `graph-symbol` now advertises the **partial/fuzzy name match** it already supported (`FindComm` -> `FindCommand`). And the **sayless thinking directive** was reworded everywhere it is injected: it used to say "skip thinking entirely" / "refuse to expand reasoning" with hard word caps, which inverted the intent -- it now says reason as deeply as the problem needs and just *compress the wording* (symbols/abbreviations/fragments): compression, not less thinking. Full automated suite passes (1889 checks).
- **v2.6.0** - **`icmg compress` now learns from itself — a self-improving, self-pruning compression vocabulary (Adaptive Output Gate).** Compression used to rediscover its glossary from scratch on every call, so a long path or identifier that appeared only once in a given input was never abbreviated. icmg now keeps a cross-session **learned glossary**: every compression records which alias→phrase mappings actually recur, with a hit count and the tokens they saved. The next compress **seeds itself** from that vocabulary, so proven-valuable phrases get abbreviated even when they appear just once — a frequency the per-call scan would skip — and round-trips stay exactly reversible (`icmg expand`), with `--no-seed` to opt out. Crucially the vocabulary stays *relevant*, not just large: entries are ranked by a **recency half-life** so a phrase you used today outranks a stale high-savings one from months ago, and a periodic **prune** forgets dead weight (old and rarely-hit) so the table can't bloat — the same recency-weighted, forgetting model icmg's memory recall already uses. Plus a **compress-before-cap** output gate (`icmg context --gate`): when a bundle would exceed the byte cap, it emits a lossless glossary-compressed form instead of a lossy truncation, so nothing is silently dropped. Full automated suite passes (1871 checks).
- **v2.5.1** - **`icmg verify --help` works from any directory — no `.icmg/` required, plus a cross-platform shell fix for `verify --command`.** `icmg verify` opened the project SQLite database before it checked for `--help`, so `icmg verify --help` — and any invocation from a directory without a `.icmg/` project (a fresh checkout, or a `ctest` run from `build/`) — failed with `unable to open database file` instead of printing usage. The `--help` early-exit now runs *above* the database constructor. The same command's `--command` runner also hardcoded an `sh -c` shell that isn't on the Windows PATH; it now routes through a cross-platform shell dispatcher (bash on MSYS, PowerShell/cmd.exe on Windows, `/bin/sh` on POSIX). The regression test is hardened with an RAII guard that redirects the project DB to a local file and pre-creates its schema, so the suite no longer depends on ambient working-directory state. Full automated suite passes (1857 checks).
- **v2.5.0** - **Memory recall grew up: citable, time-aware, and disclosure-first — plus a privacy redactor and a store that tags itself.** A batch of recall/memory UX upgrades. `icmg recall` now prints **citable** results by default (`[score] #id <icon> topic`) so you can reference a specific memory by id, and a typed icon tells decision/fix/note apart at a glance. New views: `recall --index` then `recall --get <id>` for **progressive disclosure** (skim a compact index, then pull the full node only when you need it — a real context saver), `recall --timeline` for a chronological day-grouped view (newest first), and `recall --by file` to bucket memories by the source file they mention. `store` gained two safeguards: a `<private>…</private>` **redactor** that strips secrets before they are ever persisted, and **automatic keyword derivation** (stopword-filtered, deduped, capped) when you omit `--kw`, so recall stays sharp without manual tagging. `wake-up` now shows a typed-icon legend, per-decision icons, and a token-cost footer. And two operability wins: `icmg serve` exposes `GET /api/health` (status / uptime / db / counts / version JSON) and `icmg install` does **smart version caching** — it skips a reinstall when the running and installed versions already match (`--force` to override, `--status` to inspect). Full automated suite passes (1818 checks).
- **v2.4.2** - **`icmg run` now works on plain Windows (no git-bash) — plus graph-precision and store-latency fixes.** The headline: `icmg run "<command>"` was completely broken on Windows machines without git-bash/MSYS, returning `CreateProcess failed: 2` for shell lines and bare built-ins/cmdlets. It relied on `CreateProcess`'s PATH search, which misses PowerShell under `System32\WindowsPowerShell\v1.0` and the Store `pwsh` alias; it now resolves the shell to a full absolute path and falls back through PowerShell for builtins/cmdlets, so an entire platform configuration goes from broken to working. Two more: the code graph no longer creates dozens of false `calls` edges when a callee name is defined in many files (common names like `run`/`get`/`value` used to fan out and inflate unrelated files' PageRank) — a name resolving to more than a few definitions is now left unlinked. And the memory-store duplicate check, which scanned the *entire* corpus on every `store` (O(N) word-set Jaccard), is bounded to the most-recent candidates plus a hoisted tokenizer and a subprocess-free git-sha read, so store latency stops growing with corpus size. Full automated suite passes (1763 checks).
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
