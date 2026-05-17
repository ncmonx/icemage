// v1.4.0 Task 1: `icmg target-verify` command.
//
// Usage:
//   icmg target-verify "<task>" [file_path] [--threshold F] [--json]
//
// Walks the project context graph, gathers candidate node names, and runs
// disambiguation scoring against the user task. Prints candidates that score
// above the threshold (default 0.80).
//
// Fail-soft: when no DB is available (or no candidates exist), prints nothing
// and exits 0.

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../core/context_node_store.hpp"
#include "../../core/target_disambiguator.hpp"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using nlohmann::json;

namespace icmg::cli {

class TargetVerifyCommand : public BaseCommand {
public:
    std::string name()        const override { return "target-verify"; }
    std::string description() const override {
        return "Detect ambiguous edit targets before AI edits the wrong file.";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg target-verify \"<task>\" [file_path] [--threshold F] [--json]\n\n"
            "Walks the project context graph and scores candidate node names against\n"
            "the task description using char-trigram Jaccard + suffix-overlap bonus.\n\n"
            "Options:\n"
            "  --threshold F   Score cutoff (default: 0.80, env: ICMG_DISAMBIG_THRESHOLD)\n"
            "  --json          Emit JSON array instead of plain text\n\n"
            "Exit codes:\n"
            "  0  No ambiguity / fail-soft (no DB, no candidates)\n"
            "  2  Ambiguity detected (>=2 candidates above threshold)\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty()) { usage(); return 1; }
        if (hasFlag(args, "--help")) { usage(); return 0; }

        // Parse positional: first non-flag arg is the task prompt.
        std::string task;
        std::string file_path_hint;
        bool emit_json = hasFlag(args, "--json");
        double threshold = 0.80;

        // Env override.
        if (const char* e = std::getenv("ICMG_DISAMBIG_THRESHOLD")) {
            try { threshold = std::stod(e); } catch (...) {}
        }

        // Command-line --threshold F.
        for (size_t i = 0; i < args.size(); ++i) {
            if (args[i] == "--threshold" && i + 1 < args.size()) {
                try { threshold = std::stod(args[i + 1]); } catch (...) {}
            }
        }

        // Collect positional args (non-flag, non-threshold-value).
        {
            bool skip_next = false;
            for (size_t i = 0; i < args.size(); ++i) {
                if (skip_next) { skip_next = false; continue; }
                if (args[i] == "--threshold") { skip_next = true; continue; }
                if (args[i] == "--json") continue;
                if (args[i].rfind("--", 0) == 0) continue;
                if (task.empty()) task = args[i];
                else if (file_path_hint.empty()) file_path_hint = args[i];
            }
        }

        if (task.empty()) { usage(); return 1; }

        // Gather candidates from context node store. Fail-soft on DB error.
        std::vector<std::pair<std::string, std::string>> raw_candidates;
        try {
            auto& cfg = core::Config::instance();
            core::Db db(cfg.projectDbPath("."));
            core::ContextNodeStore store(db);
            auto nodes = store.list("", true);
            for (auto& n : nodes) {
                // Use node_key as name, path from node_key or empty.
                raw_candidates.push_back({n.node_key, n.title});
            }
        } catch (...) {
            // Fail-soft: no DB, no candidates.
        }

        // Also add the file_path_hint itself as a candidate if provided.
        if (!file_path_hint.empty()) {
            std::string basename = fs::path(file_path_hint).filename().string();
            // Avoid duplicates.
            bool already = false;
            for (auto& c : raw_candidates)
                if (c.first == basename || c.second == file_path_hint) { already = true; break; }
            if (!already)
                raw_candidates.push_back({basename, file_path_hint});
        }

        if (raw_candidates.empty()) {
            if (emit_json) std::cout << "[]" << "\n";
            return 0;
        }

        auto results = core::disambiguateTargets(task, raw_candidates, threshold);

        if (results.empty()) {
            if (emit_json) std::cout << "[]" << "\n";
            return 0;
        }

        if (emit_json) {
            json arr = json::array();
            for (auto& r : results) {
                json obj;
                obj["name"]  = r.name;
                obj["path"]  = r.path;
                obj["score"] = r.score;
                arr.push_back(obj);
            }
            std::cout << arr.dump(2) << "\n";
        } else {
            if (results.size() >= 2) {
                std::cout << "[icmg target-verify] Ambiguous target in task: \""
                          << task << "\"\n";
                std::cout << "  " << results.size()
                          << " candidates above threshold " << threshold << ":\n";
            } else {
                std::cout << "[icmg target-verify] Top match for: \"" << task << "\"\n";
            }
            for (auto& r : results) {
                std::cout << "  " << r.name;
                if (!r.path.empty() && r.path != r.name)
                    std::cout << "  (" << r.path << ")";
                std::cout << "  score=" << r.score << "\n";
            }
        }

        return (results.size() >= 2) ? 2 : 0;
    }
};

ICMG_REGISTER_COMMAND("target-verify", TargetVerifyCommand);

} // namespace icmg::cli
