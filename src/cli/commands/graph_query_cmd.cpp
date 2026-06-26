// Phase 3 (graphify-parity): `icmg graph-query` — natural-language queryable
// knowledge graph.
//
// Subcommands:
//   query "<NL question>"  — seed -> subgraph -> (optional) LLM answer
//   explain "<node>"       — describe a node + its 1-hop neighbors
//
// path/report are intentionally NOT re-implemented here: `graph-path` and
// `graph-report` already exist (anti-dup reflex). This command only adds the
// genuinely new NL-answer + explain capability over QueryEngine.
//
// LLM is OPTIONAL: without a local model (or with --no-llm) the command prints
// the raw subgraph so it always works offline.
#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../graph_query_engine.hpp"
#include "../../llm/llama_runner.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace icmg::cli {

class GraphQueryCommand : public BaseCommand {
public:
    std::string name()        const override { return "graph-query"; }
    std::string description() const override {
        return "Natural-language queryable knowledge graph (query/explain over a subgraph)";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg graph-query <subcommand> [options]\n\n"
            "Subcommands:\n"
            "  query \"<NL question>\"   Answer from a subgraph (seed -> BFS -> LLM)\n"
            "  explain \"<node-path>\"   Describe a node + its neighbors\n\n"
            "Options:\n"
            "  --depth N        BFS depth around seeds (default 2)\n"
            "  --max-nodes N    Hard cap on subgraph size (default 50)\n"
            "  --no-llm         Print raw subgraph, skip the LLM answer\n"
            "  --json           Machine-readable (subgraph only)\n\n"
            "Note: shortest path = `icmg graph-path`; report = `icmg graph-report`.\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || hasFlag(args, "--help")) { usage(); return 0; }

        const std::string sub = args[0];
        int depth = 2, maxNodes = 50;
        try { depth = std::stoi(flagValue(args, "--depth", "2")); } catch (...) {}
        try { maxNodes = std::stoi(flagValue(args, "--max-nodes", "50")); } catch (...) {}
        bool noLlm   = hasFlag(args, "--no-llm");
        bool jsonOut = hasFlag(args, "--json");

        // Positional question/node = everything after the subcommand that is not
        // a flag NOR the value consumed by a value-taking flag (--depth/--max-nodes).
        std::string target;
        for (size_t i = 1; i < args.size(); ++i) {
            const std::string& a = args[i];
            if (a == "--depth" || a == "--max-nodes") { ++i; continue; }  // skip flag value
            if (a.empty() || a[0] == '-') continue;
            if (!target.empty()) target += " ";
            target += a;
        }

        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        graph::GraphStore store(db);
        QueryEngine eng(store);

        if (sub == "explain") {
            if (target.empty()) { std::cerr << "icmg graph-query explain: requires <node-path>\n"; return 1; }
            std::cout << eng.explainNode(target, depth);
            return 0;
        }

        if (sub == "query") {
            if (target.empty()) { std::cerr << "icmg graph-query query: requires <question>\n"; return 1; }
            auto sg = eng.buildSubGraph(target, depth, maxNodes);
            if (sg.nodes.empty()) {
                std::cout << "No matching nodes for: " << target << "\n";
                return 0;
            }
            std::string ctx = eng.formatSubGraph(sg);

            if (jsonOut) {
                std::cout << "{\"query\":\"" << escJson(target) << "\",\"nodes\":[";
                for (size_t i = 0; i < sg.nodes.size(); ++i) {
                    if (i) std::cout << ",";
                    std::cout << "{\"path\":\"" << escJson(sg.nodes[i].path)
                              << "\",\"kind\":\"" << escJson(sg.nodes[i].kind) << "\"}";
                }
                std::cout << "],\"truncated\":" << (sg.truncated ? "true" : "false") << "}\n";
                return 0;
            }

            // --no-llm OR no local model -> print the subgraph as the answer.
            if (noLlm || !tryLlmAnswer(target, ctx)) {
                std::cout << ctx;
            }
            return 0;
        }

        std::cerr << "icmg graph-query: unknown subcommand '" << sub << "'\n";
        usage();
        return 1;
    }

private:
    static std::string escJson(const std::string& s) {
        std::string o;
        for (char c : s) {
            if (c == '"' || c == '\\') { o += '\\'; o += c; }
            else if (c == '\n') o += "\\n";
            else o += c;
        }
        return o;
    }

    // Returns true if an LLM answer was produced + printed. Never throws; any
    // failure (no build support, no model, infer error) returns false so the
    // caller falls back to printing the raw subgraph.
    static bool tryLlmAnswer(const std::string& question, const std::string& subgraphCtx) {
        if (!llm::LlamaRunner::available()) return false;
        namespace fs = std::filesystem;
        const char* home =
#ifdef _WIN32
            std::getenv("USERPROFILE");
#else
            std::getenv("HOME");
#endif
        fs::path lldir = (home && *home ? fs::path(home) : fs::current_path()) / ".icmg" / "llm";
        std::error_code ec;
        if (fs::exists(lldir / "disabled", ec)) return false;
        std::string active;
        { std::ifstream af(lldir / "active"); std::getline(af, active); }
        if (active.empty()) return false;
        fs::path gguf = lldir / active / "model.gguf";
        if (!fs::exists(gguf, ec)) return false;

        llm::LlamaRunner r;
        if (!r.load(gguf.string())) return false;
        llm::InferParams ip;
        ip.max_tokens = 384;
        ip.temperature = 0.3f;
        std::string prompt =
            "You answer questions about a codebase using ONLY the subgraph below. "
            "Cite node paths. If the subgraph is insufficient, say so.\n\n"
            "Subgraph:\n" + subgraphCtx +
            "\nQuestion: " + question + "\n\nAnswer:";
        auto res = r.infer(prompt, ip);
        if (!res.ok) return false;
        std::cout << res.text << "\n\n--- subgraph ---\n" << subgraphCtx;
        return true;
    }
};

ICMG_REGISTER_COMMAND("graph-query", GraphQueryCommand);

}  // namespace icmg::cli
