#include "internals.hpp"

#include "../config.hpp"
#include "../db.hpp"
#include "../path_utils.hpp"
#include "../../compress/compressor.hpp"
#include "../../imem/memory_store.hpp"

#include <nlohmann/json.hpp>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using nlohmann::json;

namespace icmg::core::hooks {

// ---- helpers ---------------------------------------------------------------

static fs::path homeDir() {
    const char* h = std::getenv("USERPROFILE");
    if (!h) h = std::getenv("HOME");
    return fs::path(h ? h : ".");
}

// ---- distill (per-response + session) --------------------------------------
//
// Heuristic extraction mirroring src/cli/commands/distill_cmd.cpp.

int distillAuto(const std::string& text, size_t min_len, const std::string& tag) {
    if (text.size() < min_len) return 0;
    try {
        auto& cfg = Config::instance();
        Db db(cfg.projectDbPath("."));
        imem::MemoryStore mem(db);

        std::regex stmt_re(
            R"((?:^|\n)\s*(?:[-*]\s*)?(?:\*\*)?(Decision|Fix|Root cause|Note|IMPORTANT|Conclusion|Workaround|TODO):\s*([^\n]{20,400}))",
            std::regex::ECMAScript);
        int n = 0;
        for (auto it = std::sregex_iterator(text.begin(), text.end(), stmt_re);
             it != std::sregex_iterator() && n < 8; ++it) {
            std::string label = (*it)[1].str();
            std::string body  = (*it)[2].str();
            if (body.empty()) continue;
            while (!body.empty() && (body.back() == ' ' || body.back() == '*' ||
                                     body.back() == '\r')) body.pop_back();
            imem::MemoryNode mn;
            mn.topic      = "auto: " + label + (tag.empty() ? "" : " " + tag);
            mn.content    = body;
            mn.keywords   = label + " auto distilled";
            mn.importance = (label == "IMPORTANT" || label == "Decision") ? 2 : 1;
            mn.zone       = "default";
            try { mem.store(mn, /*force=*/false); ++n; } catch (...) {}
        }
        return n;
    } catch (...) {
        return 0;
    }
}

int distillSession(const std::string& text, const std::string& tag) {
    try {
        auto& cfg = Config::instance();
        Db db(cfg.projectDbPath("."));
        imem::MemoryStore mem(db);

        std::ostringstream summary;
        std::regex task_re(R"X("text"\s*:\s*"([^"]{20,200})")X");
        std::smatch tm_match;
        if (std::regex_search(text, tm_match, task_re)) {
            summary << "Task: " << tm_match[1].str() << "\n";
        }
        std::regex dec_re(R"((?:Decision|Conclusion|Fix|Root cause):\s*([^\n"]{20,200}))");
        int dec_n = 0;
        for (auto it = std::sregex_iterator(text.begin(), text.end(), dec_re);
             it != std::sregex_iterator() && dec_n < 10; ++it, ++dec_n) {
            summary << "  - " << (*it)[1].str() << "\n";
        }
        if (summary.str().empty()) return 0;

        std::time_t now = std::time(nullptr);
        char date_buf[16];
        std::strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", std::localtime(&now));

        imem::MemoryNode mn;
        mn.topic      = std::string("session: ") + date_buf + (tag.empty() ? "" : " " + tag);
        mn.content    = summary.str();
        mn.keywords   = "session distilled summary";
        mn.importance = 2;
        mn.zone       = "default";
        try { mem.store(mn, /*force=*/false); return 1; } catch (...) { return 0; }
    } catch (...) {
        return 0;
    }
}

// ---- compliance check-thinking ---------------------------------------------

static int countThinkingWords(const std::string& raw) {
    int total = 0;
    const std::string needle = "\"thinking\":\"";
    size_t p = 0;
    while ((p = raw.find(needle, p)) != std::string::npos) {
        p += needle.size();
        std::string body;
        while (p < raw.size()) {
            char c = raw[p];
            if (c == '\\' && p + 1 < raw.size()) {
                char nch = raw[++p];
                if      (nch == 'n') body += ' ';
                else if (nch == '"') body += '"';
                else                  body += nch;
                ++p;
                continue;
            }
            if (c == '"') { ++p; break; }
            body += c;
            ++p;
        }
        std::istringstream ws(body);
        std::string tok;
        while (ws >> tok) ++total;
    }
    return total;
}

int complianceCheckThinking(const std::string& text, int max_words) {
    try {
        fs::path flag_path = homeDir() / ".icmg" / "caveman.flag";
        if (!fs::exists(flag_path)) return 0;
        int words = countThinkingWords(text);
        if (words > max_words) {
            fs::path log = homeDir() / ".icmg" / "compliance-violations.jsonl";
            std::error_code ec;
            fs::create_directories(log.parent_path(), ec);
            std::ofstream f(log, std::ios::app);
            f << "{\"ts\":" << (int64_t)std::time(nullptr)
              << ",\"kind\":\"thinking-overrun\""
              << ",\"words\":" << words
              << ",\"limit\":" << max_words << "}\n";
        }
        return words;
    } catch (...) {
        return 0;
    }
}

// ---- fail sync-denials -----------------------------------------------------

int failSyncDenials() {
    try {
        auto& cfg = Config::instance();
        Db db(cfg.projectDbPath("."));
        imem::MemoryStore mem(db);

        fs::path global_dir = fs::path(cfg.globalDbPath()).parent_path();
        fs::path denials    = global_dir / "strict-denials.jsonl";
        fs::path offset_p   = global_dir / "sync-denials-offset.txt";

        if (!fs::exists(denials)) return 0;

        int64_t last_offset = 0;
        if (fs::exists(offset_p)) {
            std::ifstream of(offset_p);
            of >> last_offset;
        }

        std::ifstream f(denials);
        f.seekg((std::streamoff)last_offset);
        std::string line;
        int stored = 0;
        int64_t new_offset = last_offset;

        while (std::getline(f, line)) {
            new_offset = (int64_t)f.tellg();
            if (line.empty()) continue;
            try {
                auto j = json::parse(line);
                std::string hook   = j.value("hook",   std::string{});
                std::string target = j.value("target", std::string{});
                std::string reason = j.value("reason", std::string{});
                if (target.empty() || reason.empty()) continue;

                imem::MemoryNode mn;
                mn.topic      = "fail:hook-violation";
                mn.content    = "Blocked [" + hook + "]: " + target
                              + ". Use icmg equivalent. Reason: " + reason;
                mn.keywords   = "fail hook violation block " + hook;
                mn.importance = 2;
                mn.zone       = "default";
                try { mem.store(mn, /*force=*/false); ++stored; } catch (...) {}
            } catch (...) {}
        }

        if (stored > 0) {
            std::ofstream of(offset_p);
            of << new_offset;
        }
        return stored;
    } catch (...) {
        return 0;
    }
}

// ---- tool-budget reset -----------------------------------------------------

void toolBudgetReset() {
    std::error_code ec;
    fs::remove(homeDir() / ".icmg" / "tool-counter.txt", ec);
}

// ---- compress in-place -----------------------------------------------------

std::string compressInPlace(const std::string& input, int threshold) {
    try {
        icmg::compress::CompressOptions opts;
        if (threshold > 0) opts.threshold_tok = threshold;
        icmg::compress::Compressor c(opts);
        auto r = c.compress(input);
        if (r.skipped) return "";
        if (r.text.size() >= input.size()) return ""; // compress no-op
        return r.text;
    } catch (...) {
        return "";
    }
}

} // namespace icmg::core::hooks
