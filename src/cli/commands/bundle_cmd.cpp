// Phase 19: context bundle commands.
//   icmg context <file>           — single-call file/symbols/neighbors/memory bundle
//   icmg pack <task>              — task-context aggregator (recall + context + rules)
//   icmg diff-summary             — symbol-aware git-diff summary
//   icmg explain <error>          — match against errors-resolved memory
//   icmg session save/restore     — checkpoint active context

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../core/exec_utils.hpp"
#include "../../core/output_cap.hpp"
#include "../../graph/graph_store.hpp"
#include "../../imem/memory_store.hpp"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <chrono>
#include <algorithm>
#include <regex>
#include <nlohmann/json.hpp>
#include <fstream>
#include <set>
#include <filesystem>

namespace icmg::cli {

static int64_t bndNow() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

static std::string trunc(const std::string& s, size_t n) {
    if (s.size() <= n) return s;
    return s.substr(0, n - 1) + "…";
}

// =============================================================================
// icmg context <file>
// =============================================================================

class ContextCommand : public BaseCommand {
public:
    std::string name()        const override { return "context"; }
    std::string description() const override { return "File context bundle (graph + symbols + memory)"; }

    void usage() const override {
        std::cout <<
            "Usage: icmg context <file> [options]\n\n"
            "Options:\n"
            "  --depth N         Neighbor depth (default: 1)\n"
            "  --no-symbols      Skip child symbol list\n"
            "  --no-memory       Skip related memory\n"
            "  --max-bytes N     Cap output (default 4096)\n"
            "  --json            JSON output\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || args[0] == "--help") { usage(); return 0; }
        std::string file;
        for (auto& a : args) if (!a.empty() && a[0] != '-') { file = a; break; }
        if (file.empty()) { std::cerr << "icmg context: requires <file>\n"; return 1; }

        bool no_symbols = hasFlag(args, "--no-symbols");
        bool no_memory  = hasFlag(args, "--no-memory");
        size_t cap = 4096;
        try { cap = (size_t)std::stoul(flagValue(args, "--max-bytes", "4096")); } catch (...) {}

        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        graph::GraphStore store(db);
        imem::MemoryStore mem(db);

        auto node = store.getNode(file);
        if (!node) { std::cerr << "icmg context: not found in graph: " << file << "\n"; return 1; }

        std::ostringstream out;
        out << "File: " << node->path << "  (lang=" << node->lang
            << ", " << node->size_bytes << " B, zone=" << node->zone << ")\n";
        if (!node->context.empty())
            out << "Context: " << trunc(node->context, 200) << "\n";

        // Imports + Used-by (1-hop)
        auto outgoing = store.edgesFrom(node->id);
        auto incoming = store.edgesTo(node->id);
        if (!outgoing.empty()) {
            out << "Imports: ";
            int n = 0;
            for (auto& e : outgoing) {
                if (n++ >= 8) { out << "..."; break; }
                std::string p;
                db.query("SELECT path FROM graph_nodes WHERE id=?",
                         {std::to_string(e.dst)},
                         [&](const core::Row& r){ if(!r.empty()) p=r[0]; });
                if (!p.empty()) {
                    namespace fs = std::filesystem;
                    out << fs::path(p).filename().string();
                    out << "(" << e.edge_type << ") ";
                }
            }
            out << "\n";
        }
        if (!incoming.empty()) {
            out << "Used by: ";
            int n = 0;
            for (auto& e : incoming) {
                if (n++ >= 8) { out << "..."; break; }
                std::string p;
                db.query("SELECT path FROM graph_nodes WHERE id=?",
                         {std::to_string(e.src)},
                         [&](const core::Row& r){ if(!r.empty()) p=r[0]; });
                if (!p.empty()) {
                    namespace fs = std::filesystem;
                    out << fs::path(p).filename().string() << " ";
                }
            }
            out << "\n";
        }

        // Child symbols
        if (!no_symbols) {
            auto kids = store.childrenOf(node->id);
            if (!kids.empty()) {
                out << "Symbols (" << kids.size() << "):\n";
                for (auto& s : kids) {
                    out << "  [" << s.kind << "] " << s.symbol_name
                        << "  L" << s.line_start << "-" << s.line_end << "\n";
                }
            }
        }

        // Related memory (best-effort recall by file basename)
        if (!no_memory) {
            namespace fs = std::filesystem;
            std::string base = fs::path(node->path).stem().string();
            if (!base.empty()) {
                auto results = mem.recall(base, 3, false);
                if (!results.empty()) {
                    out << "Memory (top 3 for \"" << base << "\"):\n";
                    for (auto& m : results) {
                        out << "  [" << std::fixed << std::setprecision(1) << m.score
                            << "] " << trunc(m.topic, 60) << "\n";
                    }
                }
            }
        }

        // Cap output
        std::string spill;
        std::string capped = core::capOutput(out.str(), cap, spill);
        std::cout << capped;
        return 0;
    }
};

// =============================================================================
// icmg pack <task>
// =============================================================================

class PackCommand : public BaseCommand {
public:
    std::string name()        const override { return "pack"; }
    std::string description() const override { return "Task-context bundle (recall + files + rules)"; }

