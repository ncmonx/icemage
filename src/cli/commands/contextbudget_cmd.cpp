// Phase 67 T28: `icmg context-budget` — real Claude session token consumption.
//
// Savings dashboard only counts icmg-instrumented ops (run/compress/pack).
// Misses: raw Read, Bash, WebFetch, MCP calls, conversation tokens. User sees
// "16K used" but Claude shows 100% context filled. Fix: parse latest
// .claude/projects/<hash>/*.jsonl transcript, estimate tokens per source.
//
// Output: per-source breakdown (user/assistant/tool-input/tool-output)
// + top-N largest entries.
//
// v1.5.0: --all-sessions flag aggregates every transcript in the current
// project's ~/.claude/projects/<cwd-encoded>/ directory. JSON output adds a
// `sessions[]` array (one entry per transcript file) so callers like
// `icmg savings --html` can render a per-session detail table.

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../context_budget.hpp"   // accurate API-usage % (--brief)
#include <iterator>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using nlohmann::json;

namespace icmg::cli {

class ContextBudgetCommand : public BaseCommand {
public:
    std::string name()        const override { return "context-budget"; }
    std::string description() const override {
        return "Estimate real Claude session token usage from transcript";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg context-budget [options]\n\n"
            "Reads latest Claude transcript and estimates tokens per source.\n"
            "Closes the gap between `icmg savings` (instrumented ops only)\n"
            "and Claude's real context-window fill.\n\n"
            "Options:\n"
            "  --transcript PATH  Override default ~/.claude/projects/<cwd-hash>/*.jsonl\n"
            "  --thinking         Estimate thinking-block tokens spent this session\n"
            "  --all-sessions     Aggregate every transcript in current project\n"
            "                     (sums across all *.jsonl in ~/.claude/projects/<cwd>/)\n"
            "  --all-users        Enumerate sibling user homes (multi-user aggregate)\n"
            "  --top N            Show top-N largest entries (default 10)\n"
            "  --json             Machine-readable output\n";
    }

    struct Bucket { int64_t tokens = 0; int64_t calls = 0; };
    struct Entry  { std::string source; std::string label; int64_t tokens; };
    struct SessionStats {
        fs::path path;
        std::time_t mtime = 0;
        int64_t total = 0;
        std::string user;  // v1.9.0
        std::map<std::string, Bucket> by_source;
    };

