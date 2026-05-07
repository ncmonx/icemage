# icmg — Unified Memory, Graph & Tkil Tool

Single C++17 binary unifying three developer tools:

- **ICM** — persistent memory across AI sessions (BM25 recall, scoring, TTL)
- **TGraph** — code knowledge graph (file scanner, dependency edges, context)
- **Tkil** — token-saving (formerly RTK) CLI proxy (filters noisy build/test/git output)

Plus: per-folder rules, structured data store, abbreviation engine, stored procedure library, import/export, visual graph, MCP server.

---

## ⚠️ Common Mistakes

| Mistake | Fix |
|---------|-----|
| `icmg` not found after build | Add `build/` to PATH or copy binary to `/usr/local/bin` |
| `icmg` works in terminal but NOT in Claude | MCP not configured — see [MCP Setup](#mcp-server-claude-integration) |
| Claude doesn't use `icmg_recall` / `icmg_store` | `.claude/mcp.json` missing in project root — create it |
| MCP configured but tools not appearing | Restart Claude Code after creating `mcp.json` |
| `graph scan` shows 0 nodes | Run from project root, not from `.icmg/` subdirectory |
| `recall` returns nothing | Run `icmg store` first — DB starts empty per project |
| Different project has no memory | Each project has its own `.icmg/data.db` — memory is not shared |

---

## Requirements

| Platform | Requirement |
|----------|-------------|
| Windows  | MSYS2/MinGW-w64, CMake 3.20+, Git |
| Linux    | GCC 10+ or Clang 12+, CMake 3.20+, Git |
| macOS    | Xcode CLT, CMake 3.20+, Git |

---

## Installation

### 1. Clone

```bash
git clone https://github.com/youruser/icm-graph.git
cd icm-graph
```

### 2. Build

**Windows (MSYS2 MinGW64 shell):**
```bash
export PATH="/c/msys64/mingw64/bin:/c/Program Files/CMake/bin:$PATH"
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target icmg -- -j4
```

**Linux / macOS:**
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target icmg -- -j4
```

Output: `build/icmg` (Linux/macOS) or `build/icmg.exe` (Windows).

### 3. Install to PATH

```bash
# Linux/macOS
sudo cp build/icmg /usr/local/bin/icmg

# Windows — copy to any directory on PATH, or add build/ to PATH
copy build\icmg.exe C:\Windows\System32\icmg.exe
```

### 4. Verify

```bash
icmg --version   # icmg 0.1.0
icmg --help
```

### 5. Test in a project

```bash
cd /path/to/any-project
icmg store "test" "hello world"
icmg recall "hello"             # should return the stored node
icmg run git status             # Tkil filter working
icmg doctor                     # full health check
```

> **Note:** `icmg` auto-creates `.icmg/data.db` in the current directory on first use. No extra setup needed for CLI use.

---

## Activating icmg in Claude Code (MCP)

> **This step is required** if you want Claude to use `icmg_recall`, `icmg_store`, and other tools automatically during a session.
> Without this step, `icmg` only works from the terminal — Claude won't use it.

### Step 1 — Create `.claude/mcp.json` in your project

```bash
mkdir -p .claude
cat > .claude/mcp.json << 'EOF'
{
  "mcpServers": {
    "icmg": {
      "command": "icmg",
      "args": ["--mcp-server"]
    }
  }
}
EOF
```

> One `mcp.json` per project. Repeat for every project where you want Claude to use `icmg`.

### Step 2 — Restart Claude Code

Close and reopen Claude Code in that project directory.

### Step 3 — Verify MCP is active

Type `/mcp` in Claude Code. You should see `icmg` listed with 14 tools:

```
icmg — connected
  icmg_recall, icmg_store, icmg_graph_context, icmg_graph_related,
  icmg_rule_apply, icmg_data_get, icmg_abbr_expand, icmg_abbr_list,
  icmg_sp_search, icmg_sp_context, icmg_sp_deps, icmg_cmd_suggest,
  icmg_project_switch, icmg_stats
```

If `icmg` does not appear: check that `icmg` is in your PATH (`icmg --version` must work), then restart Claude Code again.

---

## Storage Layout (per-project isolation)

```
~/.icmg/
  global.db       # project registry + global memory
  config.json     # feature flags
  icmg.log        # debug log

<project_root>/
  .icmg/
    data.db       # per-project memory, graph, rules, abbr, SP
    watcher.pid   # file watcher daemon PID (optional)
```

`icmg` auto-creates `.icmg/data.db` in your current working directory on first use.

> **Each project has its own isolated database.** Memory, graph nodes, rules, and abbreviations stored in project A are not visible in project B. To share data across projects, use `icmg --project <name>` or `icmg import/export`.

---

## Quick Start

### Memory (ICM)

```bash
# Store a memory node
icmg store "auth-decision" "We use JWT with 15min expiry, refresh stored in HttpOnly cookie" \
    --importance high --kw "jwt,auth,cookie"

# Recall by semantic query
icmg recall "authentication token"

# JSON output
icmg recall "jwt" --json --limit 5

# Forget a node (requires --yes to confirm)
icmg recall "old topic" --json          # get the id
icmg forget 42 --yes
```

### Knowledge Graph

```bash
# Scan source directory into graph
icmg graph scan src/ --depth 20

# Get context for a file (imports, dependents, symbols)
icmg graph context src/core/db.cpp --json

# Find related files
icmg graph related src/core/db.cpp --limit 5

# List all scanned files, filtered by language
icmg graph list --lang cpp --json

# Find orphan files (no inbound edges)
icmg graph orphans

# Detect circular dependencies
icmg graph cycles --lang cpp

# Open visual graph in browser
icmg viz open

# File watcher daemon (auto-rescans on change)
icmg graph-watch src/
icmg graph-watch-status
icmg graph-stop
```

### Tkil — Token-Saving (formerly RTK) Command Proxy

```bash
# Run any command through the Tkil filter (filters noisy output)
icmg run git log --oneline -50
icmg run cargo build
icmg run npm test

# JSON output with metadata
icmg run --json git diff HEAD~1

# Show command frequency stats
icmg cmd suggest git --json
icmg cmd suggest --json          # all commands

# Dry-run: preview what would be filtered
icmg run --dry-run cmake --build .
```

Supported filters: `git log/diff/status/show`, `cargo/cmake/make build`, `cargo test / npm test`, `grep/rg`.

### Per-folder Rules

```bash
# Add rule: scope_path type name content
icmg rule add "src/api/" "coding" "no-console-log" "Use logger.info() not console.log()"
icmg rule add "/" "arch" "no-circular-deps" "Never create circular module dependencies"

# List rules applicable to a path (inherits from parent scopes)
icmg rule list "src/api/users/" --json

# Apply (show full inheritance chain for a file)
icmg rule apply "src/api/users/auth.ts" --json

# Enable/disable
icmg rule disable 3
icmg rule enable 3
```

### Abbreviations

```bash
# Learn abbreviation
icmg abbr learn "db" "database" --domain general
icmg abbr learn "auth" "authentication" --domain backend

# Expand text
icmg abbr expand "connect to db with auth"
# → "connect to database with authentication"

# List all
icmg abbr list --json
```

### Stored Procedures

```bash
# Add from SQL file
icmg sp add "GetActiveUsers" queries/GetActiveUsers.sql --db mssql

# Show
icmg sp show "GetActiveUsers" --json

# Dependency tree
icmg sp deps "GetActiveUsers"

# Search
icmg sp search "user" --json

# Lint for issues
icmg sp lint "GetActiveUsers"

# Diff two versions
icmg sp diff "GetActiveUsers" 1 2

# Generate call template
icmg sp template "GetActiveUsers"
```

### Structured Data

```bash
# Store a model definition
icmg data add "model" "UserModel" '{"fields":["id","name","email","role"]}'

# Show
icmg data show "UserModel" --json

# Search
icmg data search "User" --json
```

### Import / Export

```bash
# Import from another ICM export
icmg import icm backup.json

# Import from Graphify export
icmg import graphify graphify-export.json

# Import from generic JSON
icmg import json data.json

# Export to JSON
icmg export json --output backup.json

# Export to CSV
icmg export csv --output nodes.csv
```

### Multi-project Registry

```bash
# Register a project
icmg project add "my-api" /path/to/my-api

# List registered projects
icmg project list

# Access another project's data
icmg --project my-api recall "database schema"

# Remove project from registry
icmg project remove "my-api"
```

### Health Check

```bash
icmg doctor
```

---

## MCP Server (Claude Integration)

> **Setup instructions are at the top of this README** — see [Activating icmg in Claude Code](#activating-icmg-in-claude-code-mcp).

### Available MCP Tools (14)

| Tool | Description |
|------|-------------|
| `icmg_recall` | Recall memory nodes by semantic query |
| `icmg_store` | Store a new memory node |
| `icmg_graph_context` | Get file context (imports, symbols, dependents) |
| `icmg_graph_related` | Find related files |
| `icmg_rule_apply` | Get applicable rules for a file path |
| `icmg_data_get` | Retrieve structured data by name |
| `icmg_abbr_expand` | Expand abbreviations in text |
| `icmg_abbr_list` | List all known abbreviations |
| `icmg_sp_search` | Search stored procedures |
| `icmg_sp_context` | Get SP details and parameters |
| `icmg_sp_deps` | Get SP dependency tree |
| `icmg_cmd_suggest` | Get frequent command suggestions |
| `icmg_project_switch` | Switch active project context |
| `icmg_stats` | Get usage statistics |

---

## Running Tests

```bash
# Unit tests only (ctest)
cmake -B build && cmake --build build
cd build && ctest --output-on-failure

# All integration test suites
bash tests/run_all.sh

# Individual suites
ICMG=./build/icmg.exe bash tests/test_icm.sh
ICMG=./build/icmg.exe bash tests/test_graph.sh
ICMG=./build/icmg.exe bash tests/test_tkil.sh
ICMG=./build/icmg.exe bash tests/test_features.sh
ICMG=./build/icmg.exe bash tests/test_mcp.sh
ICMG=./build/icmg.exe bash tests/test_security.sh
ICMG=./build/icmg.exe bash tests/test_performance.sh
```

Expected: 14/14 unit tests + 44/44 integration tests pass.

---

## Architecture Notes

- **Storage**: SQLite WAL mode, one `.db` per project
- **Migrations**: Embedded SQL — works from any directory without `migrations/` present
- **Commands**: Registered via `ICMG_REGISTER_COMMAND` static-init macro; linker requires `--whole-archive`
- **MCP tools**: Registered via `ICMG_REGISTER_MCP_TOOL` static-init macro; same linker requirement
- **ICM scoring**: BM25 × recency decay × frequency log × importance multiplier
- **Graph edges**: Unresolved imports (dst=-1) skipped; FK enforced on `graph_edges`

---

## License

MIT
