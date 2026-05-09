// Phase 67 T28: `icmg context-budget` — real Claude session token consumption.
//
// Savings dashboard only counts icmg-instrumented ops (run/compress/pack).
// Misses: raw Read, Bash, WebFetch, MCP calls, conversation tokens. User sees
// "16K used" but Claude shows 100% context filled. Fix: parse latest
// .claude/projects/<hash>/*.jsonl transcript, estimate tokens per source.
//
// Output: per-source breakdown (user/assistant/tool-input/tool-output)
// + top-N largest entries.

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
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
            "  --top N            Show top-N largest entries (default 10)\n"
            "  --json             Machine-readable output\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (hasFlag(args, "--help")) { usage(); return 0; }
        bool json_out = hasFlag(args, "--json");
        int top = 10;
        try { top = std::stoi(flagValue(args, "--top", "10")); } catch (...) {}

        std::string explicit_path = flagValue(args, "--transcript");
        fs::path transcript;
        if (!explicit_path.empty()) {
            transcript = explicit_path;
        } else {
            transcript = findLatestTranscript();
        }
        if (transcript.empty() || !fs::exists(transcript)) {
            std::cerr << "icmg context-budget: no transcript found.\n"
                      << "  Try --transcript <path> or check ~/.claude/projects/\n";
            return 1;
        }

        // Aggregate.
        struct Bucket { int64_t tokens = 0; int64_t calls = 0; };
        std::map<std::string, Bucket> by_source;
        struct Entry { std::string source; std::string label; int64_t tokens; };
        std::vector<Entry> entries;

        std::ifstream f(transcript);
        std::string line;
        int64_t total_tokens = 0;
        while (std::getline(f, line)) {
            if (line.empty()) continue;
            json msg;
            try { msg = json::parse(line); } catch (...) { continue; }
            walkMessage(msg, by_source, entries, total_tokens);
        }

        // Sort entries desc by token count.
        std::sort(entries.begin(), entries.end(),
                  [](const Entry& a, const Entry& b){ return a.tokens > b.tokens; });

        if (json_out) {
            std::cout << "{\"transcript\":\"" << transcript.string()
                      << "\",\"total_tokens\":" << total_tokens
                      << ",\"by_source\":{";
            bool first = true;
            for (auto& [k, v] : by_source) {
                if (!first) std::cout << ",";
                first = false;
                std::cout << "\"" << k << "\":{\"calls\":" << v.calls
                          << ",\"tokens\":" << v.tokens << "}";
            }
            std::cout << "}}\n";
            return 0;
        }

        std::cout << "icmg context-budget — " << transcript.filename().string() << "\n"
                  << std::string(64, '=') << "\n\n";
        std::cout << "Estimated total: " << total_tokens << " tokens\n\n";
        std::cout << "By source:\n";
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
        std::cout << "\nTop " << top << " entries by size:\n";
        for (int i = 0; i < (int)entries.size() && i < top; ++i) {
            std::cout << "  " << std::setw(8) << entries[i].tokens
                      << " tok  [" << entries[i].source << "]  "
                      << entries[i].label.substr(0, 60) << "\n";
        }
        std::cout << "\nNote: estimate via bytes/4. Anthropic actual tokens may differ ±10%.\n";
        std::cout << "      `icmg savings` covers icmg-instrumented ops only.\n";
        std::cout << "      This view captures EVERYTHING in the conversation.\n";
        return 0;
    }

private:
    static fs::path findLatestTranscript() {
        const char* home = std::getenv("USERPROFILE");
        if (!home) home = std::getenv("HOME");
        if (!home) return {};
        fs::path root = fs::path(home) / ".claude" / "projects";
        if (!fs::exists(root)) return {};
        // Walk to find latest *.jsonl.
        fs::path latest;
        fs::file_time_type latest_t = fs::file_time_type::min();
        std::error_code ec;
        for (auto& e : fs::recursive_directory_iterator(root, ec)) {
            if (ec) { ec.clear(); continue; }
            if (!e.is_regular_file()) continue;
            if (e.path().extension() != ".jsonl") continue;
            auto wt = fs::last_write_time(e, ec);
            if (ec) { ec.clear(); continue; }
            if (wt > latest_t) { latest_t = wt; latest = e.path(); }
        }
        return latest;
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
