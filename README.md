<p align="center">
  <img src="assets/logo.svg" alt="Icemage" width="160"/>
</p>

# Icemage <sub><sup>(`icmg`)</sup></sub>

[![release](https://img.shields.io/github/v/release/ncmonx/icemage)](https://github.com/ncmonx/icemage/releases)
[![downloads](https://img.shields.io/github/downloads/ncmonx/icemage/total)](https://github.com/ncmonx/icemage/releases)
[![last-commit](https://img.shields.io/github/last-commit/ncmonx/icemage)](https://github.com/ncmonx/icemage/commits/main)
[![tests](https://img.shields.io/badge/tests-1311%2F1311%20passing-brightgreen)](#)
[![mcp tools](https://img.shields.io/badge/MCP%20tools-41-blueviolet)](#)
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
| Web-fetch reduction | 70 – 90 % smaller | up to 95 % | v0.4 |
| **Multi-file UI propagation** (style-clone) | **30 – 50× cheaper** | up to 98 % | v1.22 |
| **Cross-project bundle** (port) | **8 – 12× cheaper** | up to 95 % | v1.24 |
| **Compressed-Write** (AI emit diff) | **70 – 95 % fewer tokens** | up to 98 % | v1.25 |
| Repeat-context recall | **< 5 ms** cached | near-zero | v1.21 |
| Cold build time (icmg itself) | **~50 % faster** | 20 min → 9-10 min | v1.26 |
| **Cost per AI session** | **down 70 – 90 %** | up to 95 % | — |

## ✨ What's new

- **v1.97.0** - **The B:/ "drive not found" popup-killer now self-heals — it can't silently die anymore.** v1.92 added a background daemon to auto-dismiss the Windows hard-error dialog (which, being modal, can hang a hook subprocess and freeze the agent). But the daemon only started at SessionStart, so if it was ever killed mid-session it stayed dead and popups slipped through. v1.97 makes both the SessionStart and the per-prompt (UserPromptSubmit) hooks call `popup-killer ensure` — an idempotent, near-zero-cost check (via a named mutex) that relaunches the daemon before the next turn if it died. The drive-popup guard is now always live. Full automated suite passes (1311 checks).
- **v1.96.0** - **AI is icmg-compliant from turn one — no more reminding it to read the rules.** Previously, every fresh session an agent might `grep`/`Read` your `AGENTS.md`/`CLAUDE.md` (or ignore icmg entirely) until you reminded it. v1.96 makes the SessionStart hook inject a concise standing-rules directive at the start of every session: the project rules are already loaded as agent config (don't grep/read them), and every action should go through icmg first (`recall`, `context`, `code_search`, `run`, `parallel`), with the post-change sync reflexes. Installed automatically by `icmg init`/`--force`. Full automated suite passes (1311 checks).
- **v1.95.0** - **Tiered memory: `icmg memory list --tier hot|warm|cold`.** Memories are now classified by access pattern — recency + frequency + importance — so you can surface only what's live. Hot = used in the last 2 days, frequently recalled (≥5×), or critical-importance (pinned hot); warm = touched within a month or recalled ≥2×; cold = old and rare. The classifier is pure and shared (no schema change — reuses existing `last_used`/`frequency`/`importance`), ready to drive recall ranking and eviction next. Full automated suite passes (1311 checks).
- **v1.94.0** - **`icmg graph skeleton` — onboard a whole repo from one token-budgeted view.** Emits a dense repo skeleton: files ranked by graph degree-centrality (the most-connected "god files" first), each followed by its symbol signatures, accumulated until a `--budget` char limit. An agent (or human) understands a codebase's shape from a single command instead of reading dozens of files. Pure, deterministic ranking — reuses the existing knowledge-graph centrality. Full automated suite passes (1305 checks).
- **v1.93.0** - **New MCP tool `icmg_code_search` — agents find code by query instead of grep/Read.** Exposes the knowledge graph's symbol/content search over MCP: an AI assistant calls `icmg_code_search` with a query (symbol name, identifier, or keywords) and gets back ranked code locations (path, symbol, kind, signature, line) — no full-file Reads, no unfiltered greps. Keyword-first and deterministic (ranks `symbol_name` + content, with an exact-symbol fallback and an optional `kind` filter: file/class/function/…); a semantic cosine rerank over graph embeddings is a future opt-in (local-first: the default path needs no model). Full automated suite passes (1301 checks).
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
hosted or managed service. Everything else is fair game. Audit the binary, the release notes, and the **full public source** freely. Security issues go through private disclosure ([SECURITY.md](SECURITY.md)) so a fix can ship before exploitable details are public.
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

**Is the source open?**
Yes — the full source is public under [Elastic License 2.0](LICENSE) (source-available): free to read, build, modify, and self-host. The only limit is reselling icmg itself as a hosted/managed service. For **security issues**, please report privately per [SECURITY.md](SECURITY.md) instead of a public issue, so a fix can ship before exploitable details are public.

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
