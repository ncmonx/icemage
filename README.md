<p align="center">
  <img src="assets/logo.svg" alt="Icemage" width="160"/>
</p>

# Icemage <sub><sup>(`icmg`)</sup></sub>

[![release](https://img.shields.io/github/v/release/ncmonx/icemage)](https://github.com/ncmonx/icemage/releases)
[![downloads](https://img.shields.io/github/downloads/ncmonx/icemage/total)](https://github.com/ncmonx/icemage/releases)
[![last-commit](https://img.shields.io/github/last-commit/ncmonx/icemage)](https://github.com/ncmonx/icemage/commits/main)
[![tests](https://img.shields.io/badge/tests-2027%2F2027%20passing-brightgreen)](#)
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

- **v2.12.0** — **Fix: `icmg find` Unicode crash + build.ps1 auto-installs the live binary.** `icmg find` crashed with Windows error 1113 ("no mapping for the Unicode character") when a scanned path held non-ANSI filenames; three call sites in `find_cmd.cpp` that narrowed paths via `.string()` now use `pathU8()` for UTF-8-safe handling (issue #221). `build.ps1` now auto-updates the in-use `~/bin/icmg.exe` after a successful build via a Windows-safe rename→copy→kill→cleanup sequence (rename old to `.old` so the open handle releases the name, copy the fresh exe in, terminate any running `icmg.exe`, delete `.old`) — no more manual copy step or "file in use" failure. Migration `0046` (delta-output snapshot columns) is now embedded in shipped binaries, closing an embedded-drift gap.
- **v2.11.2** — **Ergonomics: three RAW=1 escape holes closed so the model picks icmg instead of shelling out.** From a 2026-06-28 audit of 3,353 wrong native-tool instincts. `icmg calc "<expr>"` is an offline arithmetic/stat evaluator (`+ - * / %`, right-assoc `**`, sqrt/log/ln/exp/trig, variadic min/max/sum/avg, pi/e) closing ~40× python-throwaway. `icmg context <file> --head N` / `--tail N` replaces native head/tail (~47×). A pipe-aware bash redirect closes the big one: 82% of search-escapes were grep used as a *pipe filter* (`ls|grep`), not codebase search — the classifier now exempts pipes ending in a self-filter, gives a correctly-quoted `icmg run "<pipe>"` for non-filter pipes, and per-tool advice for single commands. **+5 test files, ctest 100%.**
- **v2.11.1** — **Fix: disable popup-killer auto-start in hooks.** The B:/ MSYS drive-letter popup is now prevented at source via `MSYS_NO_PATHCONV=1` + `MSYS2_ARG_CONV_EXCL=*` in `build.ps1`, so the popup-killer daemon is no longer needed on startup. Two auto-invocations of `icmg popup-killer ensure` (SessionStart + UserPromptSubmit hooks) have been commented out — eliminates risk of daemon auto-dismissing legitimate Win32 dialogs (Save As, Print, etc.) from Firefox, Edge, SSMS, and other apps. Manual `icmg popup-killer run` still available if needed.
- **v2.11.0** â€” **Covenant + task store: deterministic cross-session injection.** `icmg covenant` stores must-hold rules and emits ALL active covenants on every SessionStart and post-compact â€” full enumeration, never BM25-sampled, never silently truncated. `--max-items` cap always shows a visible `[+N more]` marker; empty store emits nothing. `icmg task` parks work items that survive compaction: `todo`/`doing`/`done` lifecycle, `doing` listed first in inject, done tasks silent. Hook `icmg-covenant-task-session.sh` fires on every SessionStart + post-compact alongside wakeup. Migration 0045 adds `covenant` + `task` tables with zone partitioning and soft-enable/disable. Opt-out via `ICMG_NO_COVENANT_TASK=1`. **2027/2027 ctest (+17 new tests).**
- **v2.10.0** â€” **Graphify parity: NL-queryable knowledge graph, markdown/shell extractors, edge-confidence labels.** `icmg graph-query query "<NL>"` seeds from FTS, expands a bidirectional BFS subgraph, and answers via local LLM (offline-safe). `icmg graph-query explain "<node>"` describes a node + 1-hop neighbors. Markdown/RST/txt extractors map headings to symbols and links to graph edges; shell/PS1 extractors map functions and `source` calls. Every `graph_edges` row now carries `EXTRACTED`/`INFERRED`/`AMBIGUOUS` confidence. Router precision fix and `graph-clean` de-dup alias. **2010/2010 ctest (+12 new tests).**
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