    void usage() const override {
        std::cout <<
            "Usage: icmg pack <task description...> [options]\n\n"
            "Options:\n"
            "  --zone Z          Scope to zone\n"
            "  --max-bytes N     Cap output (default 4096)\n"
            "  --memory-limit N  Recall result count (default 5)\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || args[0] == "--help") { usage(); return 0; }
        std::string task;
        for (auto& a : args) {
            if (a.empty() || a[0] == '-') continue;
            if (!task.empty()) task += " ";
            task += a;
        }
        if (task.empty()) { std::cerr << "icmg pack: requires <task>\n"; return 1; }

        std::string zone = flagValue(args, "--zone");
        size_t cap = 4096;
        try { cap = (size_t)std::stoul(flagValue(args, "--max-bytes", "4096")); } catch (...) {}
        int mem_limit = 5;
        try { mem_limit = std::stoi(flagValue(args, "--memory-limit", "5")); } catch (...) {}

        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        imem::MemoryStore mem(db);
        graph::GraphStore store(db);

        std::ostringstream out;
        out << "# Task Context: " << trunc(task, 80) << "\n\n";

        // 1. Memory recall
        auto recalled = zone.empty() ? mem.recall(task, mem_limit, false)
                                       : mem.recallInZone(task, zone, mem_limit, false);
        if (!recalled.empty()) {
            out << "## Memory (top " << recalled.size() << ")\n";
            for (auto& m : recalled) {
                out << "- [" << std::fixed << std::setprecision(1) << m.score
                    << "] " << trunc(m.topic, 70) << "\n";
                out << "  " << trunc(m.content, 120) << "\n";
            }
            out << "\n";
        }

        // 2. Mentioned files: scan task tokens, look up symbol or file
        std::regex word_re(R"(\b[A-Za-z_][A-Za-z0-9_]{2,})");
        std::ostringstream files_section;
        std::set<int64_t> seen_ids;
        int file_hits = 0;
        for (auto it = std::sregex_iterator(task.begin(), task.end(), word_re);
             it != std::sregex_iterator() && file_hits < 5; ++it) {
            std::string tok = (*it)[0].str();
            // Skip very common short tokens
            static const std::set<std::string> stop = {
                "the","and","for","fix","bug","when","why","how",
                "what","this","that","must","can","run","get"};
            std::string tlo = tok; std::transform(tlo.begin(), tlo.end(), tlo.begin(), ::tolower);
            if (stop.count(tlo)) continue;

            // Try symbol first
            auto syms = store.findSymbol(tok);
            for (auto& s : syms) {
                if (seen_ids.insert(s.id).second) {
                    files_section << "### " << s.symbol_name
                                  << " (" << s.kind << ", L" << s.line_start
                                  << "-" << s.line_end << ")\n"
                                  << "Path: " << s.path << "\n\n";
                    ++file_hits;
                }
            }
        }
        if (file_hits > 0) {
            out << "## Files & Symbols (" << file_hits << ")\n" << files_section.str();
        }

        // Phase 28 T2: detect "like X" / "copy of X" / "modeled on X" -> auto-include
        // matching template manifest summary.
        {
            std::regex re_like(R"(\b(?:like|similar to|modeled on|based on|copy of)\s+(\w+))",
                               std::regex::ECMAScript | std::regex::icase);
            std::smatch m;
            if (std::regex_search(task, m, re_like) && m.size() >= 2) {
                std::string ref_name = m[1].str();
                // Lookup template by source path stem matching ref_name.
                std::string mj;
                db.query("SELECT manifest_json FROM templates "
                         "WHERE name = ? OR source_path LIKE ? OR source_path LIKE ? LIMIT 1",
                         {ref_name, "%/" + ref_name + ".%", "%\\" + ref_name + ".%"},
                         [&](const core::Row& r){ if (!r.empty()) mj = r[0]; });
                if (!mj.empty()) {
                    out << "## Reference template: " << ref_name << "\n";
                    try {
                        auto j = nlohmann::json::parse(mj);
                        out << "  source: " << j.value("source", "") << "\n";
                        if (j.contains("required_symbols") && j["required_symbols"].is_array()) {
                            out << "  required_symbols (" << j["required_symbols"].size() << "):\n";
                            for (auto& s : j["required_symbols"]) {
                                out << "    - " << s.value("name", "")
                                    << " (" << s.value("kind", "") << ")\n";
                            }
                        }
                        if (j.contains("structural_markers") && j["structural_markers"].is_array()) {
                            out << "  structural_markers:\n";
                            for (size_t i = 0; i < j["structural_markers"].size(); ++i) {
                                std::string sm = j["structural_markers"][i];
                                out << "    " << sm << "\n";
                            }
                        }
                    } catch (...) {
                        out << "  (manifest parse failed)\n";
                    }
                    out << "\n  Verify after creation: `icmg parity " << ref_name
                        << " <new-file>` or `icmg template apply " << ref_name
                        << " --to <new-file> --check`\n\n";
                }
            }
        }

        std::string spill;
        std::string capped = core::capOutput(out.str(), cap, spill);
        std::cout << capped;
        return 0;
    }

};

// =============================================================================
// icmg diff-summary
// =============================================================================

class DiffSummaryCommand : public BaseCommand {
public:
    std::string name()        const override { return "diff-summary"; }
    std::string description() const override { return "Symbol-aware git diff summary"; }

