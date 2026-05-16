// icmg bug-report — collect diagnostics + file a GitHub issue.
//
// v1.3.0 design:
//   icmg bug-report                — interactive: prompt for title/body, attach
//                                    auto-diagnostics, open new GH issue via gh CLI.
//   icmg bug-report --auto-capture  — write crash context to a pending JSONL.
//                                    Called by main() on uncaught exception.
//   icmg bug-report --send-pending  — read all pending entries, batch-file each
//                                    as a GH issue (after user confirms).
//   icmg bug-report --list-pending  — show pending count + summary.
//   icmg bug-report --discard-pending — clear pending file.
//   icmg bug-report --dry-run       — print the gh CLI invocation without firing.
//
// Pending log: $ICMG_HOME/.icmg/crash-pending.jsonl (one JSON object per line).
// Required external: `gh` (GitHub CLI). Detected via `gh auth status`. If
// missing, the cmd prints the body so the user can paste manually.
//
// Privacy: all diagnostics are local until the user explicitly confirms a
// send. No telemetry, no auto-submit without consent.

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/path_utils.hpp"
#include "../../core/exec_utils.hpp"

#include <nlohmann/json.hpp>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using nlohmann::json;

namespace icmg::cli {

namespace {

constexpr const char* REPO_OWNER = "ncmonx";
constexpr const char* REPO_NAME  = "icm-graph";

fs::path pendingFilePath() {
    return fs::path(core::icmgGlobalDir()) / "crash-pending.jsonl";
}

std::string readLastNLines(const fs::path& p, int n) {
    if (!fs::exists(p)) return "";
    std::ifstream f(p);
    if (!f) return "";
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(f, line)) lines.push_back(line);
    int start = std::max(0, (int)lines.size() - n);
    std::ostringstream out;
    for (int i = start; i < (int)lines.size(); ++i) out << lines[i] << "\n";
    return out.str();
}

std::string platformTag() {
#if defined(_WIN32)
    return "windows-x64";
#elif defined(__APPLE__)
  #if defined(__aarch64__) || defined(__arm64__)
    return "macos-arm64";
  #else
    return "macos-x64";
  #endif
#else
    return "linux-x64";
#endif
}

// Best-effort version probe — read from self if accessible.
std::string currentVersion() {
    auto r = core::safeExecShell("icmg --version", false, 3000);
    if (r.exit_code == 0 && !r.out.empty()) {
        std::string s = r.out;
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        return s;
    }
    return "icmg <unknown>";
}

// Collect diagnostics (safe: no secrets, no paths outside .icmg).
std::string collectDiagnostics(const std::string& user_message) {
    std::ostringstream body;
    body << "## Environment\n\n"
         << "- **Version:** " << currentVersion() << "\n"
         << "- **Platform:** " << platformTag() << "\n"
         << "- **Reported:** " << std::time(nullptr) << " (unix)\n\n";

    if (!user_message.empty()) {
        body << "## User report\n\n" << user_message << "\n\n";
    }

    fs::path log = fs::path(core::icmgGlobalDir()) / "icmg.log";
    std::string log_tail = readLastNLines(log, 50);
    if (!log_tail.empty()) {
        body << "## Recent log (last 50 lines of ~/.icmg/icmg.log)\n\n"
             << "```\n" << log_tail << "```\n\n";
    }

    fs::path pending = pendingFilePath();
    if (fs::exists(pending)) {
        body << "## Auto-captured crash context\n\n"
             << "```jsonl\n" << readLastNLines(pending, 20) << "```\n\n";
    }

    body << "## How to reproduce\n\n"
         << "_(user, please fill)_\n\n"
         << "## Expected vs actual\n\n"
         << "_(user, please fill)_\n";

    return body.str();
}

// Write a single crash entry (called by main() catch-all on uncaught exception).
// Caller passes the exception's what(), last cmd argv, and any extra context.
int doAutoCapture(const std::vector<std::string>& args) {
    json entry;
    entry["ts"]       = (int64_t)std::time(nullptr);
    entry["version"]  = currentVersion();
    entry["platform"] = platformTag();
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "--cmd"     && i + 1 < args.size()) entry["cmd"]     = args[++i];
        else if (a == "--err"     && i + 1 < args.size()) entry["err"]     = args[++i];
        else if (a == "--context" && i + 1 < args.size()) entry["context"] = args[++i];
    }

    fs::path pf = pendingFilePath();
    std::error_code ec;
    fs::create_directories(pf.parent_path(), ec);
    std::ofstream f(pf, std::ios::app);
    if (!f) {
        std::cerr << "bug-report: cannot write pending log at " << pf.string() << "\n";
        return 1;
    }
    f << entry.dump() << "\n";
    return 0;
}

int doListPending() {
    fs::path pf = pendingFilePath();
    if (!fs::exists(pf)) {
        std::cout << "bug-report: no pending crash reports.\n";
        return 0;
    }
    std::ifstream f(pf);
    int n = 0;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        try {
            auto j = json::parse(line);
            ++n;
            std::cout << "[" << n << "] ts=" << j.value("ts", (int64_t)0)
                      << " cmd=" << j.value("cmd", std::string("?"))
                      << " err=" << j.value("err", std::string("?")).substr(0, 80) << "\n";
        } catch (...) { /* skip malformed */ }
    }
    std::cout << "\nTotal pending: " << n << "\n";
    if (n > 0) {
        std::cout << "Send all: icmg bug-report --send-pending\n"
                     "Discard:  icmg bug-report --discard-pending\n";
    }
    return 0;
}