    int run(const std::vector<std::string>& args) override {
        if (hasFlag(args, "--help")) { usage(); return 0; }
        // --brief: one-line ACCURATE context% from the transcript's last API
        // usage (input+cache_creation+cache_read) -- hook-friendly, not bytes/4.
        if (hasFlag(args, "--brief")) {
            std::string ep = flagValue(args, "--transcript");
            fs::path tp = ep.empty() ? findLatestTranscript() : fs::path(ep);
            long long limit = resolveContextLimit(tp.string());  // per-model honest window
            long long used = 0;
            if (!tp.empty() && fs::exists(tp)) {
                std::ifstream bf(tp, std::ios::binary);
                bf.seekg(0, std::ios::end); std::streamoff sz = bf.tellg();
                std::streamoff st = sz > 524288 ? sz - 524288 : 0; bf.seekg(st);
                std::string chunk((std::istreambuf_iterator<char>(bf)), std::istreambuf_iterator<char>());
                size_t pos = 0;
                while (pos <= chunk.size()) {
                    size_t nl = chunk.find('\n', pos);
                    std::string ln = chunk.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
                    if (ln.find("input_tokens") != std::string::npos) {
                        long long t = contextTokensFromUsageLine(ln);
                        if (t > 0) used = t;
                    }
                    if (nl == std::string::npos) break;
                    pos = nl + 1;
                }
            }
            if (used <= 0) { std::cerr << "context-budget --brief: no API usage in transcript\n"; return 1; }
            std::cout << formatBudget(computeBudget(used, limit)) << "\n";
            return 0;
        }
        if (hasFlag(args, "--thinking")) {
            std::string ep = flagValue(args, "--transcript");
            fs::path tp = ep.empty() ? findLatestTranscript() : fs::path(ep);
            if (tp.empty() || !fs::exists(tp)) { std::cerr << "context-budget --thinking: no transcript found\n"; return 1; }
            std::ifstream bf(tp, std::ios::binary);
            std::string body((std::istreambuf_iterator<char>(bf)), std::istreambuf_iterator<char>());
            ThinkingSpend ts = sumThinkingTokens(body);
            std::cout << "thinking tokens (est): " << ts.est_tokens
                      << "  |  " << ts.blocks << " block(s)  |  largest ~" << ts.max_block << "\n"
                      << "note: estimate (~4 chars/token); thinking folds into output_tokens, not billed separately.\n";
            return 0;
        }
        bool json_out     = hasFlag(args, "--json");
        bool all_sessions = hasFlag(args, "--all-sessions");
        bool all_users    = hasFlag(args, "--all-users");
        if (all_users) all_sessions = true;
        int top = 10;
        try { top = std::stoi(flagValue(args, "--top", "10")); } catch (...) {}

        std::string explicit_path = flagValue(args, "--transcript");

        // --- multi-session path -------------------------------------------
        if (all_sessions && explicit_path.empty()) {
            return runAllSessions(json_out, top, all_users);
        }

        // --- single-transcript path (back-compat) -------------------------
        fs::path transcript;
        if (!explicit_path.empty())  transcript = explicit_path;
        else                          transcript = findLatestTranscript();

        if (transcript.empty() || !fs::exists(transcript)) {
            std::cerr << "icmg context-budget: no transcript found.\n"
                      << "  Try --transcript <path> or check ~/.claude/projects/\n";
            return 1;
        }

        std::map<std::string, Bucket> by_source;
        std::vector<Entry> entries;
        int64_t total_tokens = 0;
        parseTranscript(transcript, by_source, entries, total_tokens);

        // v2.0.1: --percent prints just the context-window fill % (for the C5
        // idle-compact advisor + scripts). Window default 200K (CC flagship);
        // override via ICMG_CONTEXT_WINDOW. Capped to [0,100].
        if (hasFlag(args, "--percent")) {
            long long window = 200000;
            if (const char* w = std::getenv("ICMG_CONTEXT_WINDOW")) {
                try { window = std::stoll(w); } catch (...) {}
            }
            int pct = window > 0 ? (int)(total_tokens * 100 / window) : 0;
            if (pct > 100) pct = 100;
            if (pct < 0) pct = 0;
            std::cout << pct << "\n";
            return 0;
        }

        std::sort(entries.begin(), entries.end(),
                  [](const Entry& a, const Entry& b){ return a.tokens > b.tokens; });

        if (json_out) {
            std::cout << "{\"transcript\":\"" << jsonEscape(transcript.string())
                      << "\",\"total_tokens\":" << total_tokens
                      << ",\"by_source\":" << bySourceJson(by_source)
                      << "}\n";
            return 0;
        }
        emitText(transcript.filename().string(), total_tokens, by_source, entries, top);
        return 0;
    }

private:
    // ---- aggregate-all-sessions path -------------------------------------
    int runAllSessions(bool json_out, int top, bool all_users) {
        std::vector<std::pair<fs::path,std::string>> scan_targets;
        if (all_users) {
            std::string enc = encodeCwd();
            if (enc.empty()) {
                std::cerr << "icmg context-budget: cannot resolve cwd\n";
                return 1;
            }
            for (auto& home : enumUserHomes()) {
                fs::path pd = home.first / ".claude" / "projects" / enc;
                std::error_code ec; if (fs::exists(pd, ec)) {
                    scan_targets.push_back({pd, home.second});
                }
            }
        } else {
            fs::path proj_dir = currentProjectDir();
            if (!proj_dir.empty()) scan_targets.push_back({proj_dir, currentUser()});
        }

        if (scan_targets.empty()) {
            std::cerr << "icmg context-budget: project transcript dir not found.\n"
                      << "  Looked for ~/.claude/projects/" << encodeCwd()
                      << "\n  Run from your project root, or pass --transcript PATH.\n";
            return 1;
        }

        std::vector<SessionStats> sessions;
        std::map<std::string, Bucket> agg_source;
        std::vector<Entry> agg_entries;
        int64_t grand_total = 0;
        fs::path display_proj_dir = scan_targets.front().first;

        std::error_code ec;
        for (auto& [proj_dir, user_name] : scan_targets) {
        if (!fs::exists(proj_dir, ec)) { ec.clear(); continue; }
        for (auto& e : fs::recursive_directory_iterator(proj_dir, ec)) {
            if (ec) { ec.clear(); continue; }
            if (!e.is_regular_file()) continue;
            if (e.path().extension() != ".jsonl") continue;

            SessionStats s;
            s.path = e.path();
            s.user = user_name;
            auto wt = fs::last_write_time(e, ec);
            if (!ec) {
                // Convert file_time_type → std::time_t (portable enough).
                auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                    wt - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
                s.mtime = std::chrono::system_clock::to_time_t(sctp);
            } else { ec.clear(); }

            parseTranscript(s.path, s.by_source, agg_entries, s.total);
            for (auto& [k, v] : s.by_source) {
                agg_source[k].tokens += v.tokens;
                agg_source[k].calls  += v.calls;
            }
            grand_total += s.total;
            sessions.push_back(std::move(s));
        }
        }  // end scan_targets loop
        // Newest first.
        std::sort(sessions.begin(), sessions.end(),
                  [](const SessionStats& a, const SessionStats& b){ return a.mtime > b.mtime; });
        std::sort(agg_entries.begin(), agg_entries.end(),
                  [](const Entry& a, const Entry& b){ return a.tokens > b.tokens; });

        if (json_out) {
            std::map<std::string,int64_t> per_user_total;
            for (auto& s : sessions) per_user_total[s.user] += s.total;
            std::cout << "{\"project_dir\":\"" << jsonEscape(display_proj_dir.string())
                      << "\",\"session_count\":" << sessions.size()
                      << ",\"user_count\":" << per_user_total.size()
                      << ",\"total_tokens\":" << grand_total
                      << ",\"by_source\":" << bySourceJson(agg_source)
                      << ",\"users\":[";
            bool fu = true;
            for (auto& [u, t] : per_user_total) {
                if (!fu) std::cout << ",";
                fu = false;
                std::cout << "{\"user\":\"" << jsonEscape(u) << "\",\"total_tokens\":" << t << "}";
            }
            std::cout << "],\"sessions\":[";
            bool first = true;
            for (auto& s : sessions) {
                if (!first) std::cout << ",";
                first = false;
                std::cout << "{\"file\":\"" << jsonEscape(s.path.filename().string())
                          << "\",\"user\":\"" << jsonEscape(s.user) << "\""
                          << ",\"mtime\":" << s.mtime
                          << ",\"total_tokens\":" << s.total
                          << ",\"by_source\":" << bySourceJson(s.by_source)
                          << "}";
            }
            std::cout << "]}\n";
            return 0;
        }

        std::cout << "icmg context-budget — project aggregate (" << sessions.size()
                  << " session" << (sessions.size() == 1 ? "" : "s") << ")\n"
                  << std::string(64, '=') << "\n\n";
        std::cout << "Project dir: " << display_proj_dir.string() << "\n";
        std::cout << "Grand total: " << grand_total << " tokens\n\n";
        std::cout << "Aggregate by source:\n";
        emitBySource(agg_source, grand_total);

        std::cout << "\nSessions (newest first):\n";
        std::cout << "  " << std::left
                  << std::setw(14) << "user"
                  << std::setw(40) << "file"
                  << std::setw(14) << "tokens"
                  << "mtime\n";
        std::cout << "  " << std::string(88, '-') << "\n";
        for (auto& s : sessions) {
            char buf[20]; std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M",
                std::localtime(&s.mtime));
            std::cout << "  " << std::left
                      << std::setw(14) << s.user.substr(0, 13)
                      << std::setw(40) << s.path.filename().string().substr(0, 38)
                      << std::setw(14) << s.total
                      << buf << "\n";
        }
        std::cout << "\nNote: bytes/4 estimate; ±10% vs Anthropic exact count.\n";
        return 0;
    }