    void usage() const override {
        std::cout <<
            "Usage: icmg diff-summary [--ref REF] [--full]\n\n"
            "Wraps `git diff [REF]` and groups changes by enclosing symbol\n"
            "(via line_start/line_end of indexed graph nodes).\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (hasFlag(args, "--help")) { usage(); return 0; }
        std::string ref = flagValue(args, "--ref");
        bool full = hasFlag(args, "--full");

        // Run git diff (use --name-only first to get file list)
        std::string cmd = "git diff --name-only";
        if (!ref.empty()) cmd += " " + ref;
        auto result = core::safeExec({"sh", "-c", cmd}, true, 30000);
        if (result.exit_code != 0) {
            std::cerr << "git diff failed: " << result.out << "\n";
            return 1;
        }

        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));

        std::cout << "Diff summary";
        if (!ref.empty()) std::cout << " (ref=" << ref << ")";
        std::cout << ":\n";

        std::istringstream iss(result.out);
        std::string fpath;
        int file_count = 0;
        while (std::getline(iss, fpath)) {
            while (!fpath.empty() && (fpath.back() == '\r' || fpath.back() == '\n')) fpath.pop_back();
            if (fpath.empty()) continue;
            ++file_count;
            std::cout << "M " << fpath << "\n";

            // Get diff for this single file (compact)
            std::string fcmd = "git diff " + (ref.empty() ? std::string() : ref + " ") + "-- \"" + fpath + "\"";
            auto fdiff = core::safeExec({"sh", "-c", fcmd}, true, 15000);
            // Parse @@ markers → list affected line ranges
            std::regex hunk_re(R"(@@\s+-\d+(?:,\d+)?\s+\+(\d+)(?:,(\d+))?\s+@@)");
            std::vector<std::pair<int,int>> ranges;
            for (auto it = std::sregex_iterator(fdiff.out.begin(), fdiff.out.end(), hunk_re);
                 it != std::sregex_iterator(); ++it) {
                int start = std::stoi((*it)[1].str());
                int len = (*it)[2].matched ? std::stoi((*it)[2].str()) : 1;
                ranges.push_back({start, start + len - 1});
            }
            // Map ranges → enclosing symbols
            for (auto& [s, e] : ranges) {
                std::string sym, kind;
                db.query(
                    "SELECT COALESCE(symbol_name,''), kind FROM graph_nodes"
                    " WHERE path=? AND kind != 'file'"
                    "   AND line_start <= ? AND line_end >= ?"
                    " ORDER BY (line_end - line_start) ASC LIMIT 1",
                    {fpath, std::to_string(s), std::to_string(e)},
                    [&](const core::Row& r){ if (r.size() >= 2) { sym = r[0]; kind = r[1]; } });
                if (!sym.empty())
                    std::cout << "  ~ " << kind << " " << sym << " (L" << s << "-" << e << ")\n";
                else
                    std::cout << "  ~ L" << s << "-" << e << "\n";
            }
        }
        if (file_count == 0) std::cout << "  (no changes)\n";

        if (full) {
            std::cout << "\n--- Full diff ---\n";
            std::string raw_cmd = "git diff" + (ref.empty() ? std::string() : " " + ref);
            auto raw = core::safeExec({"sh", "-c", raw_cmd}, true, 30000);
            std::cout << raw.out;
        }
        return 0;
    }
};

