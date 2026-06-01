// Phase 20: heuristic file summarizer.
// Output: file header + symbol tree (if Phase 18 indexed) + class/fn outline +
// last 5 lines. Cap default 60 lines.

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../graph/graph_store.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <filesystem>
#include <algorithm>

namespace icmg::cli {

class SummarizeCommand : public BaseCommand {
public:
    std::string name()        const override { return "summarize"; }
    std::string description() const override { return "Heuristic file outline (avoid full Read on large files)"; }

    void usage() const override {
        std::cout <<
            "Usage: icmg summarize <file> [options]\n\n"
            "Options:\n"
            "  --max-lines N    Output cap (default 60)\n"
            "  --bodies         Include first 3 lines of each symbol body\n"
            "  --full-symbols   List all symbols (no truncation)\n"
            "  --json           JSON output\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || args[0] == "--help") { usage(); return 0; }
        std::string path;
        for (auto& a : args) if (!a.empty() && a[0] != '-') { path = a; break; }
        if (path.empty()) { std::cerr << "icmg summarize: requires <file>\n"; return 1; }

        int max_lines = 60;
        try { max_lines = std::stoi(flagValue(args, "--max-lines", "60")); } catch (...) {}
        bool bodies = hasFlag(args, "--bodies");
        bool full_syms = hasFlag(args, "--full-symbols");
        bool json_out = hasFlag(args, "--json");

        // Read file
        std::ifstream f(path, std::ios::binary);
        if (!f) { std::cerr << "icmg summarize: cannot open " << path << "\n"; return 1; }
        std::vector<std::string> lines;
        std::string ln;
        while (std::getline(f, ln)) {
            while (!ln.empty() && ln.back() == '\r') ln.pop_back();
            lines.push_back(ln);
        }

        namespace fs = std::filesystem;
        auto sz = fs::file_size(path);
        std::string lang;
        std::string ext = fs::path(path).extension().string();
        if (ext == ".cpp" || ext == ".hpp" || ext == ".h" || ext == ".cc") lang = "cpp";
        else if (ext == ".cs") lang = "csharp";
        else if (ext == ".sql") lang = "sql";
        else if (ext == ".py") lang = "python";
        else if (ext == ".js" || ext == ".ts" || ext == ".tsx") lang = "js";
        else if (ext == ".go") lang = "go";
        else if (ext == ".rs") lang = "rust";
        else if (ext == ".java") lang = "java";
        else lang = "generic";

        // Try to fetch indexed symbols from graph
        auto& cfg = core::Config::instance();
        std::vector<graph::GraphNode> symbols;
        try {
            core::Db db(cfg.projectDbPath("."));
            graph::GraphStore store(db);
            auto node = store.getNode(path);
            if (node) symbols = store.childrenOf(node->id);
        } catch (...) { /* DB not available — heuristic-only */ }

        std::ostringstream out;
        out << "# " << fs::path(path).filename().string()
            << "  (" << lines.size() << " lines, " << sz << " B, lang=" << lang << ")\n";

        // First 5 lines (file header / shebang / license)
        out << "[L1-5 header]\n";
        for (size_t i = 0; i < lines.size() && i < 5; ++i) {
            out << "  " << lines[i] << "\n";
        }
        out << "\n";

        // Symbol tree from indexed graph (preferred)
        if (!symbols.empty()) {
            int max_sym = full_syms ? (int)symbols.size() : 30;
            out << "Symbols (" << symbols.size() << "):\n";
            int shown = 0;
            for (auto& s : symbols) {
                if (shown++ >= max_sym) { out << "  ...\n"; break; }
                out << "  [" << s.kind << "] " << s.symbol_name
                    << "  L" << s.line_start << "-" << s.line_end << "\n";
                if (bodies && s.line_start > 0 && s.line_start <= (int)lines.size()) {
                    int start = s.line_start;  // 1-indexed
                    int end = std::min(start + 2, (int)lines.size());
                    for (int i = start; i <= end; ++i) {
                        if (i - 1 < (int)lines.size())
                            out << "    " << lines[i - 1] << "\n";
                    }
                }
            }
            out << "\n";
        } else {
            // Heuristic fallback: regex-grep top-level constructs
            out << "Top-level constructs (heuristic):\n";
            int shown = 0;
            for (size_t i = 0; i < lines.size() && shown < 30; ++i) {
                const auto& l = lines[i];
                std::string trimmed;
                size_t s = 0; while (s < l.size() && (l[s] == ' ' || l[s] == '\t')) ++s;
                trimmed = l.substr(s);
                if (trimmed.empty() || trimmed[0] == '/' || trimmed[0] == '*' || trimmed[0] == '#') {
                    // skip pure-comment lines (but allow #include / #define on cpp)
                    if (lang == "cpp" && (trimmed.rfind("#include", 0) == 0 || trimmed.rfind("#define", 0) == 0)) {
                        // pass through
                    } else continue;
                }
                bool is_decl = false;
                static const std::vector<std::string> kw = {
                    "class ", "interface ", "struct ", "namespace ",
                    "function ", "def ", "fn ", "func ",
                    "public ", "private ", "internal ", "protected ", "static ",
                    "void ", "int ", "string ", "bool ", "auto ", "template",
                    "CREATE PROC", "CREATE FUNCTION", "CREATE OR ALTER",
                    "#include", "import ", "using "
                };
                for (auto& k : kw) {
                    if (trimmed.rfind(k, 0) == 0) { is_decl = true; break; }
                }
                if (is_decl) {
                    out << "  L" << (i + 1) << ": " << trimmed.substr(0, 80) << "\n";
                    ++shown;
                }
            }
            out << "\n";
        }

        // Last 5 lines (often module exports / main)
        if (lines.size() > 5) {
            out << "[L" << (lines.size() - 4) << "-" << lines.size() << " tail]\n";
            for (size_t i = (lines.size() >= 5 ? lines.size() - 5 : 0); i < lines.size(); ++i) {
                out << "  " << lines[i] << "\n";
            }
        }

        // Apply line cap
        std::string full = out.str();
        std::istringstream iss(full);
        std::string outline;
        int kept = 0;
        std::string l2;
        while (std::getline(iss, l2)) {
            if (kept >= max_lines) {
                outline += "... [truncated, --max-lines " + std::to_string(max_lines) + "]\n";
                break;
            }
            outline += l2 + "\n";
            ++kept;
        }

        if (json_out) {
            std::cout << "{\"path\":\"" << path << "\",\"lines\":" << lines.size()
                      << ",\"size\":" << sz << ",\"summary\":\"";
            for (char c : outline) {
                if (c == '"') std::cout << "\\\"";
                else if (c == '\\') std::cout << "\\\\";
                else if (c == '\n') std::cout << "\\n";
                else std::cout << c;
            }
            std::cout << "\"}\n";
        } else {
            std::cout << outline;
        }
        return 0;
    }
};

ICMG_REGISTER_COMMAND("summarize", SummarizeCommand);

} // namespace icmg::cli