    // ---- helpers ---------------------------------------------------------
    // Convert cwd into Claude project-dir encoding: replace each ':', '\',
    // '/' or ' ' with '-'. Example:
    //   D:\Data Kerja\Personal\AI\icm-graph
    //   → D--Data-Kerja-Personal-AI-icm-graph
    // v1.9.0: detect current OS user (USERNAME on Win, USER on POSIX).
    static std::string currentUser() {
        const char* u = std::getenv("USERNAME");
        if (!u || !*u) u = std::getenv("USER");
        if (!u || !*u) u = std::getenv("LOGNAME");
        return (u && *u) ? std::string(u) : std::string("unknown");
    }

    // v1.9.0: enumerate sibling user homes. Win: C:\Users\* with .claude/.
    // POSIX: /home/* plus /root.
    static std::vector<std::pair<fs::path,std::string>> enumUserHomes() {
        std::vector<std::pair<fs::path,std::string>> out;
        std::error_code ec;
#ifdef _WIN32
        const char* up = std::getenv("USERPROFILE");
        fs::path users_root;
        if (up && *up) users_root = fs::path(up).parent_path();
        if (users_root.empty() || !fs::exists(users_root, ec)) {
            users_root = fs::path("C:\\Users");
        }
#else
        fs::path users_root = "/home";
#endif
        if (!fs::exists(users_root, ec)) return out;
        for (auto& e : fs::directory_iterator(users_root, ec)) {
            if (ec) { ec.clear(); continue; }
            if (!e.is_directory(ec)) continue;
            std::string name = e.path().filename().string();
            if (name == "Default" || name == "Default User" ||
                name == "All Users" || name == "Public" ||
                name == "desktop.ini" || name == "WDAGUtilityAccount" ||
                name.empty() || name[0] == '.') continue;
            fs::path claude_dir = e.path() / ".claude";
            if (fs::exists(claude_dir, ec)) {
                out.push_back({e.path(), name});
            }
        }
#ifndef _WIN32
        fs::path root_home = "/root";
        if (fs::exists(root_home / ".claude", ec)) {
            out.push_back({root_home, "root"});
        }
#endif
        return out;
    }