// =============================================================================
// icmg explain <error>
// =============================================================================

class ExplainCommand : public BaseCommand {
public:
    std::string name()        const override { return "explain"; }
    std::string description() const override { return "Match error against errors-resolved memory"; }

    int run(const std::vector<std::string>& args) override {
        if (args.empty()) { std::cerr << "icmg explain: requires <error-text>\n"; return 1; }
        std::string err;
        for (auto& a : args) {
            if (a.empty() || a[0] == '-') continue;
            if (!err.empty()) err += " ";
            err += a;
        }
        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        imem::MemoryStore mem(db);

        // Recall scoped to errors-resolved topic prefix + BM25 search by error tokens
        auto candidates = mem.recallByTopic("errors-resolved", 50);
        std::string err_lo = err;
        std::transform(err_lo.begin(), err_lo.end(), err_lo.begin(), ::tolower);
        int matched = 0;
        for (auto& n : candidates) {
            std::string tlo = n.topic;
            std::transform(tlo.begin(), tlo.end(), tlo.begin(), ::tolower);
            std::string pat = tlo.size() > 17 ? tlo.substr(17) : "";
            if (pat.empty()) continue;
            // Extract first 3 tokens of pattern
            std::istringstream iss(pat);
            std::string tok;
            int ok_tokens = 0, total = 0;
            while (std::getline(iss, tok, ' ') && total < 4) {
                if (tok.size() < 3) continue;
                ++total;
                if (err_lo.find(tok) != std::string::npos) ++ok_tokens;
            }
            if (total > 0 && ok_tokens >= (total + 1) / 2) {
                std::cout << "Past resolution #" << n.id << ":\n  " << n.topic << "\n  "
                          << trunc(n.content, 200) << "\n\n";
                if (++matched >= 3) break;
            }
        }
        if (matched == 0) std::cout << "No matching past resolution. Use `icmg known-issue add` to register one.\n";
        return 0;
    }
};

// =============================================================================
// icmg session save / restore
// =============================================================================

class SessionCommand : public BaseCommand {
public:
    std::string name()        const override { return "session"; }
    std::string description() const override { return "Snapshot active task context"; }

    void usage() const override {
        std::cout <<
            "Usage: icmg session <subcommand>\n\n"
            "Subcommands:\n"
            "  save <name>    Snapshot recent recalls + open files\n"
            "  restore <name> Re-emit snapshot bundle\n"
            "  list           Show saved sessions\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || args[0] == "--help") { usage(); return 0; }
        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        imem::MemoryStore mem(db);

        std::string sub = args[0];
        std::vector<std::string> rest(args.begin() + 1, args.end());

        if (sub == "save") {
            if (rest.empty()) { std::cerr << "session save: requires <name>\n"; return 1; }
            std::string name = rest[0];
            // Snapshot = recent query history + freshly recalled context
            auto qs = mem.queryHistory(20);
            std::ostringstream content;
            content << "Session: " << name << "\n"
                    << "Saved at: " << bndNow() << "\n\n"
                    << "Recent queries (" << qs.size() << "):\n";
            for (auto& q : qs) content << "- " << q << "\n";

            imem::MemoryNode n;
            n.topic    = "session-snapshot " + name;
            n.content  = content.str();
            n.keywords = "session snapshot " + name;
            n.importance = 1;
            try { int64_t id = mem.store(n, /*force=*/true); std::cout << "Saved session #" << id << ": " << name << "\n"; }
            catch (...) { std::cerr << "save failed\n"; return 1; }
            return 0;
        }
        if (sub == "restore") {
            if (rest.empty()) { std::cerr << "session restore: requires <name>\n"; return 1; }
            auto items = mem.recallByTopic("session-snapshot " + rest[0], 1);
            if (items.empty()) { std::cerr << "session not found: " << rest[0] << "\n"; return 1; }
            std::cout << items[0].content;
            return 0;
        }
        if (sub == "list") {
            auto items = mem.recallByTopic("session-snapshot", 50);
            for (auto& n : items) std::cout << "#" << n.id << "  " << n.topic << "\n";
            return 0;
        }
        std::cerr << "icmg session: unknown subcommand: " << sub << "\n";
        usage();
        return 1;
    }
};

ICMG_REGISTER_COMMAND("context",      ContextCommand);
ICMG_REGISTER_COMMAND("pack",         PackCommand);
ICMG_REGISTER_COMMAND("diff-summary", DiffSummaryCommand);
ICMG_REGISTER_COMMAND("explain",      ExplainCommand);
ICMG_REGISTER_COMMAND("session",      SessionCommand);

} // namespace icmg::cli
