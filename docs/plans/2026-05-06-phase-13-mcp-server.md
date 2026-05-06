# Phase 13: MCP Server (stdio transport)

> **For Claude:** REQUIRED SUB-SKILL: Use `executing-plans` or `subagent-driven-development` to implement this plan.

**Goal:** Expose semua icmg capabilities sebagai MCP tools via stdio transport — Claude Code bisa panggil langsung tanpa user ketik command.
**Architecture:** JSON-RPC 2.0 over stdio. Tool registry map ke icmg internal functions. Tools auto-registered via ICMG_REGISTER_MCP_TOOL.
**Tech Stack:** C++17, JSON (manual parser/serializer), stdio
**Assumptions:** Phase 01-12 selesai. Claude Code MCP integration tersedia.

---

### Task 1: Minimal JSON serializer/deserializer

**Files:**
- Create: `src/mcp/json.hpp`
- Create: `src/mcp/json.cpp`

Minimal JSON support (tidak perlu full library):
- Serialize: string, int, double, bool, null, array, object
- Deserialize: parse top-level object + nested
- Hanya support UTF-8 ASCII (no unicode escape needed)

---

### Task 2: MCP protocol handler

**Files:**
- Create: `src/mcp/server.hpp`
- Create: `src/mcp/server.cpp`

```cpp
class McpServer {
public:
    void run();  // stdio loop: read line → parse JSON-RPC → dispatch → write response

private:
    void handleInitialize(const JsonObj& req);
    void handleListTools(const JsonObj& req);
    void handleCallTool(const JsonObj& req);
    void sendResponse(const JsonObj& res);
    void sendError(int id, int code, const std::string& msg);
};
```

**JSON-RPC methods handled:**
- `initialize` → return server capabilities
- `tools/list` → list all registered MCP tools
- `tools/call` → dispatch to tool handler

---

### Task 3: BaseMcpTool interface

**Files:**
- Create: `src/mcp/base_mcp_tool.hpp`

```cpp
struct McpToolParam {
    std::string name;
    std::string type;       // string|number|boolean
    std::string description;
    bool required = false;
};

class BaseMcpTool {
public:
    virtual ~BaseMcpTool() = default;
    virtual std::string name() const = 0;
    virtual std::string description() const = 0;
    virtual std::vector<McpToolParam> params() const = 0;
    virtual std::string call(const std::unordered_map<std::string, std::string>& args,
                              core::Db& db) = 0;
};
```

Registration: `ICMG_REGISTER_MCP_TOOL("icmg_recall", RecallTool);`

---

### Task 4: Core MCP tools implementation

**Files:**
- Create: `src/mcp/tools/recall_tool.cpp`
- Create: `src/mcp/tools/store_tool.cpp`
- Create: `src/mcp/tools/graph_context_tool.cpp`
- Create: `src/mcp/tools/graph_related_tool.cpp`
- Create: `src/mcp/tools/rule_apply_tool.cpp`
- Create: `src/mcp/tools/data_get_tool.cpp`
- Create: `src/mcp/tools/abbr_expand_tool.cpp`
- Create: `src/mcp/tools/abbr_list_tool.cpp`
- Create: `src/mcp/tools/sp_search_tool.cpp`
- Create: `src/mcp/tools/sp_context_tool.cpp`
- Create: `src/mcp/tools/sp_deps_tool.cpp`
- Create: `src/mcp/tools/cmd_suggest_tool.cpp`
- Create: `src/mcp/tools/project_switch_tool.cpp`
- Create: `src/mcp/tools/stats_tool.cpp`

**Tools list untuk Claude:**

| MCP Tool | Input | Output |
|---|---|---|
| `icmg_recall` | query, limit?, topic? | ranked memory nodes JSON |
| `icmg_store` | topic, content, importance? | stored node ID |
| `icmg_graph_context` | path | file context + symbols JSON |
| `icmg_graph_related` | path, limit? | related files JSON |
| `icmg_rule_apply` | file_path | applicable rules JSON |
| `icmg_data_get` | name, type? | structured data JSON |
| `icmg_abbr_expand` | text | expanded text |
| `icmg_abbr_list` | domain? | abbreviations JSON |
| `icmg_sp_search` | query, limit? | matching SPs JSON |
| `icmg_sp_context` | sp_name | SP details + params JSON |
| `icmg_sp_deps` | sp_name | dependency tree JSON |
| `icmg_cmd_suggest` | prefix?, limit? | ranked commands JSON |
| `icmg_project_switch` | project_name | confirmation |
| `icmg_stats` | (none) | DB stats JSON |

