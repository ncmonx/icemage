<p align="center">
  <img src="assets/logo.svg" alt="Icemage" width="160"/>
</p>

# Icemage <sub><sup>(`icmg`)</sup></sub>

[![release](https://img.shields.io/github/v/release/ncmonx/icemage)](https://github.com/ncmonx/icemage/releases)
[![downloads](https://img.shields.io/github/downloads/ncmonx/icemage/total)](https://github.com/ncmonx/icemage/releases)
[![last-commit](https://img.shields.io/github/last-commit/ncmonx/icemage)](https://github.com/ncmonx/icemage/commits/main)
[![tests](https://img.shields.io/badge/tests-1600%2F1600%20passing-brightgreen)](#)
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

- **v2.0.13** - **Audit-grade read-only enforcement, a no-bypass escape hatch, and a service-process bloat fix.** A new `icmg strict audit` read-only mode for token-frugal audits: native full-file `Read` is hard-denied and redirected to `icmg context` (~80% fewer tokens) while targeted slices stay allowed, and browser-automation MCP tools (puppeteer/playwright) are denied at PreToolUse because their DOM/screenshot payloads bypass the command filter entirely — safe because an audit never edits (hard-denying `Read` otherwise breaks Claude Code's Read-before-Edit contract). `RAW=1` no longer bypasses pure read/search verbs (`cat`/`grep`/`sed`/…) — those always have an icmg equivalent, so the bypass is denied with an escalating hourly cooldown cap, stopping the escape hatch from becoming a habitual token leak. And a singleton-guard fix: the `icmg service` background process used a `Global\` named mutex that requires elevation, so on a normal session it failed open and the per-prompt autostart spawned dozens of duplicate services (DB-lock contention that hung `graph update`, a dead-drive popup storm, pipe collisions) — the guard now uses the `Local\` namespace and holds without elevation. Full automated suite passes (1600 checks).
- **v2.0.12** - **Frictionless capture, unread-aware messaging, one-shot session resume, and zero-LLM event memory.** Four small high-leverage tools. `icmg store --quick "<msg>"` drops a note with no topic (an auto `quick:<epoch>` is assigned) for friction-free capture mid-task. `icmg msg check` peeks unread cross-session messages since a persisted cursor that auto-advances, so you never pass `--since` by hand. `icmg wake-up --resume` appends persona identity and recent moments to the session briefing, re-hydrating continuity in one command. And `icmg auto-extract` is a Layer-0, zero-LLM memory capture for tool events: a successful `git commit` becomes a workflow-log note, a failing build's error becomes a known-issue candidate, everything else is skipped silently — enriched with rule-based entity extraction (URLs, IPs, env vars, @mentions) so a captured event is searchable by what it referenced, all without a model call. Full automated suite passes (1593 checks).
- **v2.0.11** - **Cross-project relationship memory, premium-aware local routing, and self-tending memory.** `icmg moment add|list|recall|forget|migrate` keeps relationship/identity "moments" in the per-install persona DB (cross-project, user-keyed) instead of the per-project code-memory DB, so they survive across every project and a fresh session; `icmg recall` auto-merges them into normal recall (FTS-ranked, deduped, fail-open), and `icmg moment migrate --topic <substr>…` does a curated, dry-run-first copy of genuine moments out of project memory (scoped to `memoir:`/`decisions-`; the earlier auto-heuristic was dropped after it pulled build/graph junk). The internal LLM router is now premium-aware: when a premium model is present and no explicit local mode is requested, internal LLM hops short-circuit to the deterministic regex path instead of spending a call — the local backend is reserved for genuinely no-premium contexts (headless cold-compaction, `agent` advisory). And a rate-limited auto-consolidate trigger (24h per-zone cooldown, fire-and-forget via a detached process) keeps a long-lived project DB from growing unbounded. Full automated suite passes (1576 checks).
- **v2.0.10** - **Live mutual-awareness across parallel sessions, exact BPE token counting, and an honest savings line.** Parallel `icmg` sessions on one machine can now see each other — `presence` (heartbeat liveness with TTL), `bus` (a shared action stream), `lock` (advisory ownership for safe parallelism), `msg` (directed/broadcast messages), and `hot` (a shared key/value whiteboard) — all append-only and race-free, with a heartbeat hook auto-installed by `icmg init` so each session beats its presence every turn. `ICMG_WIRE_DIR` points the wire at a commonly-visible folder, so sessions whose `%APPDATA%` is not shared can still rendezvous. Opt-in exact tokenization: `icmg tokenizer download cl100k|o200k` plus `ICMG_TOKENIZER=bpe-cl100k` gives precise GPT-family token counts (the default heuristic is now character-class weighted for a closer estimate with no download). And `icmg savings` prints honest two-number session totals instead of a fabricated coverage percentage. Full automated suite passes (1541 checks).
- **v2.0.9** - **Crash-hardening for path-argument commands on Windows Server.** A latent crash class: the throwing `std::filesystem::absolute` / `weakly_canonical` reach the PathCch API-set, which fails to resolve on some Windows Server SKUs and raises `filesystem_error` 126, crashing any command that resolves a path argument (`context`, `graph`, `claudemd`, `plan`, `import`) on that host — even though `--version` and memory commands kept working. A new no-throw `core::absolutePath` helper (error-code overload + purely-lexical fallback) now backs eleven previously-unguarded sites, and `Config::projectDbPath` no longer calls the throwing `weakly_canonical`. Path-arg commands degrade to a lexical path instead of terminating. Full automated suite passes (1483 checks).

## 🚀 Quick start

1. **Download** the latest installer from the [Releases page](https://github.com/ncmonx/icemage/releases) — `icmg-<version>-win-x64.zip` for Windows, `icmg-<version>-linux-x64.tar.gz` for Linux.
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