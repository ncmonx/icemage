<p align="center">
  <img src="assets/logo.svg" alt="Icemage" width="160"/>
</p>

# Icemage <sub><sup>(`icmg`)</sup></sub>

[![release](https://img.shields.io/github/v/release/ncmonx/icemage)](https://github.com/ncmonx/icemage/releases)
[![downloads](https://img.shields.io/github/downloads/ncmonx/icemage/total)](https://github.com/ncmonx/icemage/releases)
[![last-commit](https://img.shields.io/github/last-commit/ncmonx/icemage)](https://github.com/ncmonx/icemage/commits/main)
[![tests](https://img.shields.io/badge/tests-1331%2F1331%20passing-brightgreen)](#)
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
| **Code / graph search** (FTS5 snapshot) | **full-scan → sub-millisecond** | ~220× on a 19K-node graph | v1.100 |
| **Cost per AI session** | **down 70 – 90 %** | up to 95 % | — |

## ✨ What's new

- **v2.0.5** - **Long-session hardening + a self-improving prompt loop.** A multi-hour stress session of v2.0.4 surfaced a cluster of fixes plus one v2-roadmap feature. Fixes: `icmg suggest` scores by command **name** (not just description overlap), so "compress large output" maps to `compress` not `expand`, and the gated `suggest --hook` fires only on a strong name-level match (it previously never fired). `icmg hook` and `icmg shrink` read stdin via the isatty-guarded `slurpStdinSafe`, so a hook without piped input can't block (#30704). `icmg flow` rejects a flow that needs an argument when none is given (no junk `wflog add ""`). `icmg prompt-capture` reads only the transcript **tail**, so its per-turn cost stays constant as a session grows. And `build.ps1`'s error scan no longer false-trips on deliberate runtime test-log lines. Feature: `icmg profile qa-frequent` clusters recurring prompts from the auto-recorded history and surfaces them as candidates to promote into a saved skill — closing the loop capture → reuse → detect → promote. Full automated suite passes (1411 checks).
- **v2.0.4** - **Use what you built: command discovery, interlinked workflows, a persistent mode banner, and auto-recorded prompt reuse.** v2.0.0 added a lot of surface; this round makes it findable and self-driving. `icmg suggest "<intent>"` ranks the live command registry against a natural-language intent (model-free) so the long tail is discoverable — and as a gated UserPromptSubmit hook it auto-surfaces the most relevant command each turn. `icmg flow <name>` runs a named chain of existing commands in one shot (`change-done`/`sanity`/`refresh`, `{ARG}` substitution, `--dry-run`, fail-fast) so features interlink. `icmg mode set/get/clear` keeps a cross-project session-mode banner in the persona DB and injects it every turn, so the agent stays oriented across compaction. `icmg profile qa-suggest` reuses a past answer for a similar prompt (confidence-gated), and a new Stop hook (`icmg prompt-capture`) auto-records each turn's prompt→response into a per-day session zone — reuse builds itself, no manual `qa-add`. Plus `icmg ingest --doc` rejects binary content in text-extension files. Full automated suite passes (1402 checks).
- **v2.0.3** - **Governor focus mode, prompt-history pagination, and unified zone browsing.** `icmg govern --focus <task>` biases the injected working-set toward what you're working on (the recall query is overridden), so the budget is spent on the task at hand. `icmg profile qa-list --limit N` paginates the prompt history, and `--json` pairs with both `qa-list` and `qa-find` for scripting. `icmg profile zones` now lists both profile/skill zones AND prompt-history zones with their entry counts, so the whole cross-project persona store is navigable at a glance. Plus two fixes from continued long-session testing: `govern advise --fill` clamps nonsense values to `[0,100]`, and `qa-add` / `profile add` print the normalized zone so what you see matches what is stored. Full automated suite passes (1374 checks).
- **v2.0.2** - **Prompt-history CRUD, an active idle-compact advisor, and zone browsing.** Building on v2.0.0's prompt→response history, this round completes its lifecycle and activates a governor piece. `icmg profile qa-list` browses stored prompts (`--json` for scripts), `qa-forget` deletes one, and `profile zones` shows each zone with its prompt count. The **idle-compact advisor (C5)** is now wired into the installed Stop hook: at the end of a turn, when context fill is high, it nudges you to `/compact` at a natural break instead of the mid-task wall (reads `icmg context-budget --percent`; opt out with `ICMG_NO_COMPACT_ADVISE=1`). Full automated suite passes (1373 checks).
- **v2.0.1** - **Long-session hardening + context-window fill %.** Two fixes surfaced by a multi-hour stress session: (1) `icmg hookio` / `icmg correction` now guard `std::cin` with an isatty check (shared `slurpStdinSafe`) — a hook-spawned icmg invoked without piped input returns immediately instead of blocking forever on `cin.rdbuf()`, the root cause of process accumulation and hangs under sustained hook-spawn load; (2) `icmg init --install-hooks` now writes a `timeout` on every command hook, so the editor-connection-timeout protection survives re-init/upgrade and ships to all users. Adds `icmg context-budget --percent` (override the window via `ICMG_CONTEXT_WINDOW`) — the fill signal the idle-compact advisor consumes. Full automated suite passes (1370 checks).
## 🧭 Where v2 is headed

icmg v2 builds on four pillars. The themes below are the direction — sequenced,
not promises with dates.

**1. Token efficiency.** Optional perplexity-based compression (LLMLingua-style,
local-model only, opt-in — the deterministic Tkil filters stay the default), embedding-based
cross-turn dedup (upgrading word-set Jaccard to semantic cosine), and adaptive injection
budgets that learn per project.

**2. Long-session stability.** An *active* prompt→response history — when a prompt repeats or
closely matches a past one, the prior solution is surfaced automatically instead of being
re-derived. Semantic prompt matching and a task "focus mode" round it out.

**3. An extensible platform.** WASM skill modules: drop in a sandboxed `.wasm` (a custom Tkil
filter, a niche-language extractor, a deterministic transform) and icmg loads it without a
rebuild — a strict capability-gated sandbox, distributable across a team like a plugin. The
long-term north star is for the core binary to become a stable **orchestrator/provider** while
fast-moving, safe-to-sandbox features ship as modules you install on top — though anything
perf-critical or depending on native libraries (embeddings, the local LLM, the scanner) stays
in the native core by design.

**4. Memory intelligence.** Auto-suggesting reusable skills from frequent command patterns,
team-syncing zoned profile/skill stores, and smarter memory consolidation.

Local-first, deterministic, and opt-in remain the guiding principles throughout: the cheap
rule-based path is always the default; smarter model-based paths activate only when you ask.

---

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
- **Not a replacement for the AI.** Icemage is a token-trimming + memory layer — it doesn't write code for you and it won't make a bad model smart.
- **Semantic recall (ONNX embeddings) and the local LLM (llama) are optional and off by default** — they need a one-time model download. Without them, recall falls back to fast BM25 + recency ranking, which is enough for most work.
- **Graph-extraction depth varies by language.** Full AST parsing uses the tree-sitter backend (build flag); otherwise import/symbol extraction is regex-level — accurate for dependencies, lighter on fine-grained detail.
- **Local-first only.** No cloud sync, no telemetry. Team sharing is via git-committed JSONL (`icmg sync`) — deliberate and reviewable, not automatic background sync.
- **Encryption at rest (SQLCipher) is opt-in**, not the default — enable it if your `.icmg` database holds sensitive context.
- **Windows is the most battle-tested platform.** The Linux and macOS builds are CI-verified but see less daily use.

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
