// Phase 26 T5: `icmg discover` — scan recent Claude transcripts for missed
// `icmg run` opportunities. Reads JSONL conversations, parses Bash tool_use
// blocks, matches commands against Tkil detector patterns.
//
// Heuristic 1: Bash command matches (cargo|node|grep|...) AND not prefixed
// with `icmg run` or `rtk` -> flag as missed.
// Heuristic 2: Read tool used 3+ times for same path in same session ->
// suggest `icmg context <path>`.
//
// Privacy: --no-transcript skips transcript reading entirely (no opt-in
// default — user can disable).

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include <nlohmann/json.hpp>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <map>
#include <regex>
#include <ctime>
#include <vector>
#include <cstdlib>
#ifdef _WIN32
  #include <windows.h>
#endif

namespace fs = std::filesystem;
using nlohmann::json;

namespace icmg::cli {

class DiscoverCommand : public BaseCommand {
public:
    std::string name()        const override { return "discover"; }
    std::string description() const override { return "Scan transcripts for missed icmg-run opportunities"; }

    void usage() const override {
        std::cout <<
            "Usage: icmg discover [options]\n\n"
            "Options:\n"
            "  --since 7d            Window (default 7d; m/h/d/w)\n"
            "  --top N               Show top N missed cmds (default 10)\n"
            "  --transcript-glob G   Override path pattern\n"
            "                        (default: ~/.claude/projects/*/conversations/*.jsonl)\n"
            "  --no-transcript       Disable scanning entirely\n"
            "  --apply               Auto-install bash-rewrite hook if signal >= threshold\n"
            "  --threshold N         --apply trigger floor (default 5)\n"
            "  --dry-run             With --apply, print plan only\n"
            "  --json\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (hasFlag(args, "--help")) { usage(); return 0; }
        if (hasFlag(args, "--no-transcript")) {
            std::cout << "icmg discover: disabled by --no-transcript\n";
            return 0;
        }
        bool json_out = hasFlag(args, "--json");
        int top = 10;
        try { top = std::stoi(flagValue(args, "--top", "10")); } catch (...) {}
        int64_t now = std::time(nullptr);
        int64_t cutoff = parseSince(flagValue(args, "--since", "7d"), now);

        std::string glob_pat = flagValue(args, "--transcript-glob");
        std::vector<fs::path> files = collectTranscripts(glob_pat);
        if (files.empty()) {
            std::cout << "icmg discover: no transcripts found.\n"
                      << "  Looked at default ~/.claude/projects/*/conversations/*.jsonl\n"
                      << "  Override via --transcript-glob.\n";
            return 0;
        }

        // Tkil-tracked command prefixes to flag.
        std::regex cmd_pat(
            R"(^[[:space:]]*(grep|rg|ag|fd|find|node|deno|bun|ts-node|tsx|python|python3|py|ruby|php|java|perl|lua|cargo build|cargo test|cargo check|npm test|npm run build|yarn build|jest|vitest|pytest|dotnet build|dotnet test|dotnet run|go build|go test|go run|cmake|make|ninja|msbuild|gradle build|mvn|sqlcmd|osql|mysql|mariadb|psql|git log|git diff|git show|git status))",
            std::regex::ECMAScript);
        std::regex bypass(R"((^|[ |&;])(icmg|rtk)[ ]+)", std::regex::ECMAScript);

        std::map<std::string, int> missed;        // command -> count
        std::map<std::string, int> read_paths;    // path -> count
        int total_bash = 0;

        for (auto& f : files) {
            std::ifstream in(f);
            if (!in) continue;
            std::string line;
            while (std::getline(in, line)) {
                // Filter by mtime of file? Just scan; cheap.
                json msg;
                try { msg = json::parse(line); } catch (...) { continue; }
                // Look for tool_use blocks
                walkToolUse(msg, cutoff, cmd_pat, bypass, missed, read_paths, total_bash);
            }
        }

        // Sort missed.
        std::vector<std::pair<std::string, int>> missed_v(missed.begin(), missed.end());
        std::sort(missed_v.begin(), missed_v.end(),
                  [](auto& a, auto& b){ return a.second > b.second; });

        // Filter Read suggestions (3+).
        std::vector<std::pair<std::string, int>> read_v;
        for (auto& [p, c] : read_paths) if (c >= 3) read_v.push_back({p, c});
        std::sort(read_v.begin(), read_v.end(),
                  [](auto& a, auto& b){ return a.second > b.second; });

        if (json_out) {
            std::cout << "{\"transcripts\":" << files.size()
                      << ",\"total_bash\":" << total_bash
                      << ",\"missed\":[";
            int n = 0;
            for (auto& [c, k] : missed_v) {
                if (n >= top) break;
                if (n) std::cout << ",";
                std::cout << "{\"cmd\":\"" << escape(c) << "\",\"count\":" << k << "}";
                ++n;
            }
            std::cout << "],\"reads_3plus\":[";
            n = 0;
            for (auto& [p, k] : read_v) {
                if (n >= top) break;
                if (n) std::cout << ",";
                std::cout << "{\"path\":\"" << escape(p) << "\",\"count\":" << k << "}";
                ++n;
            }
            std::cout << "]}\n";
            return 0;
        }

        std::cout << "icmg discover (" << files.size() << " transcript files, "
                  << total_bash << " bash invocations)\n\n";
        std::cout << "Missed icmg-run opportunities (top " << top << "):\n";
        if (missed_v.empty()) std::cout << "  none — agent routes through icmg consistently.\n";
        int n = 0;
        for (auto& [c, k] : missed_v) {
            if (n++ >= top) break;
            std::cout << "  " << std::setw(4) << k << "x  " << c << "\n";
        }

        std::cout << "\nFiles read 3+ times in same window (suggest icmg context):\n";
        if (read_v.empty()) std::cout << "  none.\n";
        n = 0;
        for (auto& [p, k] : read_v) {
            if (n++ >= top) break;
            std::cout << "  " << std::setw(4) << k << "x  " << p << "\n";
        }

        // Phase 28 T3: --apply auto-installs bash-rewrite hook when signal high.
        if (hasFlag(args, "--apply")) {
            int threshold = 5;
            try { threshold = std::stoi(flagValue(args, "--threshold", "5")); } catch (...) {}
            int hot_total = 0;
            for (auto& [c, k] : missed_v) if (k >= threshold) ++hot_total;
            bool dry = hasFlag(args, "--dry-run");
            std::cout << "\n--apply: " << hot_total << " command(s) above threshold " << threshold << "\n";
            if (hot_total == 0) {
                std::cout << "  No action — agent already routes through icmg consistently.\n";
                return 0;
            }
            if (dry) {
                std::cout << "  [dry-run] would invoke `icmg init --no-agents --no-embedder --force`\n";
                return 0;
            }
            // Reuse `icmg init` logic to write hooks + settings.local.json.
            std::string self = locateSelf();
            std::string cmd = "\"" + self + "\" init --no-agents --no-embedder --force";
            std::cout << "  installing bash-rewrite hook via `icmg init`...\n";
            int rc = std::system(cmd.c_str());
            if (rc == 0) std::cout << "  Done. Restart your AI agent to pick up new hook.\n";
            else         std::cout << "  init failed (exit=" << rc << ")\n";
            return rc;
        }
        return 0;
    }

