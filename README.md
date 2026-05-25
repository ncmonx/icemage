<p align="center">
  <img src="assets/logo.svg" alt="Icemage" width="160"/>
</p>

# Icemage <sub><sup>(`icmg`)</sup></sub>

[![release](https://img.shields.io/github/v/release/ncmonx/icm-graph)](https://github.com/ncmonx/icm-graph/releases)
[![downloads](https://img.shields.io/github/downloads/ncmonx/icm-graph/total)](https://github.com/ncmonx/icm-graph/releases)
[![last-commit](https://img.shields.io/github/last-commit/ncmonx/icm-graph)](https://github.com/ncmonx/icm-graph/commits/main)
[![ctest](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/ncmonx/7d6a2efa9d6191e28ff3f6a26e6ba7c7/raw/ctest.json)](#)
[![mcp tools](https://img.shields.io/badge/MCP%20tools-40-blueviolet)](#)
[![license](https://img.shields.io/badge/license-Apache--2.0-blue.svg)](LICENSE)
[![OpenSSF Scorecard](https://api.securityscorecards.dev/projects/github.com/ncmonx/icm-graph/badge)](https://securityscorecards.dev/viewer/?uri=github.com/ncmonx/icm-graph)
[![OpenSSF Best Practices](https://img.shields.io/cii/level/12818?label=OpenSSF%20Best%20Practices)](https://www.bestpractices.dev/projects/12818)
[![sponsor](https://img.shields.io/badge/sponsor-GitHub-ea4aaa?logo=github-sponsors)](https://github.com/sponsors/ncmonx)
[![ko-fi](https://img.shields.io/badge/Ko--fi-tip-ff5e5b?logo=ko-fi)](https://ko-fi.com/ncmonx)

> **Stop burning tokens. Stop losing context. Ship faster.**

A small helper app that makes AI coding assistants — Claude Code, Cursor, and friends — **70 – 98 % cheaper** to run, without making them less helpful.

**40 MCP tools · 122/122 tests · single-binary · 100 % local · pure-bash hooks** (zero Python/jq dependency).

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
| **Caveman-auto** (long-prose replies) | **60 – 75 % compress** | up to 85 % | v1.30.0 |
| **Service auto-start** (UserPromptSubmit) | **0-touch warm-up** | — | v1.30.0 |
| **Path ambiguity warning** (icmg context) | wrong-file lookups → loud | — | v1.29.0 |
| **rg-wrapper + brace glob** (icmg grep/files) | flag-mirror, **{a,b}** expand | — | v1.29.0 |
| **Local LLM scaffold** (llama.cpp vendored, opt-in) | **0 cloud calls** | privacy-first | v1.31.0 |
| **Smart router** (REGEX vs LLM_LOCAL vs CACHE) | **<100 us p99** | hot-path forced regex | v1.31.0 |
| **HTTP streaming download** (model fetch + SHA256) | **400 MB - 2 GB** safe-verify | tamper-detect | v1.31.0 |
| **icmg git** wrapper (single ergonomic entry) | **Tkil-filtered** + safety-gated | enforces icmg-FIRST | v1.31.0 |
| **Python-free core** (PRECOMPACT_PY dropped) | **-200-500 ms** boot saved | single-binary | v1.31.0 |
| **pack --rerank** (LLM-reorder memory hits) | **opt-in** warm-path | router-gated | v1.32.0 |
| **PreCompact LLM summary** (warm-pool Qwen 0.5B) | **<15 s** cold | regex fallback always | v1.32.0 |
| **icmg compact-bg** (proactive memory worker) | **<3 s** warm | manual + future hook | v1.32.0 |
| **KV-cache mgmt** (LlamaRunner) | **multi-prompt safe** | no ctx overflow | v1.32.0 |
| Cost per AI session | **down 70 – 90 %** vs. raw | up to 95 % | — |

Measured on real-world sessions. Your mileage will vary with project size and habits — anyone running a busy AI agent for a day already sees meaningful savings.
---

## ✨ What's new

> **Recent releases.** Older entries archived in [`CHANGELOG.md`](CHANGELOG.md).

- **v1.39.1** - **Item B live: `icmg chat --backend=local`**. Chat REPL now supports `--backend=local` (alias `--local`) flag that short-circuits to warm-pool LlamaRunner, skipping `icmg agent` subprocess + cloud cost entirely. Phi-3.5 @ 2.13 tok/s → ~30 s per 64-tok reply (slower than cloud but ZERO Claude API cost). Falls through to existing agent path on warm-pool acquire fail. Pair with active Phi-3.5 model (`icmg llm use phi-3.5-mini-q4`). Deferred v1.39.2+: Item A caveman LLM compress (async architecture), required-pack soft warn, Modules pilot (CMake 3.28 bump + 1-file `import std;` convert).
- **v1.39.0** - **`icmg-shell` smart entry + ctx-fill auto-trigger** (A9b live). NEW `scripts/icmg-shell.sh` — classify prompt via C2 intent cache (<2 ms): trivial → `icmg ask --backend=local` (zero Claude cost); debug/code/decision → print FORWARD marker (user pastes to Claude Code); default → try local then fall through. Wire via shell alias `?='icmg-shell'` for habit-forming routing. **A9b ctx-fill auto-trigger live** (UserPromptSubmit hook script): tracks running token estimate per session at `$HOME/.icmg/ctx-state/<sid>.tok`, fires detached `icmg compact-bg --threshold 75` at >150k tokens (~75 % of 200k Sonnet ctx window). Zero hot-path block (`&` + `disown`). Counter resets on successful compact. Opt-out: `ICMG_NO_AUTO_COMPACT=1`. Deferred v1.39.1: chat `--backend=local` wire, caveman LLM compress (async architecture needed), required-pack soft warn, modules pilot.
- **v1.38.2** - **5 more bash wrappers + Win `df` fix**. NEW: `icmg tar` (cross-platform create/extract via system tar), `icmg ps` (tasklist Win / `ps -ef` Linux, with `--name <substr>` filter), `icmg kill <pid> [--force] [--unsafe]` (refuses PID < 100 unless `--unsafe` to guard Win SYSTEM / Linux init), `icmg df [path]` (Win uses `wmic logicaldisk` — PowerShell escape was too fragile), `icmg du <path> [--summary]` (per-dir size). Bash surface coverage now **~85% common idioms**. Combined with v1.37's 14 wrappers and existing cmds, AI rarely needs raw bash. Pair with `ICMG_NO_BASH=1` (v1.37 leash ID=25) for hard enforcement.
- **v1.38.1** - **Wire helpers from v1.38.0**. `runPreToolUseTokenBudget` now active in `emitContext` — soft-warns user when prompt estimate exceeds `~/.icmg/token-budget.json` cap (default 50k tokens, word-count × 1.3 heuristic). Header `"TOKEN BUDGET WARN. ICMG_TOKEN_BUDGET_OFF=1 to disable."` prepended FIRST in additionalContext. `runPostToolForceCompress` now active in `cmdPostToolUseBash` — dedup adjacent lines + tail-cap last 50 lines when tool output exceeds `ICMG_FORCE_COMPRESS_KB` (default 2 KB). Appended to existing test-fail-context with `---` separator. Opt-outs: `ICMG_TOKEN_BUDGET_OFF=1`, `ICMG_NO_FORCE_COMPRESS=1`. Concat order in `additionalContext`: budget → amnesia → escalated → drift → rules → projects → known → fail → decisions → ship → approach → skill → caveman → msg.
- **v1.32.1** - **B1 toolchain fix + Linux build restored**: opt-in LLM build (`-DICMG_USE_LLAMA=ON`) previously crashed MSYS2/MinGW c++.exe (`0xc0000139`) on `ggml-cpu/amx/mmq.cpp` + `binary-ops.cpp` under default GGML flags. Root cause: `GGML_NATIVE=ON` (`-march=native` AVX intrinsics) × `GGML_CCACHE=ON` (wrapper) combo on GCC 15.2. Fix: inside `if(ICMG_USE_LLAMA)` block, force-disable both with `CACHE FORCE`. Override via `-DGGML_NATIVE=ON` / `-DGGML_CCACHE=ON` for known-good toolchains. Default OFF build unchanged. Also: **Linux x64 tarball restored** to public release (v1.31.0 + v1.32.0 were Win-only; v1.32.1 ships both). 122/122 ctest Win + Linux WSL.
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
