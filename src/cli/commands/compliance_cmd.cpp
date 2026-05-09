// Phase 67 T32: `icmg compliance` — directive-violation tracker.
//
// Caveman directive injected via SessionStart, but Anthropic thinking phase
// generation often ignores it (model-internal). icmg can't strip thinking
// retroactively, but can count violations and surface them upfront on next
// session — increasing model salience.
//
// Stop hook calls `compliance check-thinking` with assistant message text.
// Violation = thinking section > N words (default 80) when caveman flag ON.
// SessionStart hook prepends "X violations in last 24h — comply now" to
// caveman directive, escalating language if pattern persists.

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace icmg::cli {

class ComplianceCommand : public BaseCommand {
public:
    std::string name()        const override { return "compliance"; }
    std::string description() const override {
        return "Directive-violation tracker (caveman thinking-phase non-compliance)";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg compliance <action> [options]\n\n"
            "Actions:\n"
            "  check-thinking       Read stdin (assistant message JSON), count\n"
            "                       thinking-section words, log violation if over limit.\n"
            "  count                Show violation counts per window.\n"
            "  recent [--last N]    List recent violation entries.\n"
            "  reset                Clear violation log.\n"
            "  inject               Print SessionStart additionalContext line\n"
            "                       (called by caveman hook when violations exist).\n\n"
            "Options:\n"
            "  --max-words N        Limit (default 80)\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || hasFlag(args, "--help")) { usage(); return 0; }
        std::string action = args[0];
        fs::path log = logPath();

        if (action == "check-thinking") {
            int max_words = 80;
            try { max_words = std::stoi(flagValue(args, "--max-words", "80")); } catch (...) {}
            // Caveman flag must be ON to count violations.
            if (!fs::exists(flagPath())) return 0;
            // Read JSON from stdin.
            std::ostringstream buf;
            buf << std::cin.rdbuf();
            std::string raw = buf.str();
            int word_count = countThinkingWords(raw);
            if (word_count > max_words) {
                logViolation(log, word_count, max_words);
                std::cerr << "[icmg compliance] thinking " << word_count
                          << " words > " << max_words << " — violation logged\n";
            }
            return 0;
        }

        if (action == "count") {
            int total = 0, last_24h = 0;
            int64_t cutoff = (int64_t)std::time(nullptr) - 86400;
            countViolations(log, total, last_24h, cutoff);
            std::cout << "Total violations: " << total << "\n"
                      << "Last 24h:         " << last_24h << "\n";
            return 0;
        }

        if (action == "recent") {
            int n = 10;
            try { n = std::stoi(flagValue(args, "--last", "10")); } catch (...) {}
            if (!fs::exists(log)) { std::cout << "No violations.\n"; return 0; }
            std::ifstream f(log);
            std::string line;
            std::vector<std::string> lines;
            while (std::getline(f, line)) lines.push_back(line);
            int start = (int)lines.size() - n;
            if (start < 0) start = 0;
            for (int i = start; i < (int)lines.size(); ++i) std::cout << lines[i] << "\n";
            return 0;
        }

        if (action == "reset") {
            std::error_code ec; fs::remove(log, ec);
            std::cout << "icmg compliance: log cleared.\n";
            return 0;
        }

        if (action == "inject") {
            int total = 0, last_24h = 0;
            int64_t cutoff = (int64_t)std::time(nullptr) - 86400;
            countViolations(log, total, last_24h, cutoff);
            if (last_24h == 0) return 0;  // no pressure needed
            std::string severity;
            if (last_24h >= 5) severity = "STRONG WARNING";
            else if (last_24h >= 2) severity = "REMINDER";
            else severity = "NOTE";
            std::cout << severity << ": last 24h had " << last_24h
                      << " thinking-phase caveman violation(s). "
                      << "Each instance burned ~500-2000 tokens. "
                      << "Apply caveman ultra to thinking strictly THIS TURN. "
                      << "If approach is obvious skip thinking entirely.";
            if (last_24h >= 5) {
                std::cout << " User has noticed; further violations break trust.";
            }
            return 0;
        }

        std::cerr << "icmg compliance: unknown action '" << action << "'\n";
        usage();
        return 1;
    }

private:
    static fs::path homeDir() {
        const char* h = std::getenv("USERPROFILE");
        if (!h) h = std::getenv("HOME");
        return fs::path(h ? h : ".");
    }
    static fs::path logPath()  { return homeDir() / ".icmg" / "compliance-violations.jsonl"; }
    static fs::path flagPath() { return homeDir() / ".icmg" / "caveman.flag"; }

    static int countThinkingWords(const std::string& raw) {
        // Manual scan: find `"thinking":"...` (or similar key) → count
        // whitespace-separated tokens until closing unescaped quote.
        // Avoids regex backtracking blowup on long bodies.
        int total = 0;
        const std::string needle = "\"thinking\":\"";
        size_t p = 0;
        while ((p = raw.find(needle, p)) != std::string::npos) {
            p += needle.size();
            // Walk chars until unescaped `"`.
            std::string body;
            while (p < raw.size()) {
                char c = raw[p];
                if (c == '\\' && p + 1 < raw.size()) {
                    char n = raw[++p];
                    if      (n == 'n') body += ' ';
                    else if (n == '"') body += '"';
                    else                body += n;
                    ++p;
                    continue;
                }
                if (c == '"') { ++p; break; }
                body += c;
                ++p;
            }
            // Word count.
            std::istringstream ws(body);
            std::string tok; while (ws >> tok) ++total;
        }
        return total;
    }

    static void logViolation(const fs::path& log, int words, int limit) {
        std::error_code ec; fs::create_directories(log.parent_path(), ec);
        std::ofstream f(log, std::ios::app);
        f << "{\"ts\":" << (int64_t)std::time(nullptr)
          << ",\"kind\":\"thinking-overrun\""
          << ",\"words\":" << words
          << ",\"limit\":" << limit << "}\n";
    }

    static void countViolations(const fs::path& log, int& total, int& last_24h,
                                 int64_t cutoff) {
        if (!fs::exists(log)) return;
        std::ifstream f(log);
        std::string line;
        while (std::getline(f, line)) {
            if (line.empty()) continue;
            ++total;
            auto pos = line.find("\"ts\":");
            if (pos == std::string::npos) continue;
            try {
                int64_t ts = std::stoll(line.substr(pos + 5));
                if (ts > cutoff) ++last_24h;
            } catch (...) {}
        }
    }
};

ICMG_REGISTER_COMMAND("compliance", ComplianceCommand);

} // namespace icmg::cli
