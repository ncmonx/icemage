// v1.10.0 T2: hookio command tests.
//
// `icmg hookio` parses stdin JSON (.tool_input.command etc.) and emits
// canonical envelope JSON. Replaces `jq` in bundled hook scripts.

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

// RAII stdin/stdout redirector.
class IoRedirect {
public:
    IoRedirect(const std::string& stdin_data)
      : in_stream_(stdin_data),
        old_cin_(std::cin.rdbuf(in_stream_.rdbuf())),
        old_cout_(std::cout.rdbuf(out_stream_.rdbuf())) {}
    ~IoRedirect() {
        std::cin.rdbuf(old_cin_);
        std::cout.rdbuf(old_cout_);
    }
    std::string captured() const { return out_stream_.str(); }
private:
    std::istringstream in_stream_;
    std::ostringstream out_stream_;
    std::streambuf*    old_cin_;
    std::streambuf*    old_cout_;
};

std::unique_ptr<cli::BaseCommand> makeHookio() {
    auto& reg = core::Registry<cli::BaseCommand>::instance();
    return reg.create("hookio");
}

}  // namespace

TEST("hookio: registered in command registry") {
    auto cmd = makeHookio();
    ASSERT_TRUE(static_cast<bool>(cmd));
    ASSERT_EQ(cmd->name(), std::string("hookio"));
}

TEST("hookio get: extract top-level string") {
    auto cmd = makeHookio();
    IoRedirect io(R"({"tool_name":"Bash","other":"x"})");
    int rc = cmd->run({"get", "tool_name"});
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(io.captured(), std::string("Bash"));
}

TEST("hookio get: extract nested dotted path") {
    auto cmd = makeHookio();
    IoRedirect io(R"({"tool_input":{"command":"ls -la","extra":1}})");
    int rc = cmd->run({"get", "tool_input.command"});
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(io.captured(), std::string("ls -la"));
}

TEST("hookio get: missing key returns empty") {
    auto cmd = makeHookio();
    IoRedirect io(R"({"tool_name":"Bash"})");
    int rc = cmd->run({"get", "tool_input.command"});
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(io.captured(), std::string(""));
}

TEST("hookio get: leading dot in path is stripped") {
    auto cmd = makeHookio();
    IoRedirect io(R"({"prompt":"hello"})");
    int rc = cmd->run({"get", ".prompt"});
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(io.captured(), std::string("hello"));
}

TEST("hookio get: unescapes JSON escape sequences") {
    auto cmd = makeHookio();
    IoRedirect io(R"({"msg":"line1\nline2\ttab\"quote"})");
    int rc = cmd->run({"get", "msg"});
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(io.captured(), std::string("line1\nline2\ttab\"quote"));
}

TEST("hookio get: numeric value stringified") {
    auto cmd = makeHookio();
    IoRedirect io(R"({"total_tokens":12345})");
    int rc = cmd->run({"get", "total_tokens"});
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(io.captured(), std::string("12345"));
}

TEST("hookio get: null value yields empty") {
    auto cmd = makeHookio();
    IoRedirect io(R"({"command":null,"foo":"bar"})");
    int rc = cmd->run({"get", "command"});
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(io.captured(), std::string(""));
}

TEST("hookio emit: --ctx envelope shape") {
    auto cmd = makeHookio();
    IoRedirect io("");
    int rc = cmd->run({"emit", "PostToolUse", "--ctx", "hello"});
    ASSERT_EQ(rc, 0);
    std::string out = io.captured();
    ASSERT_CONTAINS(out, "\"hookEventName\":\"PostToolUse\"");
    ASSERT_CONTAINS(out, "\"additionalContext\":\"hello\"");
    ASSERT_CONTAINS(out, "\"hookSpecificOutput\":");
}

TEST("hookio emit: --deny PreToolUse envelope") {
    auto cmd = makeHookio();
    IoRedirect io("");
    int rc = cmd->run({"emit", "PreToolUse", "--deny", "blocked by rule X"});
    ASSERT_EQ(rc, 0);
    std::string out = io.captured();
    ASSERT_CONTAINS(out, "\"hookEventName\":\"PreToolUse\"");
    ASSERT_CONTAINS(out, "\"permissionDecision\":\"deny\"");
    ASSERT_CONTAINS(out, "\"permissionDecisionReason\":\"blocked by rule X\"");
}

TEST("hookio emit: --ctx escapes newlines + quotes") {
    auto cmd = makeHookio();
    IoRedirect io("");
    int rc = cmd->run({"emit", "Stop", "--ctx", "line1\nline2\"quote"});
    ASSERT_EQ(rc, 0);
    std::string out = io.captured();
    ASSERT_CONTAINS(out, "\\n");
    ASSERT_CONTAINS(out, "\\\"");
}

TEST("hookio emit: --ctx-stdin reads message from stdin") {
    auto cmd = makeHookio();
    IoRedirect io("from-stdin-ctx");
    int rc = cmd->run({"emit", "SessionStart", "--ctx-stdin"});
    ASSERT_EQ(rc, 0);
    ASSERT_CONTAINS(io.captured(), "\"additionalContext\":\"from-stdin-ctx\"");
}

TEST("hookio escape: JSON-quotes raw string") {
    auto cmd = makeHookio();
    IoRedirect io("hi \"world\"\n");
    int rc = cmd->run({"escape"});
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(io.captured(), std::string("\"hi \\\"world\\\"\\n\""));
}

TEST("hookio: unknown action returns 1") {
    auto cmd = makeHookio();
    IoRedirect io("");
    int rc = cmd->run({"this-is-not-an-action"});
    ASSERT_EQ(rc, 1);
}

TEST("hookio: empty args prints usage, returns 0") {
    auto cmd = makeHookio();
    IoRedirect io("");
    int rc = cmd->run({});
    ASSERT_EQ(rc, 0);
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
