// Phase 65 T1: `icmg fail store/recall` — anti-pattern memory.
//
// Closes the silent-retry loop: when Claude tries approach X on task T and X
// fails for reason R, store it. Future similar tasks auto-recall — Claude
// sees "tried X, failed because R" upfront, picks different approach.
//
// Storage: piggybacks on memory_nodes with topic prefix `fail:` so the
// existing BM25 + embedding recall pipeline applies for free. No schema
// change. importance=2 so fails win over routine memories on tied score.

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../imem/memory_store.hpp"
#include <iostream>
#include <sstream>
#include <string>

namespace icmg::cli {

class FailCommand : public BaseCommand {
public:
    std::string name()        const override { return "fail"; }
    std::string description() const override {
        return "Anti-pattern memory: store/recall failed approaches for tasks";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg fail <action> [args]\n\n"
            "Actions:\n"
            "  store <task> <approach> <reason>   Record a failed attempt\n"
            "  recall <task> [--limit N]          List past fails matching task\n"
            "  list [--limit N]                   Recent failed attempts\n\n"
            "Stored as memory_nodes with topic `fail: <task>`. Auto-recalled\n"
            "by `icmg pack` (alongside regular memory) so Claude sees prior\n"
            "failures upfront and avoids repeating them.\n\n"
            "Example:\n"
            "  icmg fail store \"jwt refresh\" \"clear cookie on 401\" \"breaks SSO loop\"\n"
            "  icmg pack \"jwt refresh\"   # auto-injects past fail\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || hasFlag(args, "--help")) { usage(); return 0; }
        std::string action = args[0];

        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        imem::MemoryStore mem(db);

        if (action == "store") {
            if (args.size() < 4) {
                std::cerr << "icmg fail store: requires <task> <approach> <reason>\n";
                return 1;
            }
            std::string task = args[1], approach = args[2], reason = args[3];
            imem::MemoryNode mn;
            mn.topic    = "fail: " + task;
            mn.content  = "Tried: " + approach + ". Failed because: " + reason
                        + ". Don't repeat this approach.";
            mn.keywords = "fail antipattern " + task + " " + approach;
            mn.importance = 2;  // high — fails should outweigh routine notes
            mn.zone = "default";
            try { mem.store(mn, /*force=*/false); }
            catch (const std::exception& e) {
                std::cerr << "icmg fail store: " << e.what() << "\n";
                return 1;
            }
            std::cout << "icmg fail: stored anti-pattern for \"" << task << "\"\n"
                      << "  approach: " << approach << "\n"
                      << "  reason:   " << reason << "\n";
            return 0;
        }

        if (action == "recall") {
            if (args.size() < 2) { std::cerr << "icmg fail recall: requires <task>\n"; return 1; }
            std::string task = args[1];
            int limit = 5;
            try { limit = std::stoi(flagValue(args, "--limit", "5")); } catch (...) {}
            // BM25 search restricted to fail: topic prefix.
            auto hits = mem.recall(task, limit * 4);
            int shown = 0;
            for (auto& h : hits) {
                if (h.topic.rfind("fail:", 0) != 0) continue;
                if (shown == 0) std::cout << "Past failed attempts for \"" << task << "\":\n";
                std::cout << "  - " << h.topic.substr(6) << "\n"
                          << "    " << h.content << "\n";
                if (++shown >= limit) break;
            }
            if (shown == 0) std::cout << "No past failures recorded for \"" << task << "\".\n";
            return 0;
        }

        if (action == "list") {
            int limit = 20;
            try { limit = std::stoi(flagValue(args, "--limit", "20")); } catch (...) {}
            int shown = 0;
            db.query(
                "SELECT topic, content, last_used FROM memory_nodes"
                " WHERE topic LIKE 'fail:%'"
                " ORDER BY last_used DESC LIMIT ?",
                {std::to_string(limit)},
                [&](const core::Row& r) {
                    if (r.size() < 2) return;
                    if (shown == 0) std::cout << "Recent failed attempts:\n";
                    std::cout << "  " << r[0].substr(6) << "\n"
                              << "    " << r[1] << "\n";
                    ++shown;
                });
            if (shown == 0) std::cout << "No failed attempts recorded.\n";
            return 0;
        }

        std::cerr << "icmg fail: unknown action '" << action << "'\n";
        usage();
        return 1;
    }
};

ICMG_REGISTER_COMMAND("fail", FailCommand);

} // namespace icmg::cli