    static std::string encodeCwd() {
        std::error_code ec;
        fs::path cwd = fs::current_path(ec);
        if (ec) return {};
        std::string s = cwd.string();
        std::string out;
        out.reserve(s.size());
        for (char c : s) {
            if (c == ':' || c == '\\' || c == '/' || c == ' ') out.push_back('-');
            else out.push_back(c);
        }
        return out;
    }

    static fs::path currentProjectDir() {
        const char* home = std::getenv("USERPROFILE");
        if (!home) home = std::getenv("HOME");
        if (!home) return {};
        std::string enc = encodeCwd();
        if (enc.empty()) return {};
        return fs::path(home) / ".claude" / "projects" / enc;
    }

    static fs::path findLatestTranscript() {
        const char* home = std::getenv("USERPROFILE");
        if (!home) home = std::getenv("HOME");
        if (!home) return {};
        fs::path root = fs::path(home) / ".claude" / "projects";
        if (!fs::exists(root)) return {};
        // Prefer current-project dir; fall back to global walk.
        fs::path proj = currentProjectDir();
        fs::path latest;
        fs::file_time_type latest_t = fs::file_time_type::min();
        std::error_code ec;
        auto pick = [&](const fs::path& base) {
            for (auto& e : fs::recursive_directory_iterator(base, ec)) {
                if (ec) { ec.clear(); continue; }
                if (!e.is_regular_file()) continue;
                if (e.path().extension() != ".jsonl") continue;
                auto wt = fs::last_write_time(e, ec);
                if (ec) { ec.clear(); continue; }
                if (wt > latest_t) { latest_t = wt; latest = e.path(); }
            }
        };
        if (!proj.empty() && fs::exists(proj)) pick(proj);
        if (latest.empty()) pick(root);
        return latest;
    }

    static void parseTranscript(const fs::path& transcript,
                                 std::map<std::string, Bucket>& by_source,
                                 std::vector<Entry>& entries,
                                 int64_t& total) {
        std::ifstream f(transcript);
        std::string line;
        while (std::getline(f, line)) {
            if (line.empty()) continue;
            json msg;
            try { msg = json::parse(line); } catch (...) { continue; }
            walkMessage(msg, by_source, entries, total);
        }
    }

    static std::string bySourceJson(const std::map<std::string, Bucket>& by_source) {
        std::ostringstream os;
        os << "{";
        bool first = true;
        for (auto& [k, v] : by_source) {
            if (!first) os << ",";
            first = false;
            os << "\"" << jsonEscape(k) << "\":{\"calls\":" << v.calls
               << ",\"tokens\":" << v.tokens << "}";
        }
        os << "}";
        return os.str();
    }

    static std::string jsonEscape(const std::string& s) {
        std::string out;
        out.reserve(s.size() + 4);
        for (char c : s) {
            switch (c) {
                case '"':  out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n";  break;
                case '\r': out += "\\r";  break;
                case '\t': out += "\\t";  break;
                default:
                    if ((unsigned char)c < 0x20) {
                        char buf[8]; std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                        out += buf;
                    } else out.push_back(c);
            }
        }
        return out;
    }