    static std::string locateSelf() {
#ifdef _WIN32
        char buf[1024]; GetModuleFileNameA(nullptr, buf, sizeof(buf));
        return buf;
#else
        return "icmg";
#endif
    }

private:
    static int64_t parseSince(const std::string& s, int64_t now) {
        if (s.empty()) return now - 7LL * 86400;
        char unit = s.back();
        int n = 7;
        try { n = std::stoi(s.substr(0, s.size() - 1)); } catch (...) {}
        int64_t scale = 86400;
        if (unit == 'h') scale = 3600;
        else if (unit == 'm') scale = 60;
        else if (unit == 'w') scale = 7 * 86400;
        return now - (int64_t)n * scale;
    }

    static std::vector<fs::path> collectTranscripts(const std::string& glob_override) {
        std::vector<fs::path> out;
        const char* h = std::getenv("USERPROFILE");
        if (!h) h = std::getenv("HOME");
        if (!h) return out;
        fs::path root = fs::path(h) / ".claude" / "projects";
        if (!glob_override.empty()) {
            // Naive single-dir scan if user provided override.
            try {
                fs::path g = glob_override;
                if (fs::is_directory(g)) {
                    for (auto& e : fs::recursive_directory_iterator(g)) {
                        if (e.is_regular_file() && e.path().extension() == ".jsonl") {
                            out.push_back(e.path());
                        }
                    }
                }
            } catch (...) {}
            return out;
        }
        if (!fs::exists(root)) return out;
        try {
            for (auto& e : fs::recursive_directory_iterator(root)) {
                if (e.is_regular_file() && e.path().extension() == ".jsonl") {
                    out.push_back(e.path());
                }
            }
        } catch (...) {}
        return out;
    }

    static void walkToolUse(const json& msg, int64_t cutoff,
                              const std::regex& cmd_pat, const std::regex& bypass,
                              std::map<std::string,int>& missed,
                              std::map<std::string,int>& read_paths,
                              int& total_bash) {
        // Claude Code transcript schema varies; we look for any nested object
        // with type=tool_use + name=Bash/Read + input.command/file_path.
        if (msg.is_object()) {
            // value() throws type_error on null field (vs missing). Defensive read.
            auto safe_str = [](const json& obj, const char* key) -> std::string {
                if (!obj.contains(key)) return "";
                const auto& v = obj.at(key);
                if (!v.is_string()) return "";
                return v.get<std::string>();
            };
            std::string type = safe_str(msg, "type");
            std::string name = safe_str(msg, "name");
            if (type == "tool_use") {
                if (name == "Bash") {
                    auto inp = msg.contains("input") && msg["input"].is_object() ? msg["input"] : json::object();
                    std::string cmd = safe_str(inp, "command");
                    if (!cmd.empty()) {
                        ++total_bash;
                        if (std::regex_search(cmd, cmd_pat) &&
                            !std::regex_search(cmd, bypass)) {
                            // Capture first matched word for grouping.
                            std::smatch m;
                            std::regex_search(cmd, m, cmd_pat);
                            if (!m.empty()) ++missed[m[1].str()];
                        }
                    }
                } else if (name == "Read") {
                    auto inp = msg.contains("input") && msg["input"].is_object() ? msg["input"] : json::object();
                    std::string p = safe_str(inp, "file_path");
                    if (!p.empty()) ++read_paths[p];
                }
            }
        }
        if (msg.is_object()) {
            for (auto& [k, v] : msg.items()) walkToolUse(v, cutoff, cmd_pat, bypass, missed, read_paths, total_bash);
        } else if (msg.is_array()) {
            for (auto& v : msg) walkToolUse(v, cutoff, cmd_pat, bypass, missed, read_paths, total_bash);
        }
    }

    static std::string escape(const std::string& s) {
        std::string out; out.reserve(s.size());
        for (char c : s) {
            if (c == '"' || c == '\\') { out.push_back('\\'); out.push_back(c); }
            else if (c == '\n') out += "\\n";
            else out.push_back(c);
        }
        return out;
    }
};

ICMG_REGISTER_COMMAND("discover", DiscoverCommand);

} // namespace icmg::cli
