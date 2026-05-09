// Phase 67 T30: `icmg mcp list` — surface all registered MCP tools.
//
// Discoverability: 28+ MCP tools registered; users have no way to see what
// Claude can call short of reading mcp.json or src/mcp/tools/. This cmd
// queries the registry directly.

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../mcp/base_mcp_tool.hpp"
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace icmg::cli {

class McpListCommand : public BaseCommand {
public:
    std::string name()        const override { return "mcp"; }
    std::string description() const override {
        return "MCP tool management (list)";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg mcp <action>\n\n"
            "Actions:\n"
            "  list [--json]      Show all registered MCP tools + descriptions\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || hasFlag(args, "--help")) { usage(); return 0; }
        std::string action = args[0];
        if (action != "list") {
            std::cerr << "icmg mcp: unknown action '" << action << "'\n";
            usage();
            return 1;
        }
        bool json_out = hasFlag(args, "--json");
        auto& reg = core::Registry<mcp::BaseMcpTool>::instance();
        auto names = reg.keys();
        std::sort(names.begin(), names.end());

        if (json_out) {
            std::cout << "[";
            for (size_t i = 0; i < names.size(); ++i) {
                auto handler = reg.create(names[i]);
                if (i) std::cout << ",";
                std::cout << "{\"name\":\"" << names[i]
                          << "\",\"description\":\""
                          << escapeJson(handler->description()) << "\"}";
            }
            std::cout << "]\n";
            return 0;
        }
        std::cout << "Registered MCP tools (" << names.size() << "):\n";
        for (auto& n : names) {
            auto handler = reg.create(n);
            std::cout << "  " << std::left << std::setw(28) << n
                      << handler->description().substr(0, 80) << "\n";
        }
        return 0;
    }

private:
    static std::string escapeJson(const std::string& s) {
        std::string out;
        for (char c : s) {
            if (c == '"' || c == '\\') { out += '\\'; out += c; }
            else if (c == '\n') out += "\\n";
            else if (c >= 0x20)  out += c;
        }
        return out;
    }
};

ICMG_REGISTER_COMMAND("mcp", McpListCommand);

} // namespace icmg::cli
