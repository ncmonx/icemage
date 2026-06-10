// `icmg find "<intent>"` -- one-shot multi-file intent search. Walks the project
// for source files, ranks them by relevance to the intent, and prints the top
// files each with only their relevant line windows (with line numbers). Collapses
// a Read -> Grep -> Read chain into a single turn.
#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../find_slices.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <string>
#include <vector>
#include <set>

namespace icmg::cli {

namespace {
namespace fs = std::filesystem;

bool isSkipDir(const std::string& name) {
    static const std::set<std::string> skip = {
        ".git", ".svn", ".hg", ".icmg", "node_modules", "third_party", "vendor",
        "dist", "build", "out", "target", ".vs", ".vscode", ".idea",
        "__pycache__", ".next", ".cache", "bin", "obj", "coverage",
        "build-msvc-full", "icmg-build"};
    if (!name.empty() && name[0] == '.' && name != ".") {
        if (skip.count(name)) return true;
    }
    return skip.count(name) > 0;
}

bool isSourceExt(const std::string& ext) {
    // Code-focused: prose docs (.md/.txt) are excluded so keyword-dense
    // markdown can't drown out the actual source for a code intent.
    static const std::set<std::string> ok = {
        ".cpp",".hpp",".h",".hh",".cc",".cxx",".c",".py",".js",".jsx",".ts",
        ".tsx",".go",".java",".rs",".cs",".php",".rb",".kt",".swift",".scala",
        ".lua",".sh",".ps1",".sql",".json",".yaml",".yml",".toml",
        ".html",".css",".vue",".svelte",".cmake"};
    return ok.count(ext) > 0;
}
}  // namespace

class FindCommand : public BaseCommand {
public:
    std::string name()        const override { return "find"; }
    std::string description() const override {
        return "One-shot multi-file intent search -> relevant code slices (fewer turns)";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg find \"<intent>\" [options]\n\n"
            "Ranks project source files by relevance to the intent and prints the\n"
            "top files with only their relevant line windows -- the answer in one\n"
            "turn instead of a Read->Grep->Read chain.\n\n"
            "Options:\n"
            "  --max-files N   Top files to show (default 5)\n"
            "  --ctx N         Context lines around each hit (default 4)\n"
            "  --max-bytes N   Cap total output (default 6000)\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || hasFlag(args, "--help") || hasFlag(args, "-h")) { usage(); return 0; }

        // Join positional words into the intent; skip flags AND their values
        // (so `--max-files 3` does not leak "3" into the query).
        static const std::set<std::string> valFlags = {"--max-files", "--ctx", "--max-bytes"};
        std::string intent;
        for (size_t i = 0; i < args.size(); ++i) {
            const std::string& a = args[i];
            if (a.empty()) continue;
            if (a[0] == '-') { if (valFlags.count(a)) ++i; continue; }  // skip flag (+ its value)
            if (!intent.empty()) intent += " ";
            intent += a;
        }
        if (intent.empty()) { usage(); return 1; }

        int max_files = 5, ctx = 4;
        size_t max_bytes = 6000;
        try { max_files = std::stoi(flagValue(args, "--max-files", "5")); } catch (...) {}
        try { ctx = std::stoi(flagValue(args, "--ctx", "4")); } catch (...) {}
        try { max_bytes = (size_t)std::stoul(flagValue(args, "--max-bytes", "6000")); } catch (...) {}

        // Collect (relpath, body) for project source files; bounded.
        std::vector<std::pair<std::string, std::string>> files;
        const size_t kMaxScan = 4000, kMaxFileBytes = 256 * 1024;
        std::error_code ec;
        fs::path base = fs::current_path(ec);
        size_t scanned = 0;
        auto it = fs::recursive_directory_iterator(
            base, fs::directory_options::skip_permission_denied, ec);
        fs::recursive_directory_iterator end;
        for (; it != end && scanned < kMaxScan; it.increment(ec)) {
            if (ec) { ec.clear(); continue; }
            const fs::path& p = it->path();
            if (it->is_directory(ec)) {
                if (isSkipDir(p.filename().string())) it.disable_recursion_pending();
                continue;
            }
            if (!it->is_regular_file(ec)) continue;
            std::string ext = p.extension().string();
            for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
            if (!isSourceExt(ext)) continue;
            std::uintmax_t sz = it->file_size(ec);
            if (ec || sz == 0 || sz > kMaxFileBytes) { ec.clear(); continue; }
            std::ifstream f(p, std::ios::binary);
            if (!f) continue;
            std::ostringstream ss; ss << f.rdbuf();
            std::string rel = fs::relative(p, base, ec).string();
            if (ec || rel.empty()) { rel = p.string(); ec.clear(); }
            files.emplace_back(rel, ss.str());
            ++scanned;
        }

        auto hits = rankFileSlices(files, intent, ctx, max_files, /*maxWin*/3);
        if (hits.empty()) {
            std::cout << "icmg find: no relevant lines for \"" << intent
                      << "\" (scanned " << scanned << " files)\n";
            return 0;
        }

        std::ostringstream out;
        out << "icmg find \"" << intent << "\" -- " << hits.size()
            << " file(s) (scanned " << scanned << "):\n";
        for (const auto& h : hits) {
            // re-split the body for this file to print numbered windows
            std::string body;
            for (const auto& pr : files) if (pr.first == h.file) { body = pr.second; break; }
            std::vector<std::string> lines;
            { std::istringstream is(body); std::string ln; while (std::getline(is, ln)) lines.push_back(ln); }
            out << "\n=== " << h.file << " (score " << h.score << ") ===\n";
            for (const auto& r : h.ranges) {
                out << "  lines " << r.start << "-" << r.end << ":\n";
                for (int n = r.start; n <= r.end && n <= (int)lines.size(); ++n)
                    out << std::setw(6) << n << "  " << lines[(size_t)n - 1] << "\n";
            }
        }
        std::string s = out.str();
        if (s.size() > max_bytes) {
            s.resize(max_bytes);
            s += "\n--- [output truncated; raise --max-bytes or narrow the intent] ---\n";
        }
        std::cout << s;
        return 0;
    }
};

ICMG_REGISTER_COMMAND("find", FindCommand);

}  // namespace icmg::cli
