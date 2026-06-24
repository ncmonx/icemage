// src/cli/commands/context_vcs_cmd.cpp
// v2.8.4: Git-style context versioning.
//
// Three commands inspired by GCC (Git Context Controller, arXiv:2508.00031)
// which achieved 80%+ on SWE-Bench by treating context like version control:
//
//   icmg context-commit <message>   -- snapshot current context to .icmg/ctx-vcs/
//   icmg context-branch <name>      -- switch/create active branch
//   icmg context-merge <branch>     -- merge another branch's last snapshot here
//
// Storage: .icmg/ctx-vcs/<timestamp>-<branch>.json
// HEAD:    .icmg/ctx-vcs/HEAD.json  -> { "branch": "main" }
//
// Usage:
//   echo "...context..." | icmg context-commit "Fixed auth bug"
//   icmg context-commit "Fixed auth bug" --from session.md
//   icmg context-branch feature/new-cmd
//   icmg context-merge main

#include "../base_command.hpp"
#include "../../core/registry.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace icmg::cli {

namespace ctx_vcs {

const std::string VCS_DIR = ".icmg/ctx-vcs";
const std::string HEAD_FILE = ".icmg/ctx-vcs/HEAD.json";

std::string nowTs() {
    auto t  = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(t);
    char buf[20];
    std::strftime(buf, sizeof(buf), "%Y%m%d%H%M%S", std::gmtime(&tt));
    return buf;
}

std::string activeBranch() {
    std::ifstream f(HEAD_FILE);
    if (!f) return "main";
    std::string line, branch = "main";
    while (std::getline(f, line)) {
        auto p = line.find("\"branch\"");
        if (p != std::string::npos) {
            auto q = line.find('"', p + 9);
            auto r = line.find('"', q + 1);
            if (q != std::string::npos && r != std::string::npos)
                branch = line.substr(q + 1, r - q - 1);
        }
    }
    return branch;
}

void setActiveBranch(const std::string& name) {
    fs::create_directories(VCS_DIR);
    std::ofstream f(HEAD_FILE);
    f << "{ \"branch\": \"" << name << "\" }\n";
}

// Sanitize branch name for use in filename
std::string sanitize(const std::string& s) {
    std::string r;
    for (char c : s)
        r.push_back((c == '/' || c == '\\' || c == ' ') ? '_' : c);
    return r;
}

std::string escapeJson(const std::string& s) {
    std::string r;
    for (char c : s) {
        if (c == '"')  r += "\\\"";
        else if (c == '\\') r += "\\\\";
        else if (c == '\n') r += "\\n";
        else if (c == '\r') r += "\\r";
        else r.push_back(c);
    }
    return r;
}

// Find latest snapshot file for a branch
std::string latestSnapshot(const std::string& branch) {
    std::string san = sanitize(branch);
    std::string latest;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(VCS_DIR, ec)) {
        std::string fn = e.path().filename().string();
        if (fn.find("-" + san + ".json") != std::string::npos) {
            if (fn > latest) latest = fn;
        }
    }
    return latest.empty() ? "" : VCS_DIR + "/" + latest;
}

// Read content field from a snapshot JSON file
std::string readSnapshotContent(const std::string& path) {
    std::ifstream f(path);
    if (!f) return "";
    std::ostringstream ss; ss << f.rdbuf();
    std::string json = ss.str();
    auto p = json.find("\"content\"");
    if (p == std::string::npos) return "";
    auto q = json.find('"', p + 9 + 1); // skip : "
    if (q == std::string::npos) return "";
    // read until unescaped closing "
    std::string content;
    bool esc = false;
    for (size_t i = q + 1; i < json.size(); ++i) {
        char c = json[i];
        if (esc) {
            if (c == 'n') content.push_back('\n');
            else if (c == 'r') content.push_back('\r');
            else if (c == '\\' || c == '"') content.push_back(c);
            esc = false;
        } else if (c == '\\') {
            esc = true;
        } else if (c == '"') {
            break;
        } else {
            content.push_back(c);
        }
    }
    return content;
}

} // namespace ctx_vcs

