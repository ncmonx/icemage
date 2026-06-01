// v1.68 S2: daemon IPC token auth — server-token helper + RPC token roundtrip.

#include "../test_main.hpp"
#include "../../src/core/server_token.hpp"
#include "../../src/server/rpc_protocol.hpp"

#include <cctype>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;
using namespace icmg::core;
using namespace icmg::server;

// Point HOME/USERPROFILE at a throwaway dir so the token file does not touch
// the real ~/.icmg. icmgGlobalDir() derives from these env vars.
namespace {
struct HomeRedirect {
    std::string old_home, old_profile;
    std::string dir;
    HomeRedirect() {
        const char* h = std::getenv("HOME");
        const char* p = std::getenv("USERPROFILE");
        old_home = h ? h : "";
        old_profile = p ? p : "";
        dir = (fs::temp_directory_path() / ("icmg-tok-" + std::to_string(::time(nullptr)))).string();
        fs::create_directories(dir);
#ifdef _WIN32
        _putenv_s("USERPROFILE", dir.c_str());
#else
        setenv("HOME", dir.c_str(), 1);
#endif
    }
    ~HomeRedirect() {
#ifdef _WIN32
        _putenv_s("USERPROFILE", old_profile.c_str());
#else
        if (old_home.empty()) unsetenv("HOME"); else setenv("HOME", old_home.c_str(), 1);
#endif
        std::error_code ec; fs::remove_all(dir, ec);
    }
};
} // namespace

TEST("server-token: loadOrCreate yields stable 32-hex token") {
    HomeRedirect hr;
    std::string t1 = loadOrCreateServerToken();
    ASSERT_EQ(t1.size(), (size_t)32);
    // hex only
    bool hex = true;
    for (char c : t1) if (!std::isxdigit((unsigned char)c)) hex = false;
    ASSERT_TRUE(hex);
    std::string t2 = loadOrCreateServerToken();   // second call = same file
    ASSERT_EQ(t1, t2);
    ASSERT_EQ(readServerToken(), t1);
}

TEST("server-token: tokenMatches fail-closed") {
    std::string tok = "abc123def456abc123def456abc12300";
    ASSERT_TRUE(tokenMatches(tok, tok));
    ASSERT_FALSE(tokenMatches(tok, "wrong"));
    ASSERT_FALSE(tokenMatches(tok, ""));
    ASSERT_FALSE(tokenMatches("", ""));     // empty expected => fail closed
    ASSERT_FALSE(tokenMatches("", tok));
    // same length, one char off
    std::string almost = tok; almost.back() = (almost.back() == '0') ? '1' : '0';
    ASSERT_FALSE(tokenMatches(tok, almost));
}

TEST("rpc: token survives serialize -> parse roundtrip") {
    RpcRequest req;
    req.cmd = "sayless";
    req.args = {"status"};
    req.token = "deadbeefdeadbeefdeadbeefdeadbeef";
    std::string wire = serializeRequest(req);
    auto back = parseRequest(wire);
    ASSERT_TRUE(back.has_value());
    ASSERT_EQ(back->cmd, std::string("sayless"));
    ASSERT_EQ(back->token, req.token);
}

TEST("rpc: missing token parses as empty (back-compat)") {
    // a wire request without a token field must still parse, token == ""
    std::string wire = "{\"cmd\":\"ping\",\"args\":[]}\n";
    auto back = parseRequest(wire);
    ASSERT_TRUE(back.has_value());
    ASSERT_TRUE(back->token.empty());
}
