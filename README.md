<p align="center">
  <img src="assets/logo.svg" alt="Icemage" width="160"/>
</p>

# Icemage <sub><sup>(`icmg`)</sup></sub>

[![release](https://img.shields.io/github/v/release/ncmonx/icemage)](https://github.com/ncmonx/icemage/releases)
[![downloads](https://img.shields.io/github/downloads/ncmonx/icemage/total)](https://github.com/ncmonx/icemage/releases)
[![last-commit](https://img.shields.io/github/last-commit/ncmonx/icemage)](https://github.com/ncmonx/icemage/commits/main)
[![tests](https://img.shields.io/badge/tests-1273%2F1273%20passing-brightgreen)](#)
[![mcp tools](https://img.shields.io/badge/MCP%20tools-28-blueviolet)](#)
[![commands](https://img.shields.io/badge/CLI%20commands-95%2B-blue)](#)
[![license](https://img.shields.io/badge/license-Elastic--2.0-blue.svg)](LICENSE)
[![OpenSSF Scorecard](https://api.securityscorecards.dev/projects/github.com/ncmonx/icemage/badge)](https://securityscorecards.dev/viewer/?uri=github.com/ncmonx/icemage)
[![sponsor](https://img.shields.io/badge/sponsor-GitHub-ea4aaa?logo=github-sponsors)](https://github.com/sponsors/ncmonx)
[![ko-fi](https://img.shields.io/badge/Ko--fi-tip-ff5e5b?logo=ko-fi)](https://ko-fi.com/ncmonx)

> **Stop burning tokens. Stop losing context. Ship faster.**

A single binary that makes Claude Code, Cursor, and every other AI coding assistant **70–90% cheaper** to run — without dumbing them down.

If you've ever watched 30K tokens evaporate on a single file read, paid for "thinking" you didn't need, or re-explained the same project context after `/clear` for the fifth time today — this is for you.

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
| **Graph prune** (`icmg graph prune`) | **drops dead-file nodes** | clears scan-pollution | v1.89.0 |
| **Multimodal graph nodes** (`icmg ingest`) | **image/PDF → graph node** | kind=multimodal | v1.89.0 |
| **Multi-agent leases** (`icmg session claim --scope`) | **conflict-free, 1 DB** | ICMG_AGENT_ID identity | v1.89.0 |
| Cost per AI session | **down 70 – 90 %** vs. raw | up to 95 % | — |

## ✨ What's new

- **v1.89.0** - **Graph prune, zone-scoped memory consolidation, multimodal graph nodes, and multi-agent work leases.** Four additions: (1) `icmg graph prune [--dry-run]` removes graph nodes whose backing file no longer exists, clearing scan-pollution (dead temp/build artifacts) that bloats the default zone; (2) `icmg memory consolidate --zone <name>` scopes near-duplicate collapse to a single zone, and the over-count hint now points to the correct command; (3) ingested media (`icmg ingest` image/PDF OCR) is recorded as a first-class graph node (`kind=multimodal`) so `icmg context`, graph queries, and zones surface it like source files; (4) `icmg session claim --scope <s>` / `leases` / `release` give conflict-free work leases across concurrent agents sharing one DB (stable identity via `ICMG_AGENT_ID`, stale leases auto-reclaimable). Full automated suite passes (1273 checks, +17).
- **v1.88.0** - **First CI-built release: Linux and macOS binaries are built and published automatically by GitHub Actions.** Pushing a version tag triggers a multi-platform matrix that compiles the full-feature build (ONNX + tree-sitter + llama/Vulkan + SQLCipher) on Linux and macOS, packages `tar.gz` + SHA256, and publishes the GitHub release — no manual cross-compilation. Windows is built locally (MSVC + sccache) and uploaded alongside. Adds release-automation scripts: `bump-version.ps1` syncs the version across all three source-of-truth files (`version.hpp` + `icmg.rc` + `CMakeLists`), and `release-win.ps1` refuses to upload unless the built binary's `--version` matches the tag. Full automated suite passes (1256 checks).
- **v1.87.0** - **Fix: `icmg update` and `icmg fetch` no longer fail on plain PowerShell**. Since v1.81, running icmg from a normal PowerShell prompt (not MSYS/Git-Bash) broke every command that calls the GitHub API or fetches a URL — `icmg update --check/--apply`, `icmg fetch`, the self-upgrade check — all returned "failed to query github (network or rate-limit)" even on a healthy connection. Two compounding causes: (1) in PowerShell, `curl` is an alias for `Invoke-WebRequest`, not the real curl, so its output was never the raw response body; (2) the internal shell wrapper passed commands to `pwsh -Command "..."` without escaping inner double-quotes, so a `-H "User-Agent: ..."` header silently broke argument parsing. v1.87 routes through `curl.exe` explicitly and escapes quotes in the wrapper — fixing all HTTP/JSON callers at once. Full automated suite passes (1256 checks).
- **v1.86.0** - **Compiler cache is now permanent: rebuilds are 50-80% faster out of the box**. The MSVC build previously relied on `ccache`, which ships ABI-broken in many MSYS2 installs (crashes with `0xC0000139` and gets silently skipped, so every build recompiled from scratch). v1.86 makes **sccache** (Mozilla's Rust-based compiler cache, MSVC-native) the primary cache: installed to a stable `C:/Tools/sccache`, on the user PATH, with a persistent 10 GB cache dir, and detected first by CMake (ccache stays as fallback only). Incremental rebuilds now reuse cached object files across sessions instead of recompiling the whole tree. Full automated suite passes (1254 checks).
- **v1.85.0** - **`icmg memory cache stats` gets a JSON mode and honest hit-rate reporting**. The recall-cache stats command now goes through a unified cache-metrics normalizer: hit-rate shows `n/a` when there have been no requests yet (instead of a misleading 0.0%), and a new `--json` flag emits machine-readable output for dashboards and scripts. First of the v1.84 token-efficiency primitives wired into a live command surface. Full automated suite passes (1254 checks).

## 🚀 Quick start

1. **Download** the latest build for your platform from the [Releases page](https://github.com/ncmonx/icemage/releases): `icmg-<version>-win-x64.zip` (Windows), `icmg-<version>-linux-x64.tar.gz` (Linux), or `icmg-<version>-macos-arm64.tar.gz` (macOS, Apple Silicon). **Linux and macOS binaries are now built and published automatically by GitHub Actions CI on every release** (Windows built locally + uploaded alongside).
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
- **Open source.** [Elastic License 2.0](LICENSE) - **source-available**. Free to use, copy, modify,
and self-host. The one limitation: you may not offer icmg to third parties as a
hosted or managed service. Everything else is fair game. Audit the binary, the release notes, and the file structure freely. Source code is held privately to keep the bug surface manageable for a solo maintainer — public reports + private fixes is the operating model.
- **Tamper-evident.** Every release ships with a `sha256` sidecar so you can verify the binary you downloaded.

---

## 🩹 Honest limits

- **Prebuilt binaries: Windows (x64), Linux (x64), and macOS (Apple Silicon)** — all three built and published automatically by CI on every release. macOS Intel (x86_64) isn't prebuilt yet; build from source (one command) if you need it.
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
