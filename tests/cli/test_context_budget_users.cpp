// v1.10.0 T5: context-budget multi-user flag smoke test.
//
// Hard to unit-test enumUserHomes() without mocking C:\Users\ tree. This
// suite covers: cmd registration, flag acceptance, JSON-output shape on the
// current process's transcript dir. Deeper enum semantics verified manually.

#include "../test_main.hpp"
#include "../../src/core/registry.hpp"
#include "../../src/cli/base_command.hpp"

#include <iostream>
#include <memory>
#include <sstream>
#include <string>

namespace cli = icmg::cli;
namespace core = icmg::core;

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

class StderrCapture {
public:
    StderrCapture() : old_(std::cerr.rdbuf(buf_.rdbuf())) {}
    ~StderrCapture() { std::cerr.rdbuf(old_); }
    std::string str() const { return buf_.str(); }
private:
    std::ostringstream buf_;
    std::streambuf*    old_;
};

}  // namespace

TEST("context-budget: registered") {
    auto& reg = core::Registry<cli::BaseCommand>::instance();
    auto cmd = reg.create("context-budget");
    ASSERT_TRUE(static_cast<bool>(cmd));
}

TEST("context-budget --help mentions --all-users") {
    auto& reg = core::Registry<cli::BaseCommand>::instance();
    auto cmd = reg.create("context-budget");
    StdoutCapture cap;
    int rc = cmd->run({"--help"});
    ASSERT_EQ(rc, 0);
    ASSERT_CONTAINS(cap.str(), "--all-users");
}

TEST("context-budget --all-users --json accepts flag (smoke)") {
    auto& reg = core::Registry<cli::BaseCommand>::instance();
    auto cmd = reg.create("context-budget");
    StdoutCapture cap;
    StderrCapture err;
    int rc = cmd->run({"--all-sessions", "--all-users", "--json"});
    // rc may be 0 (found transcripts) or 1 (no transcripts) — both OK.
    // Critical: no throw, no crash.
    (void)rc;
}

TEST("context-budget --all-sessions --json emits user_count field") {
    auto& reg = core::Registry<cli::BaseCommand>::instance();
    auto cmd = reg.create("context-budget");
    StdoutCapture cap;
    int rc = cmd->run({"--all-sessions", "--json"});
    // If transcripts present (normal case), expect user_count + users[].
    // If absent (CI clean machine), rc=1 + no JSON — skip assert.
    if (rc == 0 && !cap.str().empty()) {
        ASSERT_CONTAINS(cap.str(), "\"user_count\"");
        ASSERT_CONTAINS(cap.str(), "\"users\"");
    }
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
