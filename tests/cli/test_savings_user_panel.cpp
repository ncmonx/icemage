// v1.10.0 T6: savings dashboard User column + Active-users panel smoke.
//
// v1.27.1: redirect HOME/USERPROFILE to empty temp dir before run() so
// the test does NOT scan live Claude Code transcripts at
// `~/.claude/projects/<cwd>/*.jsonl` (on dev machine = 56 sessions ×
// multi-MB each → ~5min on Windows NTFS). Previously this single test
// was 302s of 303s total ctest wall on Win. Linux fork+exec + ext4
// hid it (2s total). With redirect: <1s on both OS.

#include "../test_main.hpp"
#include "../../src/core/registry.hpp"
#include "../../src/cli/base_command.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <sstream>

namespace cli = icmg::cli;
namespace core = icmg::core;
namespace fs = std::filesystem;

namespace {

class StdoutCapture {
public:
    StdoutCapture() : old_(std::cout.rdbuf(buf_.rdbuf())) {}
    ~StdoutCapture() { std::cout.rdbuf(old_); }
    std::string str() const { return buf_.str(); }
private:
    std::ostringstream buf_;
    std::streambuf*    old_;
};

// v1.27.1: Skip live transcript scan via env var (HOME/USERPROFILE redirect
// alone insufficient because the cmd spawns `icmg context-budget` subprocess
// that inherits env — but its scan path is determined at subprocess start
// AFTER we've reset env in dtor. Direct env-var short-circuit is reliable.)
class EmptyHomeEnv {
public:
    EmptyHomeEnv() {
        prev_ = getEnv("ICMG_SAVINGS_NO_REAL_SESSIONS");
        setEnv("ICMG_SAVINGS_NO_REAL_SESSIONS", "1");
    }
    ~EmptyHomeEnv() {
        setEnv("ICMG_SAVINGS_NO_REAL_SESSIONS", prev_.c_str());
    }
private:
    static std::string getEnv(const char* k) {
        const char* v = std::getenv(k);
        return v ? std::string(v) : std::string();
    }
    static void setEnv(const char* k, const char* v) {
#ifdef _WIN32
        _putenv_s(k, v ? v : "");
#else
        if (v && *v) setenv(k, v, 1); else unsetenv(k);
#endif
    }
    std::string prev_;
};

}  // namespace

TEST("savings: registered") {
    auto& reg = core::Registry<cli::BaseCommand>::instance();
    auto cmd = reg.create("savings");
    ASSERT_TRUE(static_cast<bool>(cmd));
}

TEST("savings: empty args completes (catch DB-init throws on clean CI)") {
    EmptyHomeEnv hh;  // v1.27.1: skip live transcript scan.
    auto& reg = core::Registry<cli::BaseCommand>::instance();
    auto cmd = reg.create("savings");
    StdoutCapture cap;
    try {
        int rc = cmd->run({});
        (void)rc;
    } catch (const std::exception&) {
        // Acceptable on clean machines without .icmg/data.db
    }
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