int doDiscardPending() {
    fs::path pf = pendingFilePath();
    std::error_code ec;
    fs::remove(pf, ec);
    std::cout << "bug-report: pending log cleared.\n";
    return 0;
}

bool ghAvailable() {
    auto r = core::safeExecShell("gh auth status 2>&1", true, 3000);
    return r.exit_code == 0;
}

int fileIssue(const std::string& title, const std::string& body, bool dry_run) {
    std::string repo = std::string(REPO_OWNER) + "/" + REPO_NAME;

    if (dry_run || !ghAvailable()) {
        if (!ghAvailable() && !dry_run) {
            std::cerr << "bug-report: `gh` not available or not authenticated.\n"
                         "Install: https://cli.github.com/  then `gh auth login`.\n"
                         "For now, paste this into a new issue manually:\n\n";
        }
        std::cout << "=== gh issue create dry-run ===\n";
        std::cout << "Repo:  " << repo << "\n";
        std::cout << "Title: " << title << "\n";
        std::cout << "Body:\n" << body << "\n";
        std::cout << "================================\n";
        return 0;
    }

    // Write body to a temp file so we don't pass it through shell escaping.
    fs::path tmp = fs::temp_directory_path() / "icmg-bug-body.md";
    {
        std::ofstream f(tmp);
        f << body;
    }
    std::string cmd = "gh issue create --repo \"" + repo + "\""
                    + " --title \"" + title + "\""
                    + " --body-file \"" + tmp.string() + "\""
                    + " --label \"auto-report\"";
    auto r = core::safeExecShell(cmd, true, 30000);
    fs::remove(tmp);
    if (r.exit_code == 0) {
        std::cout << "bug-report: filed → " << r.out;
        return 0;
    }
    std::cerr << "bug-report: gh issue create failed:\n  " << r.err;
    return r.exit_code;
}

int doSendPending(bool dry_run) {
    fs::path pf = pendingFilePath();
    if (!fs::exists(pf)) {
        std::cout << "bug-report: nothing pending.\n";
        return 0;
    }
    std::ifstream f(pf);
    int sent = 0, failed = 0;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        json j;
        try { j = json::parse(line); } catch (...) { continue; }
        std::string title = "auto-report: "
            + j.value("err", std::string("crash")).substr(0, 60);
        std::ostringstream user_msg;
        user_msg << "Auto-captured icmg failure.\n\n"
                 << "- **cmd:** `" << j.value("cmd", std::string("?")) << "`\n"
                 << "- **err:** `" << j.value("err", std::string("?")) << "`\n"
                 << "- **context:** " << j.value("context", std::string("(none)")) << "\n";
        std::string body = collectDiagnostics(user_msg.str());
        int rc = fileIssue(title, body, dry_run);
        if (rc == 0) ++sent; else ++failed;
    }
    f.close();
    std::cout << "bug-report: sent=" << sent << " failed=" << failed << "\n";
    if (sent > 0 && !dry_run) {
        // Clear pending after successful send.
        std::error_code ec;
        fs::remove(pf, ec);
    }
    return failed > 0 ? 1 : 0;
}

int doInteractive(const std::string& title, bool dry_run) {
    std::string user_msg;
    if (title.empty()) {
        std::cout << "bug-report: open an issue at github.com/"
                  << REPO_OWNER << "/" << REPO_NAME << "\n"
                  << "Pass `--title \"<short summary>\"`. Body is auto-collected.\n";
        return 1;
    }
    std::string body = collectDiagnostics("");
    return fileIssue(title, body, dry_run);
}

} // namespace

class BugReportCommand : public BaseCommand {
public:
    std::string name()        const override { return "bug-report"; }
    std::string description() const override {
        return "Collect diagnostics + file a GitHub issue (manual or batched auto-capture)";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg bug-report <action>\n\n"
            "Actions:\n"
            "  --title \"<text>\"        File one issue now (interactive)\n"
            "  --auto-capture --cmd <c> --err <e> [--context <s>]\n"
            "                          Append crash entry to pending log (no network)\n"
            "  --list-pending          Show pending count + summary\n"
            "  --send-pending          Batch-file each pending entry as a GH issue\n"
            "  --discard-pending       Clear pending log\n"
            "  --dry-run               Print gh CLI call instead of executing it\n\n"
            "Privacy: nothing leaves your machine until --send-pending or --title\n"
            "is run. All diagnostics are read from ~/.icmg/ only.\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || hasFlag(args, "--help") || hasFlag(args, "-h")) {
            usage(); return 0;
        }
        bool dry_run = hasFlag(args, "--dry-run");
        if (hasFlag(args, "--list-pending"))    return doListPending();
        if (hasFlag(args, "--discard-pending")) return doDiscardPending();
        if (hasFlag(args, "--send-pending"))    return doSendPending(dry_run);
        if (hasFlag(args, "--auto-capture"))    return doAutoCapture(args);
        std::string title = flagValue(args, "--title");
        return doInteractive(title, dry_run);
    }
};

ICMG_REGISTER_COMMAND("bug-report", BugReportCommand);

} // namespace icmg::cli
