# Reddit posts — r/ClaudeAI + r/LocalLLaMA + r/programming

Different copy per sub. Reddit detects cross-posts; don't paste identical text.

---

## r/ClaudeAI

**Title:** I built a CLI that cut my Claude Code bill by 85%. Open source, no account.

**Body:**

```
TLDR: Icemage (icmg) — a 17 MB binary that sits in front of Claude Code as an MCP server + hook handler. It packs 4 KB context bundles instead of letting Claude Read whole files, filters noisy command output before the model sees it, and tracks every token saved in a receipts table.

What it does:
• `icmg pack "<task>"` — gets you a 4 KB bundle of relevant code + memory + diff
• Hooks intercept Read / Glob / Grep / Bash / WebFetch — capped + summarized
• Local SQLite memory of past decisions (BM25 + cosine recall, no cloud)
• Reversible glossary compression for big outputs
• `icmg savings` shows what you saved this week

Numbers from my daily workflow:
• Big-file Read: −83 %
• Build/test logs: −92 %
• Schema dumps: −99 %
• Re-explaining context after /clear: −100 % (memory persists)

Compounded: 85–95 % per turn.

Tech: C++17, single binary, SQLite, ONNX MiniLM embedder, tree-sitter for 10+ langs. 71/71 ctest pass on Win + Linux. Apache-2.0.

Repo: https://github.com/ncmonx/icm-graph
Latest: v1.1.1 (just shipped — auto-installs as resident service so popups are gone)

Built by me, solo, in spare evenings. Looking for feedback from heavy Claude Code users — what's still inefficient? What did I miss?
```

**Comment plan:**
- Pin top comment: "Common questions answered below" — list 5 FAQs.
- Be replying within 30 min of post for first 4 hours.
- If someone asks "how is this different from Aider's context management?" — answer with concrete: "Aider builds a repo map at startup; icmg builds per-task slices with BM25 reranking on each pack call, plus persistent memory across `/clear`."

---

## r/LocalLLaMA

**Title:** Show: token-efficiency tooling that works with any MCP-speaking LLM (Claude, local models, Cursor)

**Body:**

```
Open-sourcing the CLI I've been using to keep my local-LLM workflows tight.

Icemage (`icmg`) is an MCP server + hook handler that any MCP-aware client can talk to (Claude Code/Desktop, Cursor, Cline, Continue, and your own LMStudio/Ollama wrappers if you expose them through MCP).

The angle for local-LLM folks: small context windows hurt more, so the compounded reduction matters more.

Built-in:
- BM25 + 384-dim cosine recall over a local SQLite memory store. ONNX MiniLM-L6 embedder bundled. No vector cloud.
- Tree-sitter parsing for 10+ languages → symbol graph stored locally; `pack` walks it BFS.
- Per-command output filters (git, cargo, npm, pytest, ctest) — drop boilerplate, keep signal.
- Reversible glossary compression for SQL/JSON/log dumps. `icmg expand` round-trips losslessly.

100% local. No telemetry. No phone-home. SQLite WAL on disk; you can grep your own memory.

71/71 ctest on Win + Linux. Single binary, ~17 MB Win, ~4 MB Linux (no DLL bundle needed).

Repo: https://github.com/ncmonx/icm-graph

Use case I haven't seen others cover: model-agnostic context engineering with measured savings. The receipts table (`icmg savings`) gives per-tool, per-day token-equivalent saved — calibrate it to your local model's tokenizer if Claude defaults are off.

Welcome critique on the recall ranking (BM25 + recency + importance + cosine blend with tunable weights; defaults from my workflow which may be too Claude-tuned).
```

---

## r/programming

**Title:** Shipping a single-binary C++ CLI as your developer-tool product (lessons from Icemage)

**Body:**

```
After 1.5 years building a context/memory CLI for AI coding agents (Icemage, https://github.com/ncmonx/icm-graph), here's what I'd tell anyone shipping a developer-facing single-binary tool:

1. Static-link everything you can. MSYS2 MinGW + `-static-libgcc -static-libstdc++ -static -lwinpthread` removes 80% of "DLL hell" support tickets. The remaining DLLs (ONNX, tree-sitter, wasmtime) ship in the zip.

2. SQLite WAL is your friend. Two DBs (project-local + global), forward-only migrations compiled into the binary as a C-string array. Zero "where's my schema?" pain.

3. Registry pattern + static-init self-registration. One macro (`ICMG_REGISTER_COMMAND("name", Class)`) — adding a new command is one new .cpp file, no factory edit, no dispatch table to update. CMake `GLOB_RECURSE` picks it up.

4. Hooks > prompts. For agent-tool integration: implement Claude Code's hook spec. Reminders in system prompts drift after 5–10 turns; hard-deny via `permissionDecision: "deny"` doesn't.

5. The CI matrix tax is real for solo devs. Burned through GH Actions free-tier in 8 weeks at $0 marginal. Switched to local WSL2 Linux build + manual `gh release create`. Releases are slower but $0.

6. Receipts beat marketing. `icmg savings` shows users what they saved. Vendors who claim X% reduction without per-user numbers don't last.

71/71 ctest. ~30K LOC C++17 + ~10K LOC tests. Apache-2.0.

Repo: https://github.com/ncmonx/icm-graph

AMA on any of the above.
```

**Note on r/programming:** moderators delete posts that read as marketing. The framing here is "lessons learned" with the tool as context, not "buy my thing." Comply.

---

## Reddit posting strategy

- **Stagger posts by 48 hours.** Hitting all 3 subs same day looks spammy and Reddit's spam filter flags it.
- **Order:** r/ClaudeAI first (most engaged with the pain), r/LocalLLaMA second (technical critique), r/programming last (broadest audience, hardest moderation).
- **Times (US ET):** Tue–Thu, 8:00–10:00 AM ET. Avoid weekends and Mondays.
- **Avatar + flair:** make sure your profile has a non-default avatar and at least 50 comment karma elsewhere. Empty accounts get auto-removed.
- **Respond to every comment in first 4 hours.** Reddit's algorithm weights engagement heavily.
- **Don't link Ko-fi in the post.** Reddit treats donation links as self-promo. Let users find it on the GitHub README. Mods sometimes remove posts with donation links.
