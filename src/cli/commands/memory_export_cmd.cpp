// Phase XX: `icmg memory export`
// Dump all non-deleted memory nodes to JSON (default) or Markdown.
// Usage:
//   icmg memory export [--format json|md] [--out <file>]
//
// Fields exported: id, topic, zone, content, created_at, importance.

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../core/json_safe.hpp"
#include "../../imem/memory_store.hpp"
#include "../../imem/memory_node.hpp"
#include <nlohmann/json.hpp>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace icmg::cli {

class MemoryExportCommand : public BaseCommand {
public:
    std::string name()        const override { return "memory-export"; }
    std::string description() const override {
        return "Export all memory nodes to JSON or Markdown";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg memory export [options]\n\n"
            "Options:\n"
            "  --format json|md   Output format (default: json)\n"
            "  --out <file>       Write to file instead of stdout\n"
            "  --help             Show this help\n\n"
            "Fields exported: id, topic, zone, content, created_at, importance.\n"
            "Only non-deleted nodes are included.\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (hasFlag(args, "--help")) { usage(); return 0; }

        std::string fmt = flagValue(args, "--format", "json");
        if (fmt != "json" && fmt != "md") {
            std::cerr << "icmg memory export: unknown --format '" << fmt
                      << "' (choose json or md)\n";
            return 1;
        }
        std::string outPath = flagValue(args, "--out");

        // Open DB and load nodes.
        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        imem::MemoryStore store(db);
        const auto nodes = store.all();

        // Build output string.
        std::string output = (fmt == "json") ? toJson(nodes) : toMarkdown(nodes);

        // Write to file or stdout.
        if (outPath.empty()) {
            std::cout << output;
            // Ensure trailing newline.
            if (output.empty() || output.back() != '\n') std::cout << '\n';
        } else {
            std::ofstream ofs(outPath, std::ios::binary);
            if (!ofs) {
                std::cerr << "icmg memory export: cannot open output file: " << outPath << "\n";
                return 1;
            }
            ofs << output;
            if (output.empty() || output.back() != '\n') ofs << '\n';
            std::cerr << "icmg memory export: wrote " << nodes.size()
                      << " node(s) to " << outPath << "\n";
        }
        return 0;
    }

private:
    // ------------------------------------------------------------------ JSON
    static std::string toJson(const std::vector<imem::MemoryNode>& nodes) {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& n : nodes) {
            arr.push_back({
                {"id",         n.id},
                {"topic",      n.topic},
                {"zone",       n.zone},
                {"content",    n.content},
                {"created_at", n.created_at},
                {"importance", n.importance},
            });
        }
        return core::safeDump(arr, 2);
    }

    // ------------------------------------------------------------ Markdown
    static std::string toMarkdown(const std::vector<imem::MemoryNode>& nodes) {
        std::ostringstream oss;
        oss << "# icmg memory export\n\n";
        oss << "Total: " << nodes.size() << " node(s)\n\n";
        for (const auto& n : nodes) {
            oss << "---\n\n";
            oss << "## " << (n.topic.empty() ? "(no topic)" : n.topic) << "\n\n";
            oss << "- **id**: " << n.id << "\n";
            oss << "- **zone**: " << n.zone << "\n";
            oss << "- **importance**: " << n.importance << "\n";
            // Format created_at as ISO-8601 date if non-zero.
            if (n.created_at > 0) {
                std::time_t t = static_cast<std::time_t>(n.created_at);
                char buf[32] = {};
                std::tm* tm_info = std::gmtime(&t);
                if (tm_info)
                    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", tm_info);
                else
                    std::snprintf(buf, sizeof(buf), "%lld",
                                  static_cast<long long>(n.created_at));
                oss << "- **created_at**: " << buf << "\n";
            } else {
                oss << "- **created_at**: (unknown)\n";
            }
            oss << "\n" << n.content << "\n\n";
        }
        return oss.str();
    }
};

ICMG_REGISTER_COMMAND("memory-export", MemoryExportCommand);

} // namespace icmg::cli
