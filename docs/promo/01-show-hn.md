# Show HN — copy/paste

**Title (≤80 chars, pick one):**
- `Show HN: Icemage – cuts Claude Code token costs 70–90% (single binary)`
- `Show HN: A 17 MB binary that makes Claude Code 70–90% cheaper to run`

**URL:** https://github.com/ncmonx/icm-graph

**Body:**

```
Hi HN — I'm a solo developer who got tired of watching Claude Code burn 30K tokens
on a single file read, pay for "thinking" I didn't need, and re-explain the same
project context after /clear five times a day.

Icemage (`icmg`) is one Windows/Linux binary that sits between your terminal and any
AI coding assistant (Claude Code, Cursor, Cline, Continue). It does seven things:

1. Packs a 4 KB context bundle from a project graph + memory + recent diffs instead
   of letting the model Read whole files.
2. Filters subprocess output (git, build, test, npm) before it ever reaches the
   model — 60-90 % smaller logs, zero info loss for what matters.
3. Keeps a local SQLite memory of decisions + anti-patterns; BM25 + cosine recall.
   No vector cloud, no subscription.
4. Compresses paste/dump output reversibly via a per-session glossary.
5. Caches WebFetch/URL results locally; ETag-aware.
6. Batches cache-eligible prompts into the Anthropic prompt cache for ~90 % off on
   stable preamble.
7. Tracks every byte saved in a receipts table — `icmg savings` shows the running
   tally per project.

On a normal turn the stack compounds to **85–95 % reduction** in tokens billed by
the upstream API. Numbers come from real measurements across daily use, not vendor
benchmarks.

Architecture:
- C++17, single binary, no runtime deps beyond a few DLLs on Windows.
- 28 MCP tools so it works as an MCP server too (Claude Desktop / Code / Cline).
- 71/71 ctest on Win + Linux. CI was on GH Actions but I dropped the matrix to keep
  costs down — releases now ship via local WSL2 build.
- Apache-2.0. Will stay open-source regardless of funding.

What I'd love feedback on:
- Recall ranking: BM25 + recency + importance + cosine blend. Tunable but the
  defaults need real-world tuning data.
- The "hard-deny" hook on Claude Code's PreToolUse — blocks `cat / head / grep -r`
  when icmg has an equivalent. Mechanical works better than reminders, but it's a
  blunt instrument; curious how others handle this.
- macOS arm64: source builds clean, but I don't own Apple hardware. If anyone wants
  to PR a prebuilt binary, I'll merge.

Latest release: https://github.com/ncmonx/icm-graph/releases/tag/v1.1.1
Docs: README on the repo (1-page tour up top)
No tracking, no telemetry, no account required.

Happy to answer anything in the thread.
```

**Posting tips:**
- Tuesday or Wednesday, 8:30–10:00 AM US Pacific.
- Avoid Friday (low traffic) and Monday (algorithm slow ramp).
- First 90 min decide front-page fate — be on standby to reply.
- Reply to every comment in the first 3 hours, even one-liners. Voting weight goes up.
- Don't link Ko-fi in the post body. HN flags it. Let users find it in the README.

**Headline alternatives if first one stalls (re-submission after 30 days OK):**
- `Show HN: I cut my Claude Code bill 85% with a CLI that filters everything`
- `Show HN: Icemage — receipts-driven context engineering for AI coding agents`
