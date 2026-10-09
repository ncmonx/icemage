// Game-sidecar pilot: project-scoped daemon pipe names.
//
// `icmg server --pipe <name>` lets a game project (e.g. ice-travian) run its
// own daemon beside the default dev daemon. Precedence:
//   --pipe <name> flag  >  ICMG_SERVER_PIPE env  >  "icmg-server" default.
// resolvePipeName() is a pure arg-parser: it extracts/strips the flag and
// reports the chosen name so server_cmd can pass it to IcmgServer/PipeClient.

#include "../test_main.hpp"
#include "../../src/server/pipe_name.hpp"

#include <cstdlib>

using icmg::server::resolvePipeName;

namespace {
// RAII env guard (Windows _putenv_s / POSIX setenv).
struct EnvGuard {
    const char* key;
    explicit EnvGuard(const char* k) : key(k) {}
    void set(const char* v) {
#ifdef _WIN32
        _putenv_s(key, v);
#else
        setenv(key, v, 1);
#endif
    }
    ~EnvGuard() {
#ifdef _WIN32
        _putenv_s(key, "");
#else
        unsetenv(key);
#endif
    }
};
}  // namespace

TEST("pipe name: default when no flag and no env") {
    EnvGuard env("ICMG_SERVER_PIPE");
    env.set("");
    std::vector<std::string> args = {"start"};
    auto r = resolvePipeName(args);
    ASSERT_EQ(r.name, std::string("icmg-server"));
    ASSERT_EQ(r.args.size(), 1u);
    ASSERT_EQ(r.args[0], std::string("start"));
}

TEST("pipe name: --pipe flag wins and is stripped from args") {
    std::vector<std::string> args = {"start", "--pipe", "icmg-travian"};
    auto r = resolvePipeName(args);
    ASSERT_EQ(r.name, std::string("icmg-travian"));
    ASSERT_EQ(r.args.size(), 1u);
    ASSERT_EQ(r.args[0], std::string("start"));
}

TEST("pipe name: flag position independent (before action)") {
    std::vector<std::string> args = {"--pipe", "icmg-ro", "exec", "ping"};
    auto r = resolvePipeName(args);
    ASSERT_EQ(r.name, std::string("icmg-ro"));
    ASSERT_EQ(r.args.size(), 2u);
    ASSERT_EQ(r.args[0], std::string("exec"));
    ASSERT_EQ(r.args[1], std::string("ping"));
}

TEST("pipe name: env fallback when no flag") {
    EnvGuard env("ICMG_SERVER_PIPE");
    env.set("icmg-from-env");
    std::vector<std::string> args = {"status"};
    auto r = resolvePipeName(args);
    ASSERT_EQ(r.name, std::string("icmg-from-env"));
}

TEST("pipe name: flag beats env") {
    EnvGuard env("ICMG_SERVER_PIPE");
    env.set("icmg-from-env");
    std::vector<std::string> args = {"status", "--pipe", "icmg-flag"};
    auto r = resolvePipeName(args);
    ASSERT_EQ(r.name, std::string("icmg-flag"));
}

TEST("pipe name: dangling --pipe without value keeps default, strips flag") {
    EnvGuard env("ICMG_SERVER_PIPE");
    env.set("");
    std::vector<std::string> args = {"start", "--pipe"};
    auto r = resolvePipeName(args);
    ASSERT_EQ(r.name, std::string("icmg-server"));
    ASSERT_EQ(r.args.size(), 1u);
}

TEST("pipe name: rejects path separators (pipe names are flat)") {
    std::vector<std::string> args = {"start", "--pipe", "..\\evil\\name"};
    auto r = resolvePipeName(args);
    // Invalid chars -> fall back to default rather than constructing a
    // path-like pipe name.
    ASSERT_EQ(r.name, std::string("icmg-server"));
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
