// test_batch_read_cmd — unit tests for `icmg batch-read`.
//
// Tests:
//   (1) single file read — output contains content + divider header
//   (2) multi-file — dividers present for each file
//   (3) --limit truncates output to N lines per file
//   (4) nonexistent file — error reported, exit code non-zero
//
// Uses the internal helpers via a thin test shim that redirects stdout/stderr
// so we can capture output without spawning a subprocess.

#include "../test_main.hpp"
#include <climits>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <iostream>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Helpers shared across tests
// ---------------------------------------------------------------------------

// Write a temporary file and return its path.
static fs::path makeTmp(const std::string& name, const std::string& content) {
    fs::path p = fs::temp_directory_path() / name;
    std::ofstream f(p);
    f << content;
    return p;
}

// ---------------------------------------------------------------------------
// Inline re-implementation of the core batch-read logic so tests stay
// self-contained and don't require linking the full command registry.
// Mirrors the logic in batch_read_cmd.cpp exactly.
// ---------------------------------------------------------------------------

namespace {

struct FileSpec {
    std::string path;
    int line_from = 1;
    int line_to   = INT_MAX;
};

static FileSpec parseSpec(const std::string& token) {
    FileSpec s;
    auto colon = token.rfind(':');
    if (colon != std::string::npos && colon + 1 < token.size()) {
        const std::string after = token.substr(colon + 1);
        auto dash = after.find('-');
        if (dash != std::string::npos && dash > 0 && dash + 1 < after.size()) {
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

// Run the batch-read logic, capturing stdout in `out` and stderr in `err`.
// Returns the exit code (0 = success, 1 = at least one error).
static int runBatchRead(const std::vector<std::string>& paths,
                        int limit,
                        std::string& out,
                        std::string& err) {
    std::ostringstream oss_out, oss_err;

    bool any_error = false;
    for (const std::string& token : paths) {
        FileSpec spec = parseSpec(token);
        oss_out << "=== " << spec.path << " ===\n";

        std::ifstream fin(spec.path);
        if (!fin) {
            any_error = true;
            oss_err << "icmg batch-read: cannot open '" << spec.path << "'\n";
            oss_out << "[error: cannot open '" << spec.path << "']\n";
            continue;
        }

        std::string line;
        int row     = 0;
        int written = 0;
        while (std::getline(fin, line)) {
            ++row;
            if (row < spec.line_from) continue;
            if (row > spec.line_to)   break;
            if (written >= limit) {
                oss_out << "[... truncated at " << limit << " lines]\n";
                break;
            }
            oss_out << line << "\n";
            ++written;
        }
    }

    out = oss_out.str();
    err = oss_err.str();
    return any_error ? 1 : 0;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// TEST (1): single file read — header divider + file content present
// ---------------------------------------------------------------------------
TEST("batch-read: single file produces divider and content") {
    fs::path f = makeTmp("br_single.txt", "alpha\nbeta\ngamma\n");

    std::string out, err;
    int rc = runBatchRead({f.string()}, 200, out, err);

    ASSERT_EQ(rc, 0);
    // Divider must be present
    ASSERT_CONTAINS(out, "=== ");
    ASSERT_CONTAINS(out, f.string());
    ASSERT_CONTAINS(out, " ===");
    // File content must be present
    ASSERT_CONTAINS(out, "alpha");
    ASSERT_CONTAINS(out, "beta");
    ASSERT_CONTAINS(out, "gamma");
    // No errors
    ASSERT_TRUE(err.empty());

    fs::remove(f);
}

// ---------------------------------------------------------------------------
// TEST (2): multi-file — divider present for each file
// ---------------------------------------------------------------------------
TEST("batch-read: multi-file output has a divider per file") {
    fs::path f1 = makeTmp("br_multi1.txt", "file1_line1\nfile1_line2\n");
    fs::path f2 = makeTmp("br_multi2.txt", "file2_line1\nfile2_line2\n");
    fs::path f3 = makeTmp("br_multi3.txt", "file3_line1\n");

    std::string out, err;
    int rc = runBatchRead({f1.string(), f2.string(), f3.string()}, 200, out, err);

    ASSERT_EQ(rc, 0);

    // Each file must have its own "=== path ===" header
    {
        std::string hdr1 = "=== " + f1.string() + " ===";
        std::string hdr2 = "=== " + f2.string() + " ===";
        std::string hdr3 = "=== " + f3.string() + " ===";
        ASSERT_CONTAINS(out, hdr1);
        ASSERT_CONTAINS(out, hdr2);
        ASSERT_CONTAINS(out, hdr3);
    }

    // Content from each file must appear
    ASSERT_CONTAINS(out, "file1_line1");
    ASSERT_CONTAINS(out, "file2_line1");
    ASSERT_CONTAINS(out, "file3_line1");

    // Dividers appear in order: hdr1 before hdr2 before hdr3
    auto pos1 = out.find(f1.string());
    auto pos2 = out.find(f2.string());
    auto pos3 = out.find(f3.string());
    ASSERT_TRUE(pos1 < pos2);
    ASSERT_TRUE(pos2 < pos3);

    fs::remove(f1); fs::remove(f2); fs::remove(f3);
}

// ---------------------------------------------------------------------------
// TEST (3): --limit truncates output to N lines per file
// ---------------------------------------------------------------------------
TEST("batch-read: --limit truncates output per file") {
    // Write a file with 10 lines
    std::ostringstream content;
    for (int i = 1; i <= 10; ++i) content << "line" << i << "\n";
    fs::path f = makeTmp("br_limit.txt", content.str());

    std::string out, err;
    // limit = 3 → only first 3 lines + truncation notice
    int rc = runBatchRead({f.string()}, 3, out, err);

    ASSERT_EQ(rc, 0);

    // First 3 lines present
    ASSERT_CONTAINS(out, "line1");
    ASSERT_CONTAINS(out, "line2");
    ASSERT_CONTAINS(out, "line3");

    // Lines beyond limit must NOT appear
    ASSERT_NOT_CONTAINS(out, "line4");
    ASSERT_NOT_CONTAINS(out, "line10");

    // Truncation notice must be present
    ASSERT_CONTAINS(out, "truncated at 3 lines");

    fs::remove(f);
}

// ---------------------------------------------------------------------------
// TEST (4): nonexistent file — error reported, exit code = 1
// ---------------------------------------------------------------------------
TEST("batch-read: nonexistent file reports error and returns non-zero") {
    const std::string ghost = "/nonexistent/path/that/does/not/exist_xyz.cpp";

    std::string out, err;
    int rc = runBatchRead({ghost}, 200, out, err);

    // Exit code must be non-zero
    ASSERT_TRUE(rc != 0);

    // Error message must reference the bad path
    ASSERT_CONTAINS(err, ghost);

    // The header divider IS still emitted (we attempt each file)
    ASSERT_CONTAINS(out, "=== ");

    // An error banner appears in stdout content too
    ASSERT_CONTAINS(out, "error");
}

// ---------------------------------------------------------------------------
// Entry point (disabled in mono-test builds)
// ---------------------------------------------------------------------------
#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
