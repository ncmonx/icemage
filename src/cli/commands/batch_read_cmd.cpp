// `icmg batch-read` — read multiple files in one call with divider headers.
//
// Outputs each file prefixed by a "=== <path> ===" divider, making it easy
// to pipe several files into a single LLM context window without running
// separate `icmg read` invocations.
//
// Usage:
//   icmg batch-read <file1> [<file2> ...] [--limit N]
//   icmg batch-read file.cpp:10-50 [--limit N]
//
// Line-range syntax:  path:A-B  (e.g. src/main.cpp:1-30)
// --limit N           Max lines per file (default 200). Applied AFTER any
//                     line-range slice, so limit acts as a safety cap.

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include <climits>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace icmg::cli {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Parse "path:A-B" → (path, A, B). If no range, A=1 B=INT_MAX.
struct FileSpec {
    std::string path;
    int line_from = 1;
    int line_to   = INT_MAX;
};

static FileSpec parseSpec(const std::string& token) {
    FileSpec s;
    // Find the LAST ':' that is followed by "digits-digits" to avoid mangling
    // Windows absolute paths (C:\...) which also contain ':'.
    auto colon = token.rfind(':');
    if (colon != std::string::npos && colon + 1 < token.size()) {
        const std::string after = token.substr(colon + 1);
        auto dash = after.find('-');
        if (dash != std::string::npos && dash > 0 && dash + 1 < after.size()) {
            // Verify both sides are all digits
            auto allDigits = [](const std::string& t) {
                for (char c : t) if (c < '0' || c > '9') return false;
                return !t.empty();
            };
            std::string a = after.substr(0, dash);
            std::string b = after.substr(dash + 1);
            if (allDigits(a) && allDigits(b)) {
                s.path      = token.substr(0, colon);
                s.line_from = std::stoi(a);
                s.line_to   = std::stoi(b);
                return s;
            }
        }
    }
    s.path = token;
    return s;
}

// Read up to `limit` lines from `spec`, return them as a string.
// On error writes to stderr and returns an error banner.
static std::string readSpec(const FileSpec& spec, int limit,
                            bool& had_error) {
    std::ifstream fin(spec.path);
    if (!fin) {
        had_error = true;
        std::ostringstream err;
        err << "[error: cannot open '" << spec.path << "']\n";
        return err.str();
    }

    std::ostringstream out;
    std::string line;
    int row      = 0;   // 1-based line number
    int written  = 0;

    while (std::getline(fin, line)) {
        ++row;
        if (row < spec.line_from) continue;
        if (row > spec.line_to)   break;
        if (written >= limit) {
            out << "[... truncated at " << limit << " lines]\n";
            break;
        }
        out << line << "\n";
        ++written;
    }
    return out.str();
}

// ---------------------------------------------------------------------------
// Command
// ---------------------------------------------------------------------------

class BatchReadCommand : public BaseCommand {
public:
    std::string name()        const override { return "batch-read"; }
    std::string description() const override {
        return "Read multiple files in one call, each preceded by a '=== <path> ===' divider";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg batch-read <file1> [<file2> ...] [options]\n\n"
            "Reads each file and prints it to stdout with a divider header:\n"
            "  === <path> ===\n\n"
            "Line-range syntax (per file):\n"
            "  src/main.cpp:10-50    Read only lines 10-50 from that file\n\n"
            "Options:\n"
            "  --limit N             Max lines per file (default 200)\n"
            "  --help, -h            Show this help\n\n"
            "Examples:\n"
            "  icmg batch-read src/foo.cpp tests/test_foo.cpp\n"
            "  icmg batch-read src/main.cpp:1-40 src/util.cpp --limit 100\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (hasFlag(args, "--help") || hasFlag(args, "-h") || args.empty()) {
            usage();
            return 0;
        }

        // Parse --limit
        int limit = 200;
        {
            std::string lv = flagValue(args, "--limit");
            if (!lv.empty()) {
                try   { limit = std::stoi(lv); }
                catch (...) { limit = 200; }
                if (limit < 1) limit = 1;
            }
        }

        // Collect positional args (skip flags and their values)
        std::vector<FileSpec> specs;
        for (size_t i = 0; i < args.size(); ++i) {
            const std::string& a = args[i];
            if (a == "--limit") { ++i; continue; }       // skip flag + value
            if (a.substr(0, 2) == "--") continue;        // skip other flags
            specs.push_back(parseSpec(a));
        }

        if (specs.empty()) {
            std::cerr << "icmg batch-read: no file paths provided\n";
            return 1;
        }

        bool any_error = false;
        for (const FileSpec& spec : specs) {
            std::cout << "=== " << spec.path << " ===\n";
            bool had_error = false;
            std::string content = readSpec(spec, limit, had_error);
            if (had_error) {
                any_error = true;
                std::cerr << "icmg batch-read: " << content;  // error banner to stderr too
            }
            std::cout << content;
        }

        return any_error ? 1 : 0;
    }
};

ICMG_REGISTER_COMMAND("batch-read", BatchReadCommand);

} // namespace icmg::cli