---

### Task 5: MCP server mode CLI

**Files:**
- Modify: `src/main.cpp`

```
icmg --mcp-server     # start MCP server mode (stdio)
icmg --mcp-server --project smart-home  # dengan project context
```

**MCP config untuk Claude Code:**
```json
// .claude/mcp.json
{
  "mcpServers": {
    "icmg": {
      "command": "icmg",
      "args": ["--mcp-server"],
      "transport": "stdio"
    }
  }
}
```

---

### Task 6: Verify MCP integration

**Step 1: Test via stdio manually**
```bash
echo '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}' | ./build/icmg --mcp-server
echo '{"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}' | ./build/icmg --mcp-server
echo '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"icmg_stats","arguments":{}}}' | ./build/icmg --mcp-server
```
Expected: valid JSON-RPC responses.

**Step 2: Buat .claude/mcp.json**
```bash
mkdir -p .claude
echo '{"mcpServers":{"icmg":{"command":"./build/icmg","args":["--mcp-server"],"transport":"stdio"}}}' > .claude/mcp.json
```

**Step 3: Commit**
```bash
git add src/mcp/ .claude/mcp.json
git commit -m "feat: phase-13 MCP server with 14 Claude-callable tools"
```

---

## Amendments from Security & Architecture Review

### CRITICAL Fixes

**A1 — Pakai nlohmann/json bukan custom parser**
Task 1 (json.hpp) DIGANTI:
```cpp
// TIDAK perlu src/mcp/json.hpp custom
// Pakai third_party/nlohmann/json.hpp dari Phase 01

#include <nlohmann/json.hpp>
using json = nlohmann::json;

// MCP server parse:
try {
    auto req = json::parse(line);
    handleRequest(req);
} catch (const json::parse_error& e) {
    sendError(-32700, "Parse error: " + std::string(e.what()));
}
```

**A2 — Input validation schema per tool**
Setiap MCP tool wajib implementasikan:
```cpp
class RecallTool : public BaseMcpTool {
    void validateArgs(const json& args) override {
        if (!args.contains("query") || !args["query"].is_string())
            throw McpError(-32602, "Missing required param: query (string)");
        if (args["query"].get<std::string>().size() > 1000)
            throw McpError(-32602, "query exceeds 1000 char limit");
        if (args.contains("limit") && !args["limit"].is_number_integer())
            throw McpError(-32602, "limit must be integer");
    }
};
```

**A3 — Path validation untuk path-based tools**
`icmg_graph_context`, `icmg_rule_apply`:
```cpp
auto path = args["path"].get<std::string>();
path = core::path_utils::canonicalize(path);
if (!core::path_utils::isWithinRoot(path, projectRoot)) {
    throw McpError(-32602, "path outside project root");
}
```

### HIGH Fixes

**A4 — MCP tool return type normalization**
Semua tool return JSON string (serialize result ke JSON):
```cpp
// JANGAN: return "some plain text"
// WAJIB: return json{{"result", data}}.dump()

// Base class enforce ini:
class BaseMcpTool {
    virtual json callImpl(const json& args, core::Db& db) = 0;
    std::string call(const json& args, core::Db& db) final {
        return callImpl(args, db).dump();
    }
};
```

**A5 — Resource limits**
```cpp
// Di McpServer::handleCallTool():
// Limit total storage via icmg_store
if (toolName == "icmg_store") {
    auto count = db.queryScalar("SELECT COUNT(*) FROM memory_nodes");
    if (count > 10000) {
        return sendError(-32603, "Memory store full (10000 nodes). Run icmg memory purge.");
    }
}
// Content size limit:
if (args.value("content", "").size() > 1024*1024) {
    return sendError(-32602, "content exceeds 1MB limit");
}
```

**A6 — Audit log untuk MCP calls**
```sql
CREATE TABLE mcp_audit_log (
    id INTEGER PRIMARY KEY,
    tool_name TEXT NOT NULL,
    args_summary TEXT,      -- topic/path only, NOT full content
    created_at INTEGER
);
```
Log setiap tool call: nama tool + metadata (bukan content sensitif).

### MEDIUM Fixes

**A7 — Nesting depth limit untuk JSON input**
```cpp
// nlohmann/json sudah support ini:
json::parse(line, nullptr, true, true);  // ignore_comments, allow_exceptions
// Tambahkan manual depth check:
if (json::parser_callback_t depth > 32) throw ParseError("max nesting exceeded");
```
