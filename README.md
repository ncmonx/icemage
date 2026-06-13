<p align="center">
  <img src="assets/logo.svg" alt="Icemage" width="160"/>
</p>

# Icemage <sub><sup>(`icmg`)</sup></sub>

[![release](https://img.shields.io/github/v/release/ncmonx/icemage)](https://github.com/ncmonx/icemage/releases)
[![downloads](https://img.shields.io/github/downloads/ncmonx/icemage/total)](https://github.com/ncmonx/icemage/releases)
[![last-commit](https://img.shields.io/github/last-commit/ncmonx/icemage)](https://github.com/ncmonx/icemage/commits/main)
[![tests](https://img.shields.io/badge/tests-1763%2F1763%20passing-brightgreen)](#)
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

- **v2.4.2** - **`icmg run` now works on plain Windows (no git-bash) — plus graph-precision and store-latency fixes.** The headline: `icmg run "<command>"` was completely broken on Windows machines without git-bash/MSYS, returning `CreateProcess failed: 2` for shell lines and bare built-ins/cmdlets. It relied on `CreateProcess`'s PATH search, which misses PowerShell under `System32\WindowsPowerShell\v1.0` and the Store `pwsh` alias; it now resolves the shell to a full absolute path and falls back through PowerShell for builtins/cmdlets, so an entire platform configuration goes from broken to working. Two more: the code graph no longer creates dozens of false `calls` edges when a callee name is defined in many files (common names like `run`/`get`/`value` used to fan out and inflate unrelated files' PageRank) — a name resolving to more than a few definitions is now left unlinked. And the memory-store duplicate check, which scanned the *entire* corpus on every `store` (O(N) word-set Jaccard), is bounded to the most-recent candidates plus a hoisted tokenizer and a subprocess-free git-sha read, so store latency stops growing with corpus size. Full automated suite passes (1763 checks).
- **v2.4.1** - **`icmg run "<shell line>"` works again — pipes, `&&`, and redirects no longer break.** Two long-standing bugs in how `icmg run` handled a single quoted command string. A whole-command token like `icmg run "echo hi"` was being *re-quoted*, so the underlying `bash -c` saw `"echo hi"` as one word and reported `command not found`; and a piped command like `icmg run "ls | grep x"` had its `|` split into a literal argument fed to `ls` (`cannot access '|'`). Both are fixed: a single command token is now treated as a verbatim shell line, and any command containing an *unquoted* shell operator (`|`, `&&`, `;`, `>`, `` ` ``, `$(…)`) routes through the shell instead of argv-exec — for both the buffered and `--stream` paths. Quoted operators (`grep 'a|b' f`) stay literal and take the fast argv-safe path. Full automated suite passes (1755 checks).
- **v2.4.0** - **The code graph now ranks by PageRank — and you can steer it at your task.** icmg's graph views ranked files by raw *degree* (a 1-hop edge count that lets bundled headers dominate). They now rank by **PageRank**: importance propagates *transitively* (a symbol referenced by important symbols outranks one referenced by many trivial ones — the ranking aider and RepoGraph use), and each edge is **confidence-weighted** so a structural `inherits` edge carries more pull than a name-based `calls`. Two ways to make it task-aware: `icmg graph skeleton --for "<task>"` and `icmg graph recent --for "<task>"` seed a **Personalized PageRank** from the task's terms so the most relevant code surfaces first, and `icmg pack "<task>" --pr` ranks the bundle's mentioned files the same way (opt-in; the default pack path stays fast — no full-graph load). The views are also **clean by default**: third_party/vendored files, sibling projects that leaked in through cross-project edges, and test files are filtered out (`--include-vendored` / `--all-paths` / `--tests` bring them back), so a skeleton reads as a map of *your* production code instead of bundled headers. Pure algorithm, no new dependency. Full automated suite passes (1750 checks).
- **v2.3.1** - **The B:/ "cannot find drive" popup killed at the source, plus a self-enforcing memory gate that actually fires.** Two fixes born from real friction. The modal B:/ "insert disk" popup (and its beep) that flashed during `icmg recall`/`context` under git-bash is gone at the root: MSYS's path-converter was rewriting cmd.exe flags that look like POSIX paths into drive paths (`/B` → `B:\`), and the nonexistent B: drive made cmd raise a modal dialog — `icmg` now sets `MSYS_NO_PATHCONV=1` in its own process environment so child shells pass flags verbatim and the popup is never *created* (not merely dismissed after the fact, which always raced and flashed). And the post-change ritual gate — meant to nudge a `store`+`wflog` after you edit code — was misfiring on every turn because its recorder never ran: a missing newline in the generated Bash hook (`fi` and `out=` fused into `fiout=`) syntax-errored the whole hook, and even when it ran it inspected only the first token of a command starting literally with `icmg `, so `RAW=1 icmg store` (env prefix) and `icmg store && icmg wflog` (chained) were never recorded. Both are fixed with a pure, unit-tested command-line parser that strips env-var prefixes and scans every `&&`/`||`/`;`/`|` segment. Full automated suite passes (1726 checks).
- **v2.3.0** - **Four more languages with real symbol extraction, plus ground-truth observability.** Kotlin, Swift, Ruby, and Scala now get first-class *symbol* extraction (classes/objects/traits/interfaces/enums/functions + base types), not just import edges — via lean regex extractors in the proven csharp/sql style, so there is zero grammar bloat (Kotlin's tree-sitter grammar alone is 22-34 MB; Swift ships no committed parser.c). `icmg whereami` prints one authoritative snapshot — running binary + version, the config file actually in use, and project/global/persona DB paths — so a stale binary or wrong-path guess is caught in one command instead of a debugging detour. And a config-path fix: `set`/`get` and `unset`/`edit`/`zone`/`list` now agree on the global config location (`%APPDATA%/icmg` on Windows), with `icmg config path` to show it. Full automated suite passes (1718 checks).
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