    static void emitBySource(const std::map<std::string, Bucket>& by_source,
                              int64_t total_tokens) {
        std::cout << "  " << std::left
                  << std::setw(20) << "source"
                  << std::setw(10) << "calls"
                  << std::setw(10) << "tokens"
                  << "share\n";
        std::cout << "  " << std::string(50, '-') << "\n";
        for (auto& [src, b] : by_source) {
            int pct = total_tokens > 0 ? (int)(100 * b.tokens / total_tokens) : 0;
            std::cout << "  " << std::left
                      << std::setw(20) << src
                      << std::setw(10) << b.calls
                      << std::setw(10) << b.tokens
                      << pct << "%\n";
        }
    }

    static void emitText(const std::string& title, int64_t total_tokens,
                          const std::map<std::string, Bucket>& by_source,
                          const std::vector<Entry>& entries, int top) {
        std::cout << "icmg context-budget — " << title << "\n"
                  << std::string(64, '=') << "\n\n";
        std::cout << "Estimated total: " << total_tokens << " tokens\n\n";
        std::cout << "By source:\n";
        emitBySource(by_source, total_tokens);
        std::cout << "\nTop " << top << " entries by size:\n";
        for (int i = 0; i < (int)entries.size() && i < top; ++i) {
            std::cout << "  " << std::setw(8) << entries[i].tokens
                      << " tok  [" << entries[i].source << "]  "
                      << entries[i].label.substr(0, 60) << "\n";
        }
        std::cout << "\nNote: estimate via bytes/4. Anthropic actual tokens may differ ±10%.\n";
        std::cout << "      `icmg savings` covers icmg-instrumented ops only.\n";
        std::cout << "      This view captures EVERYTHING in the conversation.\n";
    }

    template<typename Map, typename Vec>
    static void walkMessage(const json& msg, Map& by_source, Vec& entries,
                             int64_t& total) {
        // Claude transcript schema varies; we look for: type, role, content[].
        std::string role = msg.value("type", "");
        if (role.empty()) role = msg.value("role", "");
        if (msg.contains("message") && msg["message"].is_object()) {
            const auto& m = msg["message"];
            std::string mrole = m.value("role", "");
            if (!mrole.empty()) role = mrole;
            if (m.contains("content")) walkContent(m["content"], role, by_source, entries, total);
        } else if (msg.contains("content")) {
            walkContent(msg["content"], role, by_source, entries, total);
        }
    }

    template<typename Map, typename Vec>
    static void walkContent(const json& content, const std::string& role,
                             Map& by_source, Vec& entries, int64_t& total) {
        if (content.is_string()) {
            int64_t tok = (int64_t)content.get<std::string>().size() / 4;
            std::string src = role.empty() ? "other" : role;
            by_source[src].tokens += tok;
            ++by_source[src].calls;
            total += tok;
            entries.push_back({src, content.get<std::string>().substr(0, 80), tok});
            return;
        }
        if (!content.is_array()) return;
        for (auto& blk : content) {
            std::string type = blk.value("type", "");
            std::string label = blk.value("name", "");
            std::string src;
            int64_t bytes = 0;
            if (type == "text") {
                src = role.empty() ? "text" : role;
                if (blk.contains("text")) bytes = (int64_t)blk["text"].get<std::string>().size();
            } else if (type == "tool_use") {
                src = "tool-input:" + label;
                if (blk.contains("input")) bytes = (int64_t)blk["input"].dump().size();
            } else if (type == "tool_result") {
                src = "tool-output";
                if (blk.contains("content")) {
                    if (blk["content"].is_string()) {
                        bytes = (int64_t)blk["content"].get<std::string>().size();
                    } else if (blk["content"].is_array()) {
                        for (auto& c : blk["content"]) {
                            if (c.contains("text")) bytes += (int64_t)c["text"].get<std::string>().size();
                        }
                    }
                }
            } else if (type == "thinking") {
                src = "thinking";
                if (blk.contains("thinking")) bytes = (int64_t)blk["thinking"].get<std::string>().size();
            } else {
                continue;
            }
            int64_t tok = bytes / 4;
            by_source[src].tokens += tok;
            ++by_source[src].calls;
            total += tok;
            std::string preview;
            if (blk.contains("text") && blk["text"].is_string()) preview = blk["text"].get<std::string>();
            else if (blk.contains("input")) preview = blk["input"].dump();
            else if (!label.empty()) preview = label;
            entries.push_back({src, preview.substr(0, 80), tok});
        }
    }
};

ICMG_REGISTER_COMMAND("context-budget", ContextBudgetCommand);

} // namespace icmg::cli
