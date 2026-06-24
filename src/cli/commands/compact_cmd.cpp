// src/cli/commands/compact_cmd.cpp
// v2.8.4: `icmg compact` -- session summary / handoff generator.
//
// Different from `icmg compact-bg` (background memory compaction worker).
// This command synthesizes a compact handoff summary from recent memory
// and .remember/ files, writes it to .icmg/compact-handoff.md, and
// prints it to stdout -- ready to paste as session context for the next turn.
//
// Usage:
//   icmg compact                    # summarize + write .icmg/compact-handoff.md
//   icmg compact --print-only       # stdout only, no file write
//   icmg compact --limit 30         # pull last 30 memory nodes (default 20)
//   icmg compact --out handoff.md   # override output path

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../imem/memory_store.hpp"
#include "../../core/migrator.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace icmg::cli {

namespace {

// Extract first sentence (up to first '.', '!', '?' or 120 chars).
std::string firstSentence(const std::string& s) {
    size_t end = s.find_first_of(".!?\n");
    if (end == std::string::npos) end = std::min(s.size(), (size_t)120);
    else end = std::min(end + 1, s.size());
    std::string r = s.substr(0, end);
    // strip leading whitespace
    size_t a = r.find_first_not_of(" \t\r\n");
    return a == std::string::npos ? "" : r.substr(a);
}

// Read a file into string, return "" if missing.
std::string readFileSafe(const fs::path& p) {
    std::ifstream f(p);
    if (!f) return "";
    std::ostringstream ss; ss << f.rdbuf();
    return ss.str();
}

// Simple unix timestamp -> human readable.
std::string nowStr() {
    auto t  = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(t);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&tt));
    return buf;
}

} // namespace

class CompactCommand : public BaseCommand {
public:
    std::string name()        const override { return "compact"; }
    std::string description() const override {
        return "Generate session handoff summary from recent memory + .remember/";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg compact [options]\n\n"
            "Synthesizes a compact handoff summary from:\n"
            "  - Recent memory nodes (decisions, fixes, plans)\n"
            "  - .remember/now.md and .remember/recent.md (if present)\n\n"
            "Output: .icmg/compact-handoff.md (and stdout)\n\n"
            "Options:\n"
            "  --limit N       Pull last N memory nodes (default: 20)\n"
            "  --out <path>    Override output file path\n"
            "  --print-only    Print to stdout only, no file write\n"
            "  --quiet         Suppress [compact] prefix lines\n\n"
            "Tip: paste the output as session context for the next turn.\n"
            "     Saves 60-80%% tokens vs re-reading full history.\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (hasFlag(args, "--help")) { usage(); return 0; }

        int  limit      = 20;
        bool print_only = hasFlag(args, "--print-only");
        bool quiet      = hasFlag(args, "--quiet");
        std::string out_path = flagValue(args, "--out", "");
        try {
            std::string lv = flagValue(args, "--limit", "20");
            if (!lv.empty()) limit = std::stoi(lv);
        } catch (...) {}

        // --- Gather memory nodes ---
        std::vector<std::string> bullets;
        try {
            core::Config& cfg = core::Config::instance();
            core::Db db(cfg.projectDbPath("."));
            core::Migrator mig("__nonexistent__");
            mig.runAll(db);
            imem::MemoryStore store(db);
            auto nodes = store.recall("decision fix plan session", limit);
            std::set<std::string> seen;
            for (auto& n : nodes) {
                std::string sent = firstSentence(n.content);
                if (sent.empty() || seen.count(sent)) continue;
                seen.insert(sent);
                bullets.push_back("- [" + n.topic + "] " + sent);
            }
        } catch (...) { /* DB unavailable */ }

        // --- Gather .remember/ files ---
        std::string now_md    = readFileSafe(".remember/now.md");
        std::string recent_md = readFileSafe(".remember/recent.md");

        // --- Build summary ---
        std::ostringstream ss;
        ss << "# icmg compact handoff — " << nowStr() << "\n\n";

        if (!bullets.empty()) {
            ss << "## Recent decisions & fixes\n";
            for (auto& b : bullets) ss << b << "\n";
            ss << "\n";
        }

        if (!now_md.empty()) {
            ss << "## Current focus (.remember/now.md)\n";
            // cap at 800 chars
            std::string cap = now_md.size() > 800 ? now_md.substr(0, 800) + "\n...[truncated]" : now_md;
            ss << cap << "\n\n";
        }

        if (!recent_md.empty()) {
            ss << "## Recent work (.remember/recent.md)\n";
            std::string cap = recent_md.size() > 600 ? recent_md.substr(0, 600) + "\n...[truncated]" : recent_md;
            ss << cap << "\n";
        }

        if (bullets.empty() && now_md.empty() && recent_md.empty()) {
            ss << "_No memory or .remember/ content found. "
               << "Run `icmg store` to save decisions first._\n";
        }

        std::string summary = ss.str();

        // --- Write file ---
        if (!print_only) {
            std::string dest = out_path.empty() ? ".icmg/compact-handoff.md" : out_path;
            fs::create_directories(fs::path(dest).parent_path());
            std::ofstream f(dest);
            if (f) {
                f << summary;
                if (!quiet)
                    std::cerr << "[compact] written: " << dest << "\n";
            } else {
                std::cerr << "[compact] warning: could not write " << dest << "\n";
            }
        }

        std::cout << summary;
        return 0;
    }
};

ICMG_REGISTER_COMMAND("compact", CompactCommand);

} // namespace icmg::cli
