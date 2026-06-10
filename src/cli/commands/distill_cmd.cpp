// Phase 67 T3+T5: `icmg distill` — auto-extract decisions/facts from text
// into memory_nodes. Invoked by Stop hook (per-response) and PreCompact
// hook (full session). Heuristic extraction; no model dep.
//
// Topic prefix `auto:` (per-response) or `session:` (per-compact) so users
// can prune separately via `icmg memory prune --topic 'auto:%'`.

#include "../base_command.hpp"
#include "../../core/stdin_util.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../imem/memory_store.hpp"
#include "../dense_summary.hpp"   // dense structured-summary template for compaction
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>

namespace icmg::cli {

class DistillCommand : public BaseCommand {
public:
    std::string name()        const override { return "distill"; }
    std::string description() const override {
        return "Auto-extract decisions/facts/anti-patterns from response text into memory";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg distill <action> [options]\n\n"
            "Actions:\n"
            "  auto      Read stdin (assistant message), extract heuristic\n"
            "            statements (Decision/Fix/Note/IMPORTANT), store as\n"
            "            memory_node with topic prefix 'auto:'.\n"
            "  session   Read stdin (full transcript), summarize into one\n"
            "            consolidated 'session:' memory_node.\n"
            "  show      List recent auto-distilled entries.\n"
            "  template  Print the dense structured-summary instruction for the\n"
            "            model to fill at compaction (Goal/Done/State/Next/Keep).\n\n"
            "Options:\n"
            "  --min-len N        Skip when input < N chars (default 200)\n"
            "  --tag T            Custom topic suffix\n"
            "  --turns N          (template) stamp turns summarized\n"
            "  --compactions N    (template) stamp compaction count\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || hasFlag(args, "--help")) { usage(); return 0; }
        std::string action = args[0];

        // `template` -- emit the dense structured-summary instruction for the
        // model to fill at compaction (consistent, information-dense handoff
        // that survives compaction far better than free-form prose). The
        // PreCompact hook can inject this so the model produces a uniform
        // summary. No DB / no stdin needed.
        if (action == "template") {
            int turns = 0, compactions = 0;
            try { turns = std::stoi(flagValue(args, "--turns", "0")); } catch (...) {}
            try { compactions = std::stoi(flagValue(args, "--compactions", "0")); } catch (...) {}
            std::cout << denseSummaryPrompt(turns, compactions);
            return 0;
        }

        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        imem::MemoryStore mem(db);

        if (action == "show") {
            int limit = 20;
            try { limit = std::stoi(flagValue(args, "--limit", "20")); } catch (...) {}
            int shown = 0;
            db.query(
                "SELECT topic, content FROM memory_nodes"
                " WHERE topic LIKE 'auto:%' OR topic LIKE 'session:%'"
                " ORDER BY last_used DESC LIMIT ?",
                {std::to_string(limit)},
                [&](const core::Row& r) {
                    if (r.size() < 2) return;
                    if (shown == 0) std::cout << "Recent distilled entries:\n";
                    std::cout << "  [" << r[0] << "]\n    " << r[1].substr(0, 120) << "\n";
                    ++shown;
                });
            if (shown == 0) std::cout << "No distilled entries.\n";
            return 0;
        }

        // Read stdin
        std::ostringstream buf;
        buf.str(core::slurpStdinSafe());
        std::string text = buf.str();

        size_t min_len = 200;
        try { min_len = (size_t)std::stoul(flagValue(args, "--min-len", "200")); } catch (...) {}
        if (text.size() < min_len) {
            std::cerr << "[icmg distill] input too short (" << text.size()
                      << " < " << min_len << "), skipping\n";
            return 0;
        }

        std::string tag = flagValue(args, "--tag");

        if (action == "auto") {
            int stored = distillPerResponse(mem, text, tag);
            std::cerr << "[icmg distill] " << stored << " entry(ies) extracted\n";
            return 0;
        }
        if (action == "session") {
            int stored = distillSession(mem, text, tag);
            std::cerr << "[icmg distill] session summary stored ("
                      << stored << " section(s))\n";
            return 0;
        }

        std::cerr << "icmg distill: unknown action '" << action << "'\n";
        usage();
        return 1;
    }

private:
    // Heuristic per-response: scrape `Decision:`, `Fix:`, `Root cause:`,
    // `Note:`, `IMPORTANT:`, `Conclusion:` lines. Each stored as memory_node.
    int distillPerResponse(imem::MemoryStore& mem, const std::string& text,
                            const std::string& tag) {
        std::regex stmt_re(
            R"((?:^|\n)\s*(?:[-*]\s*)?(?:\*\*)?(Decision|Fix|Root cause|Note|IMPORTANT|Conclusion|Workaround|TODO):\s*([^\n]{20,400}))",
            std::regex::ECMAScript);
        int n = 0;
        for (auto it = std::sregex_iterator(text.begin(), text.end(), stmt_re);
             it != std::sregex_iterator() && n < 8; ++it) {
            std::string label  = (*it)[1].str();
            std::string body   = (*it)[2].str();
            if (body.empty()) continue;
            // Trim trailing whitespace + bold markers
            while (!body.empty() && (body.back() == ' ' || body.back() == '*' ||
                                     body.back() == '\r')) body.pop_back();
            imem::MemoryNode mn;
            mn.topic    = "auto: " + label + (tag.empty() ? "" : " " + tag);
            mn.content  = body;
            mn.keywords = label + " auto distilled";
            mn.importance = (label == "IMPORTANT" || label == "Decision") ? 2 : 1;
            mn.zone = "default";
            try { mem.store(mn, /*force=*/false); ++n; }
            catch (...) {}
        }
        return n;
    }

    // Heuristic session summary: take first user prompt + last assistant
    // chunk + concat all `Decision:` lines. One consolidated row.
    int distillSession(imem::MemoryStore& mem, const std::string& text,
                        const std::string& tag) {
        std::ostringstream summary;
        // First task line
        std::regex task_re(R"X("text"\s*:\s*"([^"]{20,200})")X");
        std::smatch tm_match;
        if (std::regex_search(text, tm_match, task_re)) {
            summary << "Task: " << tm_match[1].str() << "\n";
        }
        // All decisions
        std::regex dec_re(R"((?:Decision|Conclusion|Fix|Root cause):\s*([^\n"]{20,200}))");
        int dec_n = 0;
        for (auto it = std::sregex_iterator(text.begin(), text.end(), dec_re);
             it != std::sregex_iterator() && dec_n < 10; ++it, ++dec_n) {
            summary << "  - " << (*it)[1].str() << "\n";
        }
        if (summary.str().empty()) return 0;

        time_t now = std::time(nullptr);
        char date_buf[16];
        std::strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", std::localtime(&now));

        imem::MemoryNode mn;
        mn.topic    = std::string("session: ") + date_buf + (tag.empty() ? "" : " " + tag);
        mn.content  = summary.str();
        mn.keywords = "session distilled summary";
        mn.importance = 2;
        mn.zone = "default";
        try { mem.store(mn, /*force=*/false); return 1; }
        catch (...) { return 0; }
    }
};

ICMG_REGISTER_COMMAND("distill", DistillCommand);

} // namespace icmg::cli
