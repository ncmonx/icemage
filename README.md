<p align="center">
  <img src="assets/logo.svg" alt="Icemage" width="160"/>
</p>

# Icemage <sub><sup>(`icmg`)</sup></sub>

[![release](https://img.shields.io/github/v/release/ncmonx/icemage)](https://github.com/ncmonx/icemage/releases)
[![downloads](https://img.shields.io/github/downloads/ncmonx/icemage/total)](https://github.com/ncmonx/icemage/releases)
[![last-commit](https://img.shields.io/github/last-commit/ncmonx/icemage)](https://github.com/ncmonx/icemage/commits/main)
[![tests](https://img.shields.io/badge/tests-1718%2F1718%20passing-brightgreen)](#)
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

- **v2.3.0** - **Four more languages with real symbol extraction, plus ground-truth observability.** Kotlin, Swift, Ruby, and Scala now get first-class *symbol* extraction (classes/objects/traits/interfaces/enums/functions + base types), not just import edges — via lean regex extractors in the proven csharp/sql style, so there is zero grammar bloat (Kotlin's tree-sitter grammar alone is 22-34 MB; Swift ships no committed parser.c). `icmg whereami` prints one authoritative snapshot — running binary + version, the config file actually in use, and project/global/persona DB paths — so a stale binary or wrong-path guess is caught in one command instead of a debugging detour. And a config-path fix: `set`/`get` and `unset`/`edit`/`zone`/`list` now agree on the global config location (`%APPDATA%/icmg` on Windows), with `icmg config path` to show it. Full automated suite passes (1718 checks).
- **v2.2.0** - **The discipline trilogy — positive-side enforcement that makes the model use the toolkit, not just avoid native calls.** icmg's enforcement was all defensive (block raw `Read`/`grep`, redirect to icmg); this adds the missing positive half — three gates that surround a work cycle, all auto-wired by `icmg init`, each with an opt-out env. `icmg recall-gate` (PRE-task) denies the first `Edit`/`Write` of a *complex* task until an `icmg recall`/`pack` has run this turn, killing the "edit a subsystem blind" anti-pattern — simple one-liners never block. `icmg ritual` (POST-change) is a Stop-hook gate that emits a hard `decision:block` when you've edited code but skipped the post-change sync (`store` + `wflog`), so the project memory never silently goes stale — replacing a soft reminder the model could ignore. `icmg discipline` (VISIBILITY) is a per-session feature-coverage scorecard, and `icmg discipline report` closes the loop by pairing coverage with gate-firing telemetry (reusing the existing rule-viol store) so you can *see* the discipline working — fewer blocks over time means it's landing. Full automated suite passes (1706 checks).
- **v2.1.0** - **One-turn code search, a visible context budget, one-line install, and 6 more languages.** `icmg find "<intent>"` is a one-shot multi-file semantic search — IDF-ranked so an exact identifier beats common words — returning just the relevant line windows from the right files and collapsing a Read→Grep→Read chain into a single turn. `icmg statusline` (auto-wired by `icmg init`) puts the per-model-honest context budget in Claude Code's status bar every turn — the invisible token cost made visible. `icmg bench savings` measures the reduction on YOUR repo, reproducibly (e.g. ~46% fewer tokens to read a ~1000-file tree via `icmg context` vs raw reads). One-line installers (`scripts/install.sh` / `install.ps1`) fetch, checksum-verify, and install the latest release — no build step. Six new graph languages — Ruby, Swift, Kotlin (now dedicated; was mis-mapped to Java), Scala, Lua, Dart — bring first-class import/symbol extraction to 14 languages. Plus a cleanup for a stale Python PreCompact hook that spammed `python3: command not found` on Python-less servers. Full automated suite passes (1673 checks).
- **v2.0.15** - **Token-efficiency leap + the Windows Server 2019 crypto crash fixed at the root.** Less re-reading, honest budgets, denser commands. New `icmg context <file> --for "<intent>"` returns only the lines relevant to an intent — each line is scored against the intent terms, windows are built around the hits, merged, and the top windows emitted with line numbers — so you get ~30 relevant lines instead of ~500, with no `--lines A-B` guessing. A read-dedup stub: re-reading an unchanged file (a context **cache hit** — the key already includes the file's mtime+size, so any edit misses the cache) now emits a compact "already shown this session" stub instead of re-dumping the body (~97% saved on the re-read; `--full` or `ICMG_NO_DEDUP_STUB` re-emits in full). A pre-exec command densifier in `icmg run` rewrites noisy commands to emit less *before* the output filter even runs (`git status → --porcelain=v2 --branch`, `pytest → -q --tb=line`, `tsc → --pretty false`, `pip list → --format=freeze`, …) — pure, idempotent, and guarded against shell composition and flags you set yourself. The context-budget meter and `icmg savings` are now per-model honest: a model context-window registry (`opus-4`/`gemini` 1M, `gpt-4o`/`deepseek` 128K, default a safe 200K) replaces a hardcoded 1M that lied on smaller windows, and a pricing registry replaces a hardcoded Sonnet rate that understated Opus cost ~5×. And the **root** of the headless Windows Server 2019 `err126` crash is fixed: SQLCipher's encrypted writes drew randomness through a legacy CryptoAPI module that isn't installed on Server Core — `icmg` now routes OpenSSL's RNG onto `BCryptGenRandom` (self-contained, always present) so `icmg context` / `graph update` run fully instead of degrading. Full automated suite passes (1657 checks).
- **v2.0.14** - **Windows-Server crypto-crash resilience, self-diagnosing module errors, and a command "you-are-here" map.** On hosts where a runtime crypto module is absent (a Windows Server SKU where SQLCipher's write-side crypto lazily loads a DLL that isn't there → `err126 "specified module could not be found"`), `icmg context` no longer crashes — it degrades to emitting the raw file so the caller keeps read access, and the graph scanner skips an un-writable node instead of crashing or hanging. Errors now diagnose themselves: an `err126` crash prints the last successfully-loaded DLL plus an actionable hint, `ICMG_TRACE_DLL=1` streams the full load order, and a new `icmg doctor --deps` walks each bundled DLL's PE import table (static **and** delay) to name any module that won't resolve on this machine — no Process Monitor or admin required. New navigation: `icmg map <cmd>` shows a command's related neighbors (derived from the live registry, so it never rots), every command's `--help` ends with a `related:` footer, and `icmg doctor` flags accidental near-duplicate commands. Housekeeping: the rich diagnose-and-auto-fix `doctor` is now canonical (a duplicate registration had been shadowing it), the quick DB/schema probe moved to `icmg db-check`, and doctor's bundled-DLL check matches the real shipped set. Full automated suite passes (1633 checks).

## 🚀 Quick start

**One line — Linux / macOS:**

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
