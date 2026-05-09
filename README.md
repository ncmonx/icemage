# icmg

[![release](https://img.shields.io/github/v/release/ncmonx/icm-graph)](https://github.com/ncmonx/icm-graph/releases)
[![tests](https://img.shields.io/badge/tests-44%2F44%20passing-brightgreen)](#)
[![release](https://img.shields.io/github/v/release/ncmonx/icm-graph)](https://github.com/ncmonx/icm-graph/releases)
[![license](https://img.shields.io/badge/license-Apache--2.0-blue.svg)](LICENSE)
[![sponsor](https://img.shields.io/badge/sponsor-GitHub-ea4aaa?logo=github-sponsors)](https://github.com/sponsors/ncmonx)
[![ko-fi](https://img.shields.io/badge/Ko--fi-tip-ff5e5b?logo=ko-fi)](https://ko-fi.com/ncmonx)

> **Stop burning tokens. Stop losing context. Ship faster.**

A single binary that makes Claude Code, Cursor, and every other AI coding assistant **70–90% cheaper** to run — without dumbing them down.

If you've ever watched 30K tokens evaporate on a single file read, paid for "thinking" you didn't need, or re-explained the same project context after `/clear` for the fifth time today — this is for you.

---

## What it does

| Pain | What icmg does about it |
|---|---|
| Big files inflate every prompt | Surgical context bundles — only what the task actually needs |
| Noisy command output drowns the model | Output filtering tuned per command type |
| Same bug solved twice | Persistent memory that surfaces past fixes when they apply |
| `/clear` wipes hard-won context | Snapshots that restore in one command |
| Models "think" 8K tokens for a one-line rename | Intent-aware directives that turn off analysis when it isn't needed |
| Re-sending the same project preamble every turn | Long-lived prompt-cache markers — pay once, reuse cheap |
| 30K tokens of logs / diffs / dumps | Lossless context compression with reversible round-trips |

Each one is a few percent. Stack them and you get the headline number.

---

## Quick start

```bash
# Build
cmake -B build && cmake --build build

# Or grab the release binary
# https://github.com/ncmonx/icm-graph/releases

# Bootstrap a project (installs the right hooks for your AI agent)
icmg init

# Try one of these
icmg pack "fix the login bug"          # surgical context bundle
icmg run npm test                      # noise-filtered build output
icmg compress < big.log                # cut tokens on dumps + diffs
icmg pack "rename foo.ts" --auto-think # skip the model's "thinking" pass when not needed
```

That's it. Nothing else to configure.

---

## Headline numbers

Real measurements on real coding sessions (your mileage varies):

- **Big-file reads:** 60–80% smaller
- **Build / test logs:** 80–95% smaller
- **SQL / table dumps:** 95–99% smaller
- **Routine "thinking" overhead:** 50–90% off
- **Stable preamble (project map, rules):** 90% off via cache markers
- **Repeat queries within a session:** 100% off via local result cache
- **Bulk operations:** 50% off via batch request emission
- **External downloads (HTML / API / PDF):** 70-90% off via local fetch + content-aware reduction
- **Screenshot input via OCR:** 90-95% off + 5x faster than vision on text-heavy images
- **Team-shared memory + graph:** zero-recompute on shared decisions across teammates
- **Combined stack on a typical turn:** ~85–95% reduction

Token-cost savings at scale: roughly **\$0.10 per non-trivial Claude turn**, hundreds of dollars a month for active users.

---

## Highlights

- **One binary.** No node_modules, no Python venv, no Docker. Drop it in `PATH`.
- **Local-first.** Per-project SQLite. Never phones home. Your code stays yours.
- **MCP server built-in.** Plugs into Claude Code, Cline, Continue — anything that speaks MCP.
- **Embedded dashboard.** `icmg serve` opens a tiny local UI for audit and recall.
- **AST-aware** for several languages out of the box; clean fallback for the rest.
- **Team-friendly.** Memory + graph share via git-tracked snapshots; conflict-resolved via row versioning.
- **Image-aware.** Local OCR for screenshots saves 90%+ vs vision API on text-heavy images.
- **Apache-2.0.** License preserved on releases.

---

## When to use which command

```bash
icmg pack "<task>"        # starting a task → one bundle, not 5–10 reads
icmg context <file>       # need a single file → surgical, not full Read
icmg run <noisy-cmd>      # any command that dumps a lot → filtered output
icmg diff-summary         # PR review → symbol-grouped, not raw diff
icmg compress             # any big text → cut tokens, reverse-able
icmg recall "<query>"     # past decisions → surfaces what you already learned
icmg agent "<task>"       # full pipeline (pack + cache + directives → LLM)
icmg batch --task ...     # bulk operations → 50% discount via batch API
icmg fetch <url>          # download HTML/JSON/PDF → reduced + cached (70-90% off)
icmg ingest screenshot.png # OCR a screenshot → text-only payload (90% off vs vision)
icmg sync init/push/pull   # team share memory + graph via git-tracked JSONL
icmg savings              # see what you saved (console / --html / --json)
icmg whats-new            # show release notes (call after `icmg update`)
icmg serve                # quick visual audit of memory + graph
```

Run `icmg --help` for the full list. Each subcommand has its own `--help`.

---

## Install

**Windows (recommended):** download the latest release, drop `icmg.exe` somewhere on `PATH`. The bundled DLLs live next to it. Done.

**Build from source:**

```bash
git clone https://github.com/ncmonx/icm-graph
cd icm-graph
cmake -B build && cmake --build build
# Optional capabilities (turn on what you want)
cmake -B build -DICMG_USE_TREESITTER=ON -DICMG_USE_ONNX=ON
```

Optional capabilities are exactly that — optional. Default build runs everywhere with zero external dependencies beyond a C++17 compiler.

---

## How it pays off in practice

After a few days of use you'll notice:

- Sessions get longer before `/compact`
- Recurring questions ("how does auth work in this repo?") answer themselves from memory
- Big PR reviews stop blowing the context window
- Repeat questions in the same session never recompute (local cache)
- Bulk operations (mass refactor preview, regen wiki) cost half
- Docs/API research no longer dumps 50KB pages into your prompt
- `icmg update --apply` survives "file in use" errors via pending-restart
- Your monthly Claude bill stops scaring you

The gains compound the more you use it. Memory recall gets sharper. Snapshot restore gets faster. Compression learns what your codebase looks like. Cache hit-rate climbs as you settle into patterns.

---

## Honest limits

- Windows is the primary target. Linux / macOS work but are tested less.
- It is opinionated. If you fight the conventions you'll fight the tool.
- It will not magically make a bad prompt good — it makes a good prompt cheap.
- Some optional capabilities require a one-time download (the binary tells you when).

---

## Support

Solo maintainer, no VC backing. If icmg saved you tokens, consider supporting development:

- 💚 [GitHub Sponsors](https://github.com/sponsors/ncmonx) — recurring or one-time
- ☕ [Ko-fi](https://ko-fi.com/ncmonx) — quick tip, no signup

Every contribution funds priority feature work and faster bug response.

---

## License

Apache 2.0. Use it however you want. Attribution appreciated, not required.

See [LICENSE](LICENSE) and [NOTICE](NOTICE).

---

## Other docs

- `AGENTS.md` — how to wire icmg into your AI agent's instruction set
- `COMMANDS.md` — full CLI reference
- `CLAUDE.md` — Claude Code-specific notes

---

**TL;DR:** stop paying full price for AI coding sessions you don't need to. Drop `icmg` in your `PATH`, run `icmg init`, get back to shipping.
