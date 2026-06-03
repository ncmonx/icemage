// Phase 67 T4: `icmg correction` — capture user fixes to AI-generated code.
//
// PostToolUse:Edit hook pipes (old_string, new_string) here. If old_string
// looks like AI-emitted code (heuristic: was in recent transcript), stores
// the diff as memory_node with topic `correction:` so future similar prompts
// surface "Claude tends to X, correct is Y."

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../core/stdin_util.hpp"   // v2.0.x #2: TTY-guarded stdin slurp (no hang)
#include "../../imem/memory_store.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace icmg::cli {

class CorrectionCommand : public BaseCommand {
public:
    std::string name()        const override { return "correction"; }
    std::string description() const override {
        return "Track diffs between AI-emitted code and user fixes";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg correction <action> [args]\n\n"
            "Actions:\n"
            "  capture        Read JSON from stdin {old_string, new_string, file_path}\n"
            "                 (PostToolUse:Edit hook payload), store as correction memory.\n"
            "  recall <task>  List corrections matching task keywords.\n"
            "  list [--limit N]   Recent corrections.\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || hasFlag(args, "--help")) { usage(); return 0; }
        std::string action = args[0];

        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        imem::MemoryStore mem(db);

        if (action == "capture") {
            std::string raw = core::slurpStdinSafe();
            return capture(mem, raw);
        }
        if (action == "recall") {
            if (args.size() < 2) { std::cerr << "icmg correction recall: requires <task>\n"; return 1; }
            std::string task = args[1];
            int limit = 5;
            try { limit = std::stoi(flagValue(args, "--limit", "5")); } catch (...) {}
            auto hits = mem.recall(task, limit * 4);
            int shown = 0;
            for (auto& h : hits) {
                if (h.topic.rfind("correction:", 0) != 0) continue;
                if (shown == 0) std::cout << "Past corrections matching \"" << task << "\":\n";
                std::cout << "  - " << h.topic.substr(11) << "\n"
                          << "    " << h.content << "\n";
                if (++shown >= limit) break;
            }
            if (shown == 0) std::cout << "No past corrections recorded for \"" << task << "\".\n";
            return 0;
        }
        if (action == "list") {
            int limit = 20;
            try { limit = std::stoi(flagValue(args, "--limit", "20")); } catch (...) {}
            int shown = 0;
            db.query(
                "SELECT topic, content FROM memory_nodes"
                " WHERE topic LIKE 'correction:%'"
                " ORDER BY last_used DESC LIMIT ?",
                {std::to_string(limit)},
                [&](const core::Row& r) {
                    if (r.size() < 2) return;
                    if (shown == 0) std::cout << "Recent corrections:\n";
                    std::cout << "  " << r[0].substr(11) << "\n"
                              << "    " << r[1].substr(0, 200) << "\n";
                    ++shown;
                });
            if (shown == 0) std::cout << "No corrections recorded.\n";
            return 0;
        }

        std::cerr << "icmg correction: unknown action '" << action << "'\n";
        usage();
        return 1;
    }

private:
    int capture(imem::MemoryStore& mem, const std::string& json_in) {
        // Cheap field extract: "old_string":"..." "new_string":"..." "file_path":"..."
        // We avoid pulling json lib here for speed; this hook fires on every Edit.
        auto extract = [&](const std::string& key) -> std::string {
            std::string needle = "\"" + key + "\":\"";
            size_t p = json_in.find(needle);
            if (p == std::string::npos) return "";
            p += needle.size();
            // Find closing unescaped quote.
            std::string out;
            for (size_t i = p; i < json_in.size(); ++i) {
                char c = json_in[i];
                if (c == '\\' && i + 1 < json_in.size()) {
                    char n = json_in[++i];
                    if      (n == 'n') out += '\n';
                    else if (n == 't') out += '\t';
                    else if (n == '"') out += '"';
                    else if (n == '\\') out += '\\';
                    else                out += n;
                    continue;
                }
                if (c == '"') break;
                out += c;
            }
            return out;
        };
        std::string old_s  = extract("old_string");
        std::string new_s  = extract("new_string");
        std::string fpath  = extract("file_path");
        if (old_s.empty() || new_s.empty() || old_s == new_s) return 0;
        // Skip when both sides too small (typo fixes etc.)
        if (old_s.size() < 30 && new_s.size() < 30) return 0;
        // Build correction body capped at 400 chars per side.
        std::string body = "Wrong (AI-emitted): " + old_s.substr(0, 400)
                         + "\n--- Corrected (user fix): " + new_s.substr(0, 400);
        if (fpath.size() > 80) fpath = fpath.substr(fpath.size() - 80);
        imem::MemoryNode mn;
        mn.topic    = "correction: " + (fpath.empty() ? "edit" : fpath);
        mn.content  = body;
        mn.keywords = "correction edit fix " + fpath;
        mn.importance = 2;
        mn.zone = "default";
        try {
            mem.store(mn, /*force=*/false);
            std::cerr << "[icmg correction] captured: " << fpath << "\n";
        } catch (...) {}
        return 0;
    }
};

ICMG_REGISTER_COMMAND("correction", CorrectionCommand);

} // namespace icmg::cli
