// `icmg patch` — apply a unified diff (-p1 format) to files in the working tree.
//
// Parses standard unified diff output (--- / +++ / @@ headers + hunk bodies)
// and applies each hunk with context matching.  Supports single-file diffs
// passed inline and multi-file diffs read from a .patch / .diff file.
//
// Usage:
//   icmg patch <file> --diff <diff_text>   Apply inline diff text to one file
//   icmg patch --file <difffile>           Apply a .patch/.diff file (multi-file)
//   icmg patch --help                      Show usage
//
// Options:
//   --diff <text>      Unified diff text to apply (shell $(...) or quoted)
//   --file <path>      Path to a unified diff file
//   --dry-run          Validate and print what would change; do not write
//   --strip <N>        Strip N leading path components (default: 1, i.e. -p1)
//   --verbose          Print each hunk as it is applied
//
// Exit codes:
//   0  All hunks applied successfully
//   1  Usage / argument error
//   2  One or more hunks failed to apply (context mismatch)
//   3  File I/O error

#include "../base_command.hpp"
#include "../../core/registry.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace icmg::cli {

// ---------------------------------------------------------------------------
// Internal diff data model
// ---------------------------------------------------------------------------

struct DiffLine {
    char kind;       // ' ' context, '+' add, '-' remove
    std::string text; // without the leading kind-char, with newline stripped
};

struct Hunk {
    int old_start = 0;  // 1-based line number in original
    int old_count = 0;
    int new_start = 0;
    int new_count = 0;
    std::vector<DiffLine> lines;
};

struct FileDiff {
    std::string old_path;   // path after strip (from --- line)
    std::string new_path;   // path after strip (from +++ line)
    std::vector<Hunk> hunks;
};

// ---------------------------------------------------------------------------
// Parser helpers
// ---------------------------------------------------------------------------

// Strip N leading path components from a diff path token.
// e.g. strip_prefix("a/src/foo.cpp", 1) -> "src/foo.cpp"
static std::string stripPrefix(const std::string& raw, int n) {
    if (raw == "/dev/null") return raw;
    std::string s = raw;
    // Normalise backslashes (Windows paths in patches).
    std::replace(s.begin(), s.end(), '\\', '/');
    for (int i = 0; i < n; ++i) {
        auto pos = s.find('/');
        if (pos == std::string::npos) return s;  // fewer components than requested
        s = s.substr(pos + 1);
    }
    return s;
}

// Remove leading +, -, or ' ' from a hunk body line.
static std::string lineText(const std::string& raw) {
    if (raw.empty()) return "";
    return raw.substr(1);
}

// Parse "@@ -A,B +C,D @@" header; returns true on success.
static bool parseHunkHeader(const std::string& line,
                            int& old_start, int& old_count,
                            int& new_start, int& new_count) {
    // Format: @@ -A[,B] +C[,D] @@ [optional context text]
    // Use sscanf-style scan.
    // Find the two ranges.
    size_t p = line.find("@@");
    if (p == std::string::npos) return false;
    size_t q = line.find("@@", p + 2);
    if (q == std::string::npos) q = line.size();
    std::string inner = line.substr(p + 2, q - (p + 2));  // " -A,B +C,D "

    // Scan "-A,B"
    size_t dash = inner.find('-');
    if (dash == std::string::npos) return false;
    std::istringstream ss(inner.substr(dash + 1));
    char comma;
    if (!(ss >> old_start)) return false;
    old_count = 1;
    if (ss.peek() == ',') { ss >> comma >> old_count; }

    // Scan "+C,D"
    size_t plus = inner.find('+');
    if (plus == std::string::npos) return false;
    std::istringstream ss2(inner.substr(plus + 1));
    if (!(ss2 >> new_start)) return false;
    new_count = 1;
    if (ss2.peek() == ',') { ss2 >> comma >> new_count; }

    return true;
}

