// icmg context-node — query context_nodes graph.
//
// Subcommands:
//   match <query> [--tier hot|cold|skill] [--top N] [--fmt additionalContext|plain] [--min-score F]
//       BM25 search context_nodes, print matching content.
//       --fmt additionalContext: emit JSON for Claude Code hook injection.
//   get <node_key>
//       Print full content of one node by key.

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../core/context_node_store.hpp"
#include <iostream>
#include <sstream>
#include <nlohmann/json.hpp>

using namespace icmg::core;
using nlohmann::json;

namespace icmg::cli {

static std::string escapeJson(const std::string& s) {
    json j = s;
    std::string r = j.dump();
    // dump includes surrounding quotes — strip them
    return r.substr(1, r.size() - 2);
}

static int doMatch(ContextNodeStore& store, const std::vector<std::string>& args) {
    std::string query, tier, fmt = "plain";
    int top = 3;
    double min_score = 0.05;

    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--tier"      && i+1 < args.size()) tier      = args[++i];
        else if (args[i] == "--top"  && i+1 < args.size()) try { top = std::stoi(args[++i]); } catch (...) {}
        else if (args[i] == "--fmt"  && i+1 < args.size()) fmt       = args[++i];
        else if (args[i] == "--min-score" && i+1 < args.size()) try { min_score = std::stod(args[++i]); } catch (...) {}
        else if (args[i].empty() || args[i][0] != '-') query = args[i];
    }

    auto nodes = store.search(query, tier, top, min_score);

    if (nodes.empty()) return 0;  // silence — hook should produce no output if no match

    if (fmt == "additionalContext") {
        // Emit Claude Code hook additionalContext JSON
        std::string ctx;
        for (auto& n : nodes) {
            ctx += "## " + n.title + " [" + n.tier + "]\n";
            ctx += n.content + "\n\n";
        }
        // Trim trailing newlines
        while (!ctx.empty() && (ctx.back() == '\n' || ctx.back() == '\r')) ctx.pop_back();

        json out;
        out["hookSpecificOutput"]["hookEventName"] = "UserPromptSubmit";
        out["hookSpecificOutput"]["additionalContext"] = ctx;
        std::cout << out.dump() << "\n";
    } else {
        // Plain: print title + content
        for (auto& n : nodes) {
            std::cout << "=== " << n.title << " [" << n.tier << "] (" << n.node_key << ") ===\n";
            std::cout << n.content << "\n\n";
        }
    }
    return 0;
}

static int doGet(ContextNodeStore& store, const std::vector<std::string>& args) {
    if (args.empty()) {
        std::cerr << "context-node get: missing node_key\n";
        return 1;
    }
    auto node = store.get(args[0]);
    if (!node) {
        std::cerr << "context-node get: not found: " << args[0] << "\n";
        return 1;
    }
    std::cout << "=== " << node->title << " [" << node->tier << "] ===\n";
    std::cout << "key:    " << node->node_key    << "\n";
    std::cout << "source: " << node->source_file << "\n";
    std::cout << "tags:   " << node->tags        << "\n";
    std::cout << "active: " << (node->active ? "yes" : "no") << "\n\n";
    std::cout << node->content << "\n";
    return 0;
}

class ContextNodeCommand : public BaseCommand {
public:
    std::string name()        const override { return "context-node"; }
    std::string description() const override { return "Query context_nodes graph (BM25 match + get)"; }

    void usage() const override {
        std::cout <<
            "Usage: icmg context-node <subcommand> [options]\n\n"
            "Subcommands:\n"
            "  match <query> [--tier hot|cold|skill] [--top N] [--min-score F]\n"
            "               [--fmt plain|additionalContext]\n"
            "      BM25 search over title+tags+content. --fmt additionalContext\n"
            "      emits JSON suitable for Claude Code hook injection.\n"
            "  get <node_key>\n"
            "      Print full content of one node.\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || args[0] == "--help" || args[0] == "-h") {
            usage(); return 0;
        }
        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        ContextNodeStore store(db);

        const std::string& sub = args[0];
        std::vector<std::string> rest(args.begin() + 1, args.end());

        if (sub == "match") return doMatch(store, rest);
        if (sub == "get")   return doGet(store, rest);

        // Treat bare query (no subcommand) as match
        return doMatch(store, args);
    }
};

ICMG_REGISTER_COMMAND("context-node", ContextNodeCommand);

} // namespace icmg::cli
