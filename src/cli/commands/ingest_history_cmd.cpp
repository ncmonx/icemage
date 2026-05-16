// `icmg ingest-history --pr-count N`
//
// Ingests the last N merged GitHub PRs (title, body, linked commit SHAs)
// into context_nodes with tier "memoir" so that `icmg recall "how did we
// solve X"` can surface them.
//
// Each PR becomes one context_nodes row:
//   node_key   = "pr-<number>"
//   title      = PR title
//   content    = PR body (may be empty)
//   tier       = "memoir"
//   source_file= "github:pr-<number>"
//   tags       = ["pr","merged"]
//
// Robustness: if `gh` is absent or returns non-zero the command prints a
// warning to stderr and exits 0 — it must never crash CI.

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../core/context_node.hpp"
#include "../../core/context_node_store.hpp"
#include "../../core/exec_utils.hpp"
#include <nlohmann/json.hpp>
#include <ctime>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace icmg::cli {

class IngestHistoryCommand : public BaseCommand {
public:
    std::string name()        const override { return "ingest-history"; }
    std::string description() const override {
        return "Ingest merged GitHub PRs into memoir tier (for recall)";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg ingest-history [options]\n\n"
            "Fetches the last N merged PRs via `gh` and stores them in the\n"
            "context_nodes table (tier=memoir) for use by `icmg recall`.\n\n"
            "Options:\n"
            "  --pr-count N        Number of merged PRs to fetch (default: 20)\n"
            "  --gh-cmd <bin>      Override gh binary/command (default: gh)\n"
            "  --dry-run           Parse + print without writing to DB\n"
            "  --help              Show this help\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (hasFlag(args, "--help")) { usage(); return 0; }

        int pr_count = 20;
        try { pr_count = std::stoi(flagValue(args, "--pr-count", "20")); } catch (...) {}
        if (pr_count < 0) pr_count = 0;

        bool dry_run = hasFlag(args, "--dry-run");

        // Allow injecting a fake gh command in tests.
        std::string gh_cmd = flagValue(args, "--gh-cmd", "gh");

        // Build the gh invocation.
        std::ostringstream cmd_ss;
        cmd_ss << gh_cmd
               << " pr list --state merged --limit " << pr_count
               << " --json number,title,body,mergedAt,mergeCommit";
        std::string cmd = cmd_ss.str();

        auto res = core::safeExecShell(cmd, false, 30000);

        if (res.exit_code != 0 || res.out.empty()) {
            std::cerr << "[icmg ingest-history] Warning: gh call failed"
                      << " (exit=" << res.exit_code << "). "
                      << "Is `" << gh_cmd << "` installed and authenticated?\n";
            if (!res.err.empty()) {
                std::cerr << "  stderr: " << res.err.substr(0, 200) << "\n";
            }
            return 0;  // tolerate absence — do NOT crash
        }

        // Parse JSON.
        nlohmann::json prs;
        try {
            prs = nlohmann::json::parse(res.out);
        } catch (const std::exception& e) {
            std::cerr << "[icmg ingest-history] Warning: JSON parse error: "
                      << e.what() << "\n";
            return 0;
        }

        if (!prs.is_array()) {
            std::cerr << "[icmg ingest-history] Warning: unexpected gh output (not array)\n";
            return 0;
        }

        // Open DB (unless dry-run).
        std::unique_ptr<core::Db> db_ptr;
        std::unique_ptr<core::ContextNodeStore> store_ptr;
        if (!dry_run) {
            auto& cfg = core::Config::instance();
            db_ptr    = std::make_unique<core::Db>(cfg.projectDbPath("."));
            store_ptr = std::make_unique<core::ContextNodeStore>(*db_ptr);
        }

        int ingested = 0;
        int64_t now  = static_cast<int64_t>(std::time(nullptr));

        for (const auto& pr : prs) {
            if (!pr.is_object()) continue;

            int     number    = pr.value("number", 0);
            std::string title = pr.value("title",  "");
            std::string body  = pr.value("body",   "");
            std::string merged_at = pr.value("mergedAt", "");

            // Append merge commit SHA to content if present.
            std::string extra;
            if (pr.contains("mergeCommit") && pr["mergeCommit"].is_object()) {
                std::string sha = pr["mergeCommit"].value("oid", "");
                if (!sha.empty()) {
                    extra = "\n\nMerge commit: " + sha;
                }
            }
            if (!merged_at.empty()) {
                extra += "\nMerged at: " + merged_at;
            }

            std::string content = body + extra;

            core::ContextNode node;
            node.node_key    = "pr-" + std::to_string(number);
            node.title       = title;
            node.content     = content;
            node.tier        = "memoir";
            node.source_file = "github:pr-" + std::to_string(number);
            node.tags        = "[\"pr\",\"merged\"]";
            node.active      = true;
            node.created_at  = now;
            node.updated_at  = now;

            if (dry_run) {
                std::cout << "[dry-run] pr-" << number << ": " << title << "\n";
            } else {
                store_ptr->upsert(node);
            }
            ++ingested;
        }

        if (dry_run) {
            std::cout << "[dry-run] Would ingest " << ingested << " PR(s) into memoir tier.\n";
        } else {
            std::cout << "Ingested " << ingested << " PRs into memoir tier.\n";
        }
        return 0;
    }
};

ICMG_REGISTER_COMMAND("ingest-history", IngestHistoryCommand);

} // namespace icmg::cli