// Parse a unified diff string into a list of FileDiff objects.
static std::vector<FileDiff> parseDiff(const std::string& diff_text, int strip) {
    std::vector<FileDiff> result;
    FileDiff cur;
    Hunk cur_hunk;
    bool in_hunk = false;
    int hunk_old_rem = 0;
    int hunk_new_rem = 0;

    auto flush_hunk = [&]() {
        if (in_hunk) {
            cur.hunks.push_back(cur_hunk);
            cur_hunk = Hunk{};
            in_hunk = false;
        }
    };
    auto flush_file = [&]() {
        flush_hunk();
        if (!cur.old_path.empty() || !cur.new_path.empty()) {
            result.push_back(cur);
            cur = FileDiff{};
        }
    };

    std::istringstream stream(diff_text);
    std::string line;
    while (std::getline(stream, line)) {
        // Strip trailing \r (Windows line endings in patch files).
        if (!line.empty() && line.back() == '\r') line.pop_back();

        if (line.size() >= 4 && line.substr(0, 4) == "--- ") {
            flush_file();
            // Token is everything after "--- " up to optional tab/timestamp.
            std::string tok = line.substr(4);
            auto tab = tok.find('\t');
            if (tab != std::string::npos) tok = tok.substr(0, tab);
            cur.old_path = stripPrefix(tok, strip);
        } else if (line.size() >= 4 && line.substr(0, 4) == "+++ ") {
            std::string tok = line.substr(4);
            auto tab = tok.find('\t');
            if (tab != std::string::npos) tok = tok.substr(0, tab);
            cur.new_path = stripPrefix(tok, strip);
        } else if (line.size() >= 2 && line.substr(0, 2) == "@@") {
            flush_hunk();
            Hunk h;
            if (parseHunkHeader(line, h.old_start, h.old_count, h.new_start, h.new_count)) {
                cur_hunk = h;
                in_hunk = true;
                hunk_old_rem = h.old_count;
                hunk_new_rem = h.new_count;
            }
        } else if (in_hunk) {
            char kind = line.empty() ? ' ' : line[0];
            if (kind == ' ' || kind == '-' || kind == '+') {
                DiffLine dl;
                dl.kind = kind;
                dl.text = lineText(line);
                cur_hunk.lines.push_back(dl);
                if (kind == ' ') { --hunk_old_rem; --hunk_new_rem; }
                else if (kind == '-') { --hunk_old_rem; }
                else if (kind == '+') { --hunk_new_rem; }
                // Allow hunk to end naturally when both counts reach zero.
                if (hunk_old_rem <= 0 && hunk_new_rem <= 0) {
                    flush_hunk();
                }
            } else {
                // Non-diff line while in hunk — close the hunk.
                flush_hunk();
            }
        }
        // diff --, index, etc. are ignored.
    }
    flush_file();
    return result;
}

// ---------------------------------------------------------------------------
// Apply logic
// ---------------------------------------------------------------------------

// Apply a single hunk to `lines` (0-based vector of file lines).
// Returns true on success; on failure `err` describes the mismatch.
static bool applyHunk(std::vector<std::string>& lines, const Hunk& h, std::string& err) {
    // old_start is 1-based.  Find the context window.
    int search_start = h.old_start - 1;  // 0-based

    // Build context+remove pattern from hunk.
    std::vector<std::string> ctx_lines;   // lines that must match (context + remove)
    for (auto& dl : h.lines) {
        if (dl.kind == ' ' || dl.kind == '-') ctx_lines.push_back(dl.text);
    }

    // Try to locate the context block, scanning ±10 lines for fuzz.
    int match_at = -1;
    int fuzz = 10;
    int lo = std::max(0, search_start - fuzz);
    int hi = std::min((int)lines.size(), search_start + fuzz + 1);
    for (int start = lo; start <= hi; ++start) {
        if (start + (int)ctx_lines.size() > (int)lines.size()) break;
        bool ok = true;
        for (int i = 0; i < (int)ctx_lines.size(); ++i) {
            if (lines[start + i] != ctx_lines[i]) { ok = false; break; }
        }
        if (ok) { match_at = start; break; }
    }

    if (match_at < 0) {
        std::ostringstream os;
        os << "hunk @@ -" << h.old_start << "," << h.old_count
           << " ++" << h.new_start << "," << h.new_count
           << " @@ context not found near line " << h.old_start;
        err = os.str();
        return false;
    }

    // Build the replacement: skip removed lines, keep context, insert new.
    std::vector<std::string> replacement;
    int read_pos = match_at;  // position in `lines`
    for (auto& dl : h.lines) {
        if (dl.kind == ' ') {
            replacement.push_back(lines[read_pos++]);
        } else if (dl.kind == '-') {
            ++read_pos;  // consume original line
        } else if (dl.kind == '+') {
            replacement.push_back(dl.text);
        }
    }

    // Replace the consumed region [match_at, read_pos) with `replacement`.
    lines.erase(lines.begin() + match_at, lines.begin() + read_pos);
    lines.insert(lines.begin() + match_at, replacement.begin(), replacement.end());
    return true;
}