// ---------------------------------------------------------------------------
// icmg context-commit
// ---------------------------------------------------------------------------
class ContextCommitCmd : public BaseCommand {
public:
    std::string name()        const override { return "context-commit"; }
    std::string description() const override {
        return "Snapshot context to .icmg/ctx-vcs/ (Git-style context versioning)";
    }
    void usage() const override {
        std::cout <<
            "Usage: icmg context-commit <message> [options]\n\n"
            "Snapshots stdin (or --from <file>) to .icmg/ctx-vcs/<ts>-<branch>.json.\n\n"
            "Options:\n"
            "  --from <file>  Read context from file instead of stdin\n"
            "  --branch <b>   Override active branch for this commit\n\n"
            "Example:\n"
            "  icmg context <file> | icmg context-commit \"Read auth module\"\n"
            "  icmg context-commit \"Checkpoint\" --from my-notes.md\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || hasFlag(args, "--help")) { usage(); return 0; }

        std::string message = args[0];
        std::string from    = flagValue(args, "--from", "");
        std::string branch  = flagValue(args, "--branch", "");
        if (branch.empty()) branch = ctx_vcs::activeBranch();

        // Read content
        std::string content;
        if (!from.empty()) {
            std::ifstream f(from);
            if (!f) { std::cerr << "context-commit: cannot open: " << from << "\n"; return 1; }
            std::ostringstream ss; ss << f.rdbuf();
            content = ss.str();
        } else {
            std::ostringstream ss; ss << std::cin.rdbuf();
            content = ss.str();
        }

        if (content.empty()) {
            std::cerr << "context-commit: no content (pipe context or use --from)\n";
            return 1;
        }

        // Write snapshot
        fs::create_directories(ctx_vcs::VCS_DIR);
        std::string ts  = ctx_vcs::nowTs();
        std::string san = ctx_vcs::sanitize(branch);
        std::string path = ctx_vcs::VCS_DIR + "/" + ts + "-" + san + ".json";

        std::ofstream f(path);
        if (!f) { std::cerr << "context-commit: write failed: " << path << "\n"; return 1; }

        f << "{\n"
          << "  \"message\": \"" << ctx_vcs::escapeJson(message) << "\",\n"
          << "  \"branch\": \""  << ctx_vcs::escapeJson(branch)  << "\",\n"
          << "  \"timestamp\": \"" << ts << "\",\n"
          << "  \"content\": \"" << ctx_vcs::escapeJson(content) << "\"\n"
          << "}\n";

        std::cout << "[context-commit] " << ts << " branch=" << branch
                  << " (" << content.size() << " bytes)\n"
                  << "  message: " << message << "\n"
                  << "  file:    " << path << "\n";
        return 0;
    }
};

// ---------------------------------------------------------------------------
// icmg context-branch
// ---------------------------------------------------------------------------
class ContextBranchCmd : public BaseCommand {
public:
    std::string name()        const override { return "context-branch"; }
    std::string description() const override {
        return "Switch/create active context branch (Git-style context versioning)";
    }
    void usage() const override {
        std::cout <<
            "Usage: icmg context-branch [<name>]\n\n"
            "Sets the active branch in .icmg/ctx-vcs/HEAD.json.\n"
            "If no name given, prints current branch.\n\n"
            "Example:\n"
            "  icmg context-branch feature/auth-fix\n"
            "  icmg context-branch           # show current\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (hasFlag(args, "--help")) { usage(); return 0; }

        if (args.empty()) {
            std::cout << "current branch: " << ctx_vcs::activeBranch() << "\n";
            return 0;
        }

        std::string name = args[0];
        ctx_vcs::setActiveBranch(name);
        std::cout << "[context-branch] switched to: " << name << "\n";
        return 0;
    }
};

// ---------------------------------------------------------------------------
// icmg context-merge
// ---------------------------------------------------------------------------
class ContextMergeCmd : public BaseCommand {
public:
    std::string name()        const override { return "context-merge"; }
    std::string description() const override {
        return "Merge another context branch's latest snapshot into current";
    }
    void usage() const override {
        std::cout <<
            "Usage: icmg context-merge <branch>\n\n"
            "Reads the latest snapshot from <branch> and outputs a merged context\n"
            "(current branch snapshot + divider + incoming branch snapshot).\n"
            "Pipe the output to icmg context-commit to save the merge.\n\n"
            "Example:\n"
            "  icmg context-merge main | icmg context-commit \"Merge main into feature\"\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || hasFlag(args, "--help")) { usage(); return 0; }

        std::string src_branch = args[0];
        std::string cur_branch = flagValue(args, "--into", ctx_vcs::activeBranch());

        std::string src_snap = ctx_vcs::latestSnapshot(src_branch);
        std::string cur_snap = ctx_vcs::latestSnapshot(cur_branch);

        if (src_snap.empty()) {
            std::cerr << "context-merge: no snapshot found for branch '" << src_branch << "'\n";
            return 1;
        }

        std::string src_content = ctx_vcs::readSnapshotContent(src_snap);
        std::string cur_content = cur_snap.empty() ? "" : ctx_vcs::readSnapshotContent(cur_snap);

        std::cout << "=== MERGE: " << cur_branch << " + " << src_branch << " ===\n\n";
        if (!cur_content.empty()) {
            std::cout << "--- " << cur_branch << " ---\n" << cur_content << "\n\n";
        }
        std::cout << "--- " << src_branch << " ---\n" << src_content << "\n";

        return 0;
    }
};

ICMG_REGISTER_COMMAND("context-commit", ContextCommitCmd);
ICMG_REGISTER_COMMAND("context-branch", ContextBranchCmd);
ICMG_REGISTER_COMMAND("context-merge",  ContextMergeCmd);

} // namespace icmg::cli
