#include "../base_mcp_tool.hpp"
#include "../../core/registry.hpp"
#include "../../imem/memory_store.hpp"
#include "../../imem/memory_node.hpp"
#include "../../core/memory_scope.hpp"   // #292 namespace / trust
#include "../../core/store_guard.hpp"    // #292 secret filter
#include <ctime>

namespace icmg::mcp {

class StoreTool : public BaseMcpTool {
public:
    std::string name()        const override { return "icmg_store"; }
    std::string description() const override {
        return "Store a memory node. Use for decisions, resolved errors, "
               "preferences, and significant task completions.";
    }
    std::vector<McpToolParam> params() const override {
        return {
            {"topic",      "string",  "Topic label (namespace-style, e.g. errors-resolved)", true},
            {"content",    "string",  "Memory content",                                     true},
            {"importance", "string",  "low|med|high|crit (default: med)",                   false},
            {"keywords",   "string",  "Comma-separated keywords for retrieval",              false},
            {"namespace",  "string",  "Tenant namespace (user:12, team:x; default shared) (#292)", false},
            {"source",     "string",  "Origin: user|agent|web|file|... (web/file -> untrusted)", false},
            {"trust",      "string",  "trusted|untrusted (overrides the source default)",  false},
            {"ttl",        "string",  "Expire after e.g. 12h, 7d, 2w",                      false},
        };
    }

protected:
    void validateArgs(const json& args) override {
        requireStr(args, "topic",   200);
        requireStr(args, "content", 1024 * 1024);
    }

    bool isMutating() const override { return true; }

    json callImpl(const json& args, core::Db& db) override {
        imem::MemoryNode node;
        node.topic      = getStr(args, "topic");
        node.content    = getStr(args, "content");
        node.keywords   = getStr(args, "keywords");
        node.importance = imem::importanceFromName(getStr(args, "importance", "med"));
        node.source     = getStr(args, "source", "mcp");

        std::vector<std::string> ns;
        std::string err;
        if (!core::parseNamespaces(getStr(args, "namespace"), &ns, &err))
            throw McpError(-32602, err);
        if (ns.size() > 1) throw McpError(-32602, "namespace takes ONE name on store");
        node.ns = ns.empty() ? core::storeNamespace() : ns[0];

        const std::string trust = getStr(args, "trust");
        if (!trust.empty()) {
            const char* t = core::parseTrust(trust);
            if (!t) throw McpError(-32602, "trust must be trusted|untrusted");
            node.trust = t;
        }
        const std::string ttl = getStr(args, "ttl");
        if (!ttl.empty()) {
            const int64_t secs = core::parseTtlSeconds(ttl);
            if (secs <= 0) throw McpError(-32602, "bad ttl (use 12h, 7d, 2w)");
            node.expires_at = (int64_t)std::time(nullptr) + secs;
        }

        imem::MemoryStore store(db);
        int64_t id = 0;
        try {
            id = store.store(node, false);
        } catch (const core::SecretRejectedError& e) {
            throw McpError(-32602, e.what());
        }
        return {{"id", id}, {"topic", node.topic}, {"status", "stored"},
                {"namespace", core::namespaceLabel(node.ns)}};
    }
};

ICMG_REGISTER_MCP_TOOL("icmg_store", StoreTool);

} // namespace icmg::mcp
