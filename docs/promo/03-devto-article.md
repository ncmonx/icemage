# dev.to / Medium / Hashnode — long-form article (full draft)

**Title:** How I cut my Claude Code token bill by 85% with a 17 MB CLI

**Tags (dev.to):** `claudecode`, `ai`, `productivity`, `cli`, `opensource`

**Cover image suggestion:** Side-by-side terminal screenshot — left "before icmg" (200K tokens), right "after icmg" (24K tokens). Same task.

---

## TL;DR

I built [Icemage](https://github.com/ncmonx/icm-graph) — an open-source CLI that sits between your terminal and any AI coding assistant. It packs context bundles, filters noisy output, caches what's cacheable, and tracks every token saved.

On my daily workflow it compounds to **85–95% reduction** in tokens billed by Claude / OpenAI / Anthropic / Cursor. One binary. Apache-2.0. No account, no telemetry.

This post explains how, why, and what I learned shipping it solo.

---

## 1. The problem (real numbers from my own bill)

Last quarter I ran a small B2B AI feature using Claude Code as my coding partner. Three weeks in, my Anthropic invoice was **$340**.

I broke it down per command:

| Action | Tokens / call | Calls / day | Daily cost |
|---|---|---|---|
| Read large file (>500 LOC) | ~30,000 | 25 | $7.50 |
| Run + paste failing test output | ~12,000 | 18 | $2.16 |
| Re-explain project context after `/clear` | ~4,000 | 12 | $0.48 |
| "Thinking" passes on trivial edits | ~8,000 | 30 | $2.40 |
| WebFetch of API docs | ~6,000 | 8 | $0.48 |
| **Total estimated daily** | — | — | **~$13/day** |

The model was technically doing its job — but most of those tokens were **stable context** the model didn't need to re-read, or **noise** that hurt response quality.

I tried the obvious things: smaller `/init`, manual `/compact`, prompt caching. Each helped 10–20%. None compounded. Most introduced cognitive overhead — I was now thinking about what to feed the model instead of writing code.

So I built a CLI that does it for me.

---

## 2. What icmg actually does

Seven independent layers. Each is small. Stacked, they multiply.

```
                ┌───────────────────────┐
   YOUR TASK ──▶│  icmg pack "<task>"   │── 4 KB context bundle
                └────────────┬──────────┘     (graph + memory + diff)
                             │
                             ▼
                ┌───────────────────────┐
                │   28 MCP tools        │── Claude Code / Cursor / Cline
                │   recall, store, …    │
                └────────────┬──────────┘
                             │
                             ▼
                ┌───────────────────────┐
                │  Hooks intercept      │── Read 100-line cap
                │  Read / Bash / Edit   │── Bash 8 KB cap, ANSI strip
                │  Glob / Grep / Web    │── Glob top-50, WebFetch 4 KB
                └────────────┬──────────┘
                             │
                             ▼
                ┌───────────────────────┐
                │   Auto-compress       │── reversible glossary
                │   (≥3 KB pack)        │
                └────────────┬──────────┘
                             │
                             ▼
              SAVINGS DASHBOARD + RECEIPTS + COMPLIANCE TRACKING
```

### 2.1 Pack bundles, not files

`icmg pack "fix the auth refresh bug"` runs:

1. BFS over the project's symbol graph (tree-sitter parsed, SQLite-stored).
2. BM25 query over a memory store of past decisions tagged for the same area.
3. Last N commits' diff filtered to the affected files.
4. Concatenated into ≤4 KB.

The model receives **exactly what's relevant**, not "here is auth.ts, all 800 lines."

### 2.2 Filter noise at the hook layer

Claude Code's `PostToolUse:Read` and `PostToolUse:Bash` hooks call `icmg compress` before output reaches the model. Examples:

| Command | Raw | Filtered | Saved |
|---|---|---|---|
| `git log --oneline -n 50` | 4.2 KB | 1.1 KB | 73% |
| `npm test` (passing) | 38 KB | 240 B | 99% |
| `cat schema.sql` | 22 KB | 1.8 KB summary + glossary | 92% |
| `find . -name "*.ts"` | 9 KB | top-50 by mtime + count | 86% |

Filters are command-type-aware. `git`, build tools, test runners, package managers all have dedicated filters that preserve signal and drop boilerplate.

### 2.3 Local memory with BM25 + cosine recall

Every approved decision lands in a SQLite memory store:

```
icmg store --topic decisions-auth "Use refresh token rotation: ..."
```

Recall is hybrid (BM25 lexical + 384-dim MiniLM cosine), weighted by recency and importance. Returns top-K relevant nodes in <50 ms over 10K-node stores.

No vector DB. No external API. No subscription.

### 2.4 Reversible compression via glossary

Long paste output gets a per-session glossary:

```
SELECT id, email, created_at FROM users WHERE id IN (1, 2, 3, ...);

# becomes
$Q1 = "SELECT id, email, created_at FROM users WHERE id IN ($IDS);"
$IDS = "1, 2, 3, ..."

Q1 ran in 12ms. 247 rows.
```

`icmg expand` round-trips losslessly when the model needs the original.

### 2.5 Caches the model would re-pay for

- URL fetches → on-disk cache, ETag-aware, content-aware reduce (strip nav, dedup script blocks).
- Stable preamble → Anthropic prompt cache markers, batched.
- Repeat recall queries → 300s LRU on the recall function.

### 2.6 Receipts, not promises

```
$ icmg savings
project           tokens_saved   cost_saved   most_saved_cmd
icm-graph         2,140,883      $32.11       Bash (git diff)
api-service       1,876,442      $28.15       Read (.ts files)
client-frontend     984,001      $14.76       WebFetch (mdn)
TOTAL             5,001,326      $75.02
```

Numbers per session, per project, per command type. JSON output for billing dashboards.

---

## 3. Why a single binary?

Things I tried and rejected:

- **Python package**: cold start 1.2 s. Hook latency budget is <10 ms.
- **Node CLI**: same problem; `npx` even worse.
- **Rust**: would have worked. I'm faster in C++17. (Maybe v2.)
- **Cloud service**: introduces an account, network dep, and trust ask. Antithesis of "local-first."

C++17 + SQLite + a few statically/dynamically linked DLLs gives me:

- 5–10 ms hook latency.
- One artifact to ship.
- No `pip install` / `npm install` ritual.
- Builds offline on Win + Linux from one CMake config.

---

## 4. Architecture in 30 lines

- `src/main.cpp` → `cli/dispatcher.cpp` → `core/Registry<BaseCommand>` → 95 command implementations in `src/cli/commands/`.
- Commands self-register at static init via `ICMG_REGISTER_COMMAND("name", Class)`.
- Same Registry pattern for: extractors (per language), filters (per command type), MCP tools, importers.
- Two SQLite DBs: project-local (`.icmg/<project>.db`) + global (`~/.icmg/global.db`).
- 27 forward-only migrations compiled into the binary as a C string array.
- 71/71 ctest pass on Windows MinGW + Linux glibc.

Full code tour in the [CLAUDE.md](https://github.com/ncmonx/icm-graph/blob/main/AGENTS.md).

---

## 5. What I got wrong (and would change)

- **HNSW ANN**: I shipped it, then ripped it out. BM25 pre-filtering already caps candidate sets at 1–10K nodes; an in-memory embed cache + cosine was strictly better, less code, no index-rebuild discipline needed.
- **Auto-VACUUM on prune**: blocks for seconds on multi-GB DBs. Now opt-in.
- **GitHub Actions matrix CI**: burned through free-tier minutes in 8 weeks. Switched to local WSL2 Linux build. CI is gone; release ceremony is `bash scripts/release-linux.sh`.
- **Caveman mode reminders**: started as plain text injection. Drifted after 5–10 turns. Now: per-prompt re-inject with 5 intensity tiers tied to a 24-h violation count. Works.

---

## 6. Try it

```bash
# Windows / WSL2 / Linux
curl -L https://github.com/ncmonx/icm-graph/releases/latest/download/icmg-1.1.1-win-x64.zip -o icmg.zip
unzip icmg.zip -d ~/bin

cd your-project
icmg init                # auto-installs hooks for Claude Code if detected
icmg pack "your task"     # see a context bundle
icmg savings              # see what you saved

# Or as MCP server:
icmg --mcp-server         # plug into Claude Desktop / Cursor / Cline
```

Apache-2.0. 71/71 ctest. No telemetry. No account. macOS arm64 builds from source.

If it saves you anything, a star ⭐ on the [repo](https://github.com/ncmonx/icm-graph) costs you nothing and helps a lot. If it saves you enough that buying me a coffee feels right, there's a [Ko-fi](https://ko-fi.com/ncmonx) in the README. Either way, the code stays open.

---

## 7. What's next

v1.2 is in design:

- macOS arm64 prebuilt binary (looking for a maintainer with Apple hardware).
- Cross-project recall: "how did I solve X in project Y two months ago?" — `icmg cross-recall` exists, needs better ranking.
- Token-cost calibration per model (current heuristics are Claude-tuned; OpenAI / Gemini coefficients differ).

PRs welcome. Issues with daily-use telemetry (anonymized, opt-in) welcome more.

---

**Repo:** https://github.com/ncmonx/icm-graph
**Latest release:** v1.1.1 (Win + Linux)
**License:** Apache-2.0

Built by one developer in spare evenings. Stays open-source regardless.
