# icmg — Unified Memory, Graph & RTK Tool

Single C++17 binary unifying three developer tools:

- **ICM** — persistent memory across AI sessions (BM25 recall, scoring, TTL)
- **Graphify** — code knowledge graph (file scanner, dependency edges, context)
- **RTK** — token-saving CLI proxy (filters noisy build/test/git output)

Plus: per-folder rules, structured data store, abbreviation engine, stored procedure library, import/export, visual graph, MCP server.

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

No configuration required — `icmg` auto-initializes its database on first use.

---

## Storage Layout

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

### RTK — Token-Saving Command Proxy

```bash
# Run any command through the RTK filter (filters noisy output)
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

`icmg` can run as an MCP server, giving Claude direct access to memory, graph, and SP tools.

### Setup

Add to your Claude Code `.claude/mcp.json`:

```json
{
  "mcpServers": {
    "icmg": {
      "command": "icmg",
      "args": ["--mcp-server"],
      "description": "ICM-Graph unified memory + graph + SP + abbreviation server"
    }
  }
}
```

Or use the pre-generated config in this repo:

```bash
# Already exists at .claude/mcp.json — copy to your project
cp .claude/mcp.json /path/to/your/project/.claude/mcp.json
```

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
ICMG=./build/icmg.exe bash tests/test_rtk.sh
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
