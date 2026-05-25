# Twitter / X thread — 8 tweets, ~280 chars each

**Pin to profile after posting. Engage replies in first 4 hours.**

---

**1/8 (hook)**

I cut my Claude Code token bill by 85% with a single 17 MB CLI.

It's open-source. One binary. No account.

Here's what it does, why it works, and the receipts 👇

🔗 https://github.com/ncmonx/icm-graph

---

**2/8 (the pain everyone knows)**

Be honest:

• Read one file → 30K tokens gone
• "Thinking" 8K tokens for a one-line rename
• /clear wipes 4 hours of context
• Same bug solved 3 times this week
• 200 lines of `npm test` output the model didn't need

It compounds. Daily.

---

**3/8 (what icmg does in one image)**

[ATTACH SCREENSHOT of `icmg savings` dashboard]

7 layers that stack:

→ Pack 4KB context bundles (not whole files)
→ Filter subprocess output 60–90%
→ Local SQLite memory of past decisions
→ Reversible glossary compression
→ Cache WebFetch + URL pulls
→ Prompt-cache batching
→ Receipts table

---

**4/8 (the moat: receipts)**

Most "token-saving" tools tell you they save tokens.

icmg shows you the bytes-saved table per project, per command type, per day.

`icmg savings` outputs JSON. Hook it into your billing dashboard if you're feeling fancy.

---

**5/8 (works with what you already use)**

✅ Claude Code (CLI + Desktop, MCP server)
✅ Cursor (MCP)
✅ Cline / Continue / Aider
✅ Direct Anthropic / OpenAI API
✅ Local LLMs via standard MCP

28 MCP tools. Drop-in. No SDK lock-in.

---

**6/8 (technical credibility)**

C++17, single binary
71/71 ctest pass Windows + Linux
SQLite WAL, BM25 + cosine recall
ONNX MiniLM-L6 embedder bundled
Tree-sitter for 10+ languages
Apache-2.0

No telemetry, no account, no cloud sync. Local-first by design.

---

**7/8 (numbers)**

Real measurements on my daily workflow:

• Big-file read: −83%
• Build/test logs: −92%
• SQL/table dumps: −99%
• "Thinking" passes: −92%
• Stable preamble: −90% via cache
• Repeat queries: −100% local cache

Compounded: 85–95% per turn.

---

**8/8 (CTA)**

If you ship code with AI agents and your monthly bill is ugly — try it tonight.

Install:
```
curl -L https://github.com/ncmonx/icm-graph/releases/latest/...
```

Star ⭐ if it saves you anything. PRs welcome.

Built by one dev. Stays open. ko-fi.com/ncmonx if it helps you.

---

**Hashtags (last tweet only, or in replies):**
`#ClaudeCode #LLM #DevTools #AItools #BuildInPublic`

**Quote-tweet targets (after posting, find replies/QTs to engage):**
- @AnthropicAI announcements
- Any tweet complaining about Claude Code costs
- @cursor_ai, @codecursor user complaints
- r/ClaudeAI screenshots cross-posted to X

**Reply playbook:**
- "How does it compare to Aider?" → screenshot comparison table from `icmg compare` (TODO: build this)
- "Does it work on macOS?" → "Source builds clean on arm64. Prebuilt binary blocked on Apple hardware; if anyone wants to ship a PR I'll merge in 24h"
- "Is data sent anywhere?" → "Zero telemetry, zero cloud sync. All SQLite local. Verify with `icmg config show`"
- "Why C++?" → "Single binary, no Python deps, sub-10ms hook latency"
