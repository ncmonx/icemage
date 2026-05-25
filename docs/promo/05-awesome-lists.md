# Awesome-list PRs — drip traffic for zero ongoing effort

For each list, PR a one-line entry under the most appropriate section. Sort alphabetically within section unless the README says otherwise.

---

## Target lists

### 1. https://github.com/sindresorhus/awesome (the meta-list)

Not directly — need a curated sub-list to be linked here first.

### 2. https://github.com/agarrharr/awesome-cli-apps

- Section: `Development → Code Search` or `Development → Tools`
- Entry line:

```markdown
- [icmg](https://github.com/ncmonx/icm-graph) - Context-graph + memory CLI for AI coding agents. Cuts token spend 70-90% via packs, output filters, and local SQLite recall.
```

### 3. https://github.com/awesome-selfhosted/awesome-selfhosted

- Section: `Software Development → Tools`
- Entry: same as above with `- 'Source: C++' 'License: Apache-2.0'` suffix per repo style.

### 4. https://github.com/topics/claude-code  (GH Topics — auto-list)

- Add `claude-code`, `mcp-server`, `ai-coding`, `token-savings`, `context-engineering`, `claude`, `anthropic` topics to YOUR OWN repo's "About" sidebar.
- This is the cheapest highest-leverage step. 5 min.

### 5. https://github.com/dair-ai/Awesome-LLM-tools

- Section: `Agents` or `Tools`
- Entry:

```markdown
- [Icemage](https://github.com/ncmonx/icm-graph) - Token-efficiency CLI for any LLM coding agent (Claude Code, Cursor, Cline). Local-first MCP server with receipts.
```

### 6. https://github.com/Hannibal046/Awesome-LLM

- Section: `Tools → CLI` or `Tools → Agents`
- Same entry adjusted.

### 7. https://github.com/mahseema/awesome-ai-tools

- Section: `Developer Tools`
- Same.

### 8. https://github.com/punkpeye/awesome-mcp-servers

- Section: `Productivity` or `Code`
- Entry tailored:

```markdown
- [icmg](https://github.com/ncmonx/icm-graph) - 28-tool MCP server providing context packs, BM25+cosine recall, output compression, and project-memory primitives. Apache-2.0.
```

### 9. https://github.com/wong2/awesome-mcp-servers

- Same section + same entry as above.

### 10. https://github.com/cline/awesome-cline (if exists for Cline ecosystem)

- Section: `Tools` or `Add-ons`
- Same entry adjusted to mention Cline compatibility.

---

## PR template (reuse for all)

**PR title:** `Add icmg — token-efficient context CLI for AI coding agents`

**PR description:**

```
Adds [icmg](https://github.com/ncmonx/icm-graph) to the <SECTION-NAME> section.

icmg is a single-binary CLI that sits between developers and AI coding agents
(Claude Code, Cursor, Cline). It packs context bundles, filters subprocess
output, maintains local SQLite memory, and tracks every token saved.

- Open source (Apache-2.0)
- 71/71 ctest, Win + Linux prebuilt binaries
- Single binary, no runtime deps (Python/Node)
- 28 MCP tools, drop-in for any MCP-aware client
- Actively maintained (latest release: v1.1.1, <date>)
- No telemetry, no account required

Repo: https://github.com/ncmonx/icm-graph
Latest release: https://github.com/ncmonx/icm-graph/releases/latest

I'm the maintainer. Happy to revise the entry to fit the list's style.
```

---

## Submission tips

- **One PR at a time.** Don't batch — each list maintainer has different style preferences; you'll get review comments to address.
- **Read the contribution guide.** Many lists require alphabetical sorting, specific line format (e.g. `\`backticks\` around the name`), license + tag suffixes. Following it boosts merge probability >80%.
- **Watch the PR for 3 days.** Most awesome-list PRs merge in 24–72 hours if format is correct.
- **If rejected** — usually format issue. Fix and re-submit, don't argue.
- **After merge** — the entry will be on the list permanently. Don't go back to "update" it unless your repo URL changes. Each edit needs another PR.

---

## After-merge cadence

These lists drive **drip traffic** — 5–50 visitors / month each.
The point isn't one big spike; it's that they keep delivering after you stop promoting.

10 awesome lists × 20 visitors/month avg × 5% star conversion = 100 stars/month passive.
