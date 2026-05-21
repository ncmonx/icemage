<p align="center">
  <img src="assets/logo.svg" alt="Icemage" width="160"/>
</p>

# Icemage <sub><sup>(`icmg`)</sup></sub>

[![release](https://img.shields.io/github/v/release/ncmonx/icm-graph)](https://github.com/ncmonx/icm-graph/releases)
[![downloads](https://img.shields.io/github/downloads/ncmonx/icm-graph/total)](https://github.com/ncmonx/icm-graph/releases)
[![last-commit](https://img.shields.io/github/last-commit/ncmonx/icm-graph)](https://github.com/ncmonx/icm-graph/commits/main)
[![ctest](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/ncmonx/7d6a2efa9d6191e28ff3f6a26e6ba7c7/raw/ctest.json)](#)
[![mcp tools](https://img.shields.io/badge/MCP%20tools-36-blueviolet)](#)
[![license](https://img.shields.io/badge/license-Apache--2.0-blue.svg)](LICENSE)
[![OpenSSF Scorecard](https://api.securityscorecards.dev/projects/github.com/ncmonx/icm-graph/badge)](https://securityscorecards.dev/viewer/?uri=github.com/ncmonx/icm-graph)
[![OpenSSF Best Practices](https://img.shields.io/cii/level/12818?label=OpenSSF%20Best%20Practices)](https://www.bestpractices.dev/projects/12818)
[![sponsor](https://img.shields.io/badge/sponsor-GitHub-ea4aaa?logo=github-sponsors)](https://github.com/sponsors/ncmonx)
[![ko-fi](https://img.shields.io/badge/Ko--fi-tip-ff5e5b?logo=ko-fi)](https://ko-fi.com/ncmonx)

> **Stop burning tokens. Stop losing context. Ship faster.**

A small helper app that makes AI coding assistants — Claude Code, Cursor, and friends — **70–90% cheaper** to run, without making them less helpful.

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

| Metric | Typical | Best |
|---|---|---|
| File-read savings | 70 – 85 % fewer tokens | up to 92 % |
| Test / build output | 60 – 80 % shorter | up to 90 % |
| Repeat-context cost | near-zero (cached) | — |
| First-prompt warmup | < 1 s | — |
| Cost per AI session | **down 70 – 90 %** vs. raw | — |

Measured on real-world sessions. Your mileage will vary with project size and habits — anyone running a busy AI agent for a day already sees meaningful savings.

---

## ✨ What's new

> **Recent releases.** Older entries archived in [`CHANGELOG.md`](CHANGELOG.md).

- **v1.21.4** — when Claude Code is about to compact a session transcript, Icemage now grabs each individual decision/fix/root-cause statement and stores it permanently so it stays recallable; eight new MCP tools (bench-recall, feedback loop, memoir, metrics, known-issue) — total 36 MCP tools.
- **v1.21.3** — six new language-aware output strippers (Rust, Go, Java, .NET, Swift, Kotlin) so noisy build/test output gets trimmed to just the errors and summary that matter, instead of the generic catch-all filter.
- **v1.21.2** — auto-extract decision/fix snippets from prompts that mention them; commands that fail noisily now save their full raw output to a spill file so you don't lose context; new `icmg bench-recall` lets you test your recall quality with a plain-text scenario file.
- **v1.21.1** — `icmg update --apply` now stops old processes before swap; new `icmg feedback-loop` command; per-project `.icmg/filters.toml` for custom strip rules; large files automatically spilled to a pointer file on Read; `grep` / `git status` results cached per-turn.
- **v1.21.0** — three convenience picks: silent dedup mode for memories, `icmg memoir export --format ai|ascii` for LLM-ready dumps, and `icmg init --tool <name>` accepts hints for non-Claude AI assistants.

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