// Apply all hunks of a FileDiff to the file on disk.
// Returns 0 on success, 2 on hunk-mismatch, 3 on I/O error.
static int applyFileDiff(const FileDiff& fd, bool dry_run, bool verbose) {
    // Determine which path to use.  If new_path is /dev/null the file should
    // be deleted; if old_path is /dev/null the file is newly created.
    bool is_delete = (fd.new_path == "/dev/null");
    bool is_create = (fd.old_path == "/dev/null");
    std::string path = is_delete ? fd.old_path : fd.new_path;

    // Read existing content.
    std::vector<std::string> lines;
    if (!is_create) {
        std::ifstream f(path);
        if (!f.is_open()) {
            std::cerr << "icmg patch: cannot open '" << path << "': "
                      << std::strerror(errno) << "\n";
            return 3;
        }
        std::string l;
        while (std::getline(f, l)) {
            // Keep original line endings stripped (getline removes \n, we
            // normalise \r away below).
            if (!l.empty() && l.back() == '\r') l.pop_back();
            lines.push_back(l);
        }
    }

    if (is_delete) {
        if (verbose || dry_run)
            std::cout << (dry_run ? "[dry-run] " : "") << "delete " << path << "\n";
        if (!dry_run) {
            std::error_code ec;
            fs::remove(path, ec);
            if (ec) {
                std::cerr << "icmg patch: delete '" << path << "' failed: "
                          << ec.message() << "\n";
                return 3;
            }
        }
        return 0;
    }

    // Apply each hunk sequentially.
    for (auto& h : fd.hunks) {
        if (verbose)
            std::cout << "  applying @@ -" << h.old_start << "," << h.old_count
                      << " +" << h.new_start << "," << h.new_count << " @@\n";
        std::string err;
        if (!applyHunk(lines, h, err)) {
            std::cerr << "icmg patch: " << path << ": " << err << "\n";
            return 2;
        }
    }

    if (dry_run) {
        std::cout << "[dry-run] would write " << path
                  << " (" << lines.size() << " lines)\n";
        return 0;
    }

    // Ensure parent directories exist (for newly created files).
    if (is_create) {
        fs::path pp = fs::path(path).parent_path();
        if (!pp.empty()) {
            std::error_code ec;
            fs::create_directories(pp, ec);
        }
    }

    // Write result.
    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) {
        std::cerr << "icmg patch: cannot write '" << path << "': "
                  << std::strerror(errno) << "\n";
        return 3;
    }
    for (auto& l : lines) out << l << "\n";
    if (verbose) std::cout << "  wrote " << path << "\n";
    return 0;
}

// ---------------------------------------------------------------------------
// Command class
// ---------------------------------------------------------------------------

class PatchCommand : public BaseCommand {
public:
    std::string name()        const override { return "patch"; }
    std::string description() const override {
        return "Apply a unified diff (-p1) to files in the working tree";
    }

    void usage() const override {
        std::cout <<
            "Usage:\n"
            "  icmg patch <file> --diff <diff_text>   Apply inline diff to one file\n"
            "  icmg patch --file <difffile>           Apply a .patch/.diff file\n\n"
            "Options:\n"
            "  --diff <text>    Unified diff text (inline)\n"
            "  --file <path>    Path to unified diff file\n"
            "  --dry-run        Validate; print what would change; do not write\n"
            "  --strip <N>      Strip N leading path components (default: 1, -p1)\n"
            "  --verbose        Print each hunk as it is applied\n\n"
            "Exit codes:\n"
            "  0  success\n"
            "  1  argument error\n"
            "  2  hunk mismatch\n"
            "  3  I/O error\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || hasFlag(args, "--help") || hasFlag(args, "-h")) {
            usage(); return 0;
        }

        bool dry_run = hasFlag(args, "--dry-run");
        bool verbose = hasFlag(args, "--verbose");
        std::string strip_s = flagValue(args, "--strip", "1");
        int strip = 1;
        try { strip = std::stoi(strip_s); } catch (...) {}

        std::string diff_text  = flagValue(args, "--diff");
        std::string diff_file  = flagValue(args, "--file");

        // Determine the target path (first positional arg, if any).
        // When using --file, the paths come from the diff itself.
        std::string target_file;
        for (auto& a : args) {
            if (a.empty() || a[0] == '-') continue;
            // Skip values that follow known flags.
            target_file = a;
            break;
        }

        // --- Load diff text ---
        if (diff_text.empty() && !diff_file.empty()) {
            std::ifstream f(diff_file);
            if (!f.is_open()) {
                std::cerr << "icmg patch: cannot open diff file '" << diff_file
                          << "': " << std::strerror(errno) << "\n";
                return 3;
            }
            std::ostringstream ss;
            ss << f.rdbuf();
            diff_text = ss.str();
        }

        if (diff_text.empty()) {
            std::cerr << "icmg patch: no diff provided. "
                         "Use --diff <text> or --file <difffile>.\n";
            usage();
            return 1;
        }

        // --- Parse ---
        auto file_diffs = parseDiff(diff_text, strip);
        if (file_diffs.empty()) {
            std::cerr << "icmg patch: no valid hunks found in diff.\n";
            return 1;
        }

        // --- If a target_file was specified, override the path in every diff ---
        if (!target_file.empty()) {
            for (auto& fd : file_diffs) {
                if (fd.new_path != "/dev/null") fd.new_path = target_file;
                if (fd.old_path != "/dev/null") fd.old_path = target_file;
            }
        }

        // --- Apply ---
        int rc = 0;
        for (auto& fd : file_diffs) {
            std::string display = (fd.new_path != "/dev/null") ? fd.new_path : fd.old_path;
            if (verbose) std::cout << "patching " << display << "\n";
            int r = applyFileDiff(fd, dry_run, verbose);
            if (r != 0) rc = r;  // keep worst error code
        }

        if (rc == 0) {
            if (!dry_run)
                std::cout << "icmg patch: applied successfully.\n";
            else
                std::cout << "icmg patch: dry-run complete, no files modified.\n";
        }
        return rc;
    }
};

ICMG_REGISTER_COMMAND("patch", PatchCommand);

} // namespace icmg::cli
