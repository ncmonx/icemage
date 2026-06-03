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

- **v2.0.0** - **Lean & Lossless Compaction governor + a zoned memory platform.** icmg's flagship release. A deterministic **context governor** (`icmg govern`) targets one pain — Claude Code's compaction stalling a session mid-task. Compaction is harness-locked (can't be made async), so the governor makes it **rarer, lossless, and un-surprising** with seven model-free pieces: budget-cap icmg's own injected context (C1); drop cross-turn duplicate slices via word-set Jaccard (C2); order context **U-shaped** so the most relevant sits at the extrema where models actually attend, mitigating "lost in the middle" (C3); snapshot the working set before compaction and rebuild a hard-capped pinned-only anchor after so nothing critical is lost (C4); an idle-compact advisor that nudges `/compact` at a break, not the mid-task wall (C5); and structural trimming of noisy tool output (C6) and path-routed documents (`icmg ingest --doc`, C7). Alongside: a **zoned profile/skill store** (`icmg profile`) in a cross-project persona DB, plus **prompt→response history** (`qa-add`/`qa-find`) so a repeated/similar prompt reuses its past solution instead of re-deriving it. Also an IDE-hang fix (every hook is now timeout-bounded, so a stalled hook can't trip an editor's 60-second connection limit) and a `working_set_snapshot` migration (0040). Full automated suite passes (1370 checks).
- **v1.100.0** - **FTS5 search snapshot: code/graph search is now an indexed MATCH, not a full-table scan.** `icmg graph search` / `icmg_code_search` previously did an `O(n)` `LIKE '%q%'` scan of every graph node on each query — slow on large repos. v1.100 adds a `graph_fts` FTS5 index (migration 0039, external-content over `graph_nodes`, kept in sync by triggers) and routes search through `MATCH` + `bm25()` ranking, falling back to `LIKE` automatically if the index is absent (old DB / FTS5 not compiled). Query input is turned into safe prefix terms (injection-proof — no FTS operators leak through). On a ~19K-node graph the snapshot turns a scan into a sub-millisecond lookup. Full automated suite passes (1331 checks).
- **v1.99.0** - **Temporal knowledge graph + API-spec compilation.** Two token-savers: (1) `icmg graph recent` ranks files by **recency-decayed centrality** — the most-connected files you touched lately float to the top (exponential half-life decay on `updated_at`, blended with degree-centrality; `--halflife-days` / `--limit`), so onboarding focuses on what's hot, not what's merely big. (2) `icmg apispec <openapi.json>` compiles a verbose OpenAPI document into a **dense endpoint map** — `METHOD /path — summary (N params)` per line — instead of feeding the whole spec to the model. Both pure + deterministic. Full automated suite passes (1325 checks).
- **v1.98.0** - **Dynamic toolsets: expose only the MCP tools you need.** The MCP server ships 41 tools; sending all their schemas on every `tools/list` is wasteful when an agent only uses a handful. v1.98 lets you scope the exposed set: `ICMG_MCP_PROFILE=core` serves a curated ~10 essentials (recall, store, context, code-search, compress, savings, fetch, ingest, sync, related), or `ICMG_MCP_TOOLS=icmg_recall,icmg_code_search,...` an explicit allowlist (wins over profile). Unset = all tools (back-compat). Smaller tool-list payload, less context spent before the agent does anything. Full automated suite passes (1316 checks).
- **v1.97.0** - **The B:/ "drive not found" popup-killer now self-heals — it can't silently die anymore.** v1.92 added a background daemon to auto-dismiss the Windows hard-error dialog (which, being modal, can hang a hook subprocess and freeze the agent). But the daemon only started at SessionStart, so if it was ever killed mid-session it stayed dead and popups slipped through. v1.97 makes both the SessionStart and the per-prompt (UserPromptSubmit) hooks call `popup-killer ensure` — an idempotent, near-zero-cost check (via a named mutex) that relaunches the daemon before the next turn if it died. The drive-popup guard is now always live. Full automated suite passes (1311 checks).
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
