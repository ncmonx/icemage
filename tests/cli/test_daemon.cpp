// Phase 81 T7 — daemon IPC unit tests.
// Tests JSON-RPC parse/dispatch logic + IPC path conventions.
// Internal functions are static, so we duplicate minimal logic inline
// (same pattern as test_review.cpp).
#include "../test_main.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <sstream>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#else
#  include <sys/socket.h>
#  include <sys/un.h>
#  include <unistd.h>
#  include <cstring>
#endif

using nlohmann::json;

// ── Inline helpers mirroring daemon_cmd.cpp ───────────────────────────────────

static std::string testIpcPath() {
#ifdef _WIN32
    // Use a test-only pipe name that will never collide with the running daemon.
    return R"(\\.\pipe\icmg-daemon-test-noserver-99)";
#else
    return "/tmp/icmg-test-daemon-not-running.sock";
#endif
}

// Minimal JSON-RPC dispatcher (mirrors dispatchRequest logic)
static std::string testDispatch(const std::string& raw) {
    try {
        auto req = json::parse(raw);
        int id = req.value("id", 0);
        std::string method = req.value("method", std::string(""));
        auto params = req.value("params", json::object());

        json resp;
        resp["id"] = id;

        if (method == "ping") {
            resp["result"] = "pong";
        } else if (method == "SHUTDOWN") {
            resp["result"] = "shutdown";
        } else if (method == "recall") {
            std::string query = params.value("query", std::string(""));
            if (query.empty())
                resp["error"] = "missing param: query";
            else
                resp["result"] = "";  // simplified: no icmg exec in unit test
        } else if (method == "hook.userprompt") {
            std::string prompt = params.value("prompt", std::string(""));
            if (prompt.empty())
                resp["error"] = "missing param: prompt";
            else
                resp["result"] = "";  // simplified
        } else {
            resp["error"] = "unknown method: " + method;
        }
        return resp.dump();
    } catch (...) {
        return R"({"id":0,"error":"parse error"})";
    }
}

// Attempt IPC connect to a non-running daemon — must return empty, not crash.
static std::string tryClientSendNoServer(const std::string& req_json) {
#ifdef _WIN32
    HANDLE pipe = CreateFileA(testIpcPath().c_str(),
                              GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                              OPEN_EXISTING, 0, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) return "";
    CloseHandle(pipe);
    return "unexpected-open";
#else
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return "";
    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, testIpcPath().c_str(), sizeof(addr.sun_path) - 1);
    if (::connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        ::close(fd);
        return "";
    }
    ::close(fd);
    return "unexpected-open";
#endif
}

// ── IPC path format tests ─────────────────────────────────────────────────────

TEST("daemon: ipc path has expected platform prefix") {
    std::string path = testIpcPath();
#ifdef _WIN32
    ASSERT_TRUE(path.rfind(R"(\\.\pipe\)", 0) == 0);
    ASSERT_CONTAINS(path, "icmg");
#else
    ASSERT_CONTAINS(path, "/");
#endif
}

TEST("daemon: ipc path is non-empty") {
    ASSERT_TRUE(!testIpcPath().empty());
}

// ── JSON-RPC protocol tests ───────────────────────────────────────────────────

TEST("daemon: ping returns pong") {
    std::string raw = R"({"id":1,"method":"ping","params":{}})";
    std::string resp_str = testDispatch(raw);
    auto resp = json::parse(resp_str);
    ASSERT_EQ(resp.value("id", -1), 1);
    ASSERT_EQ(resp.value("result", std::string("")), std::string("pong"));
}

TEST("daemon: id echoed back in response") {
    for (int id : {0, 1, 42, 999}) {
        json req;
        req["id"] = id;
        req["method"] = "ping";
        req["params"] = json::object();
        auto resp = json::parse(testDispatch(req.dump()));
        ASSERT_EQ(resp.value("id", -1), id);
    }
}

TEST("daemon: unknown method returns error field") {
    std::string raw = R"({"id":2,"method":"nonexistent","params":{}})";
    auto resp = json::parse(testDispatch(raw));
    ASSERT_TRUE(resp.contains("error"));
    ASSERT_NOT_CONTAINS(resp.dump(), "\"result\"");
}

TEST("daemon: SHUTDOWN method returns shutdown result") {
    std::string raw = R"({"id":3,"method":"SHUTDOWN","params":{}})";
    auto resp = json::parse(testDispatch(raw));
    ASSERT_EQ(resp.value("result", std::string("")), std::string("shutdown"));
}

TEST("daemon: recall missing query returns error") {
    std::string raw = R"({"id":4,"method":"recall","params":{}})";
    auto resp = json::parse(testDispatch(raw));
    ASSERT_TRUE(resp.contains("error"));
    ASSERT_CONTAINS(resp["error"].get<std::string>(), "query");
}

TEST("daemon: hook.userprompt missing prompt returns error") {
    std::string raw = R"({"id":5,"method":"hook.userprompt","params":{}})";
    auto resp = json::parse(testDispatch(raw));
    ASSERT_TRUE(resp.contains("error"));
    ASSERT_CONTAINS(resp["error"].get<std::string>(), "prompt");
}

TEST("daemon: malformed JSON returns parse error") {
    std::string raw = "not-json{{{{";
    auto resp = json::parse(testDispatch(raw));
    ASSERT_TRUE(resp.contains("error"));
}

TEST("daemon: empty body returns parse error") {
    auto resp = json::parse(testDispatch(""));
    ASSERT_TRUE(resp.contains("error"));
}

TEST("daemon: recall with query field succeeds (no error)") {
    std::string raw = R"({"id":6,"method":"recall","params":{"query":"test query"}})";
    auto resp = json::parse(testDispatch(raw));
    ASSERT_FALSE(resp.contains("error"));
    ASSERT_TRUE(resp.contains("result"));
}

// ── Client fallback: connect to non-running daemon ───────────────────────────

TEST("daemon: client connect to non-running daemon returns empty (no crash)") {
    // Daemon is not running on the test IPC path — must return "" gracefully.
    std::string result = tryClientSendNoServer(R"({"id":1,"method":"ping","params":{}})");
    ASSERT_EQ(result, std::string(""));
}

// ── JSON-RPC request builder ──────────────────────────────────────────────────

TEST("daemon: well-formed request has id/method/params") {
    json req;
    req["id"] = 1;
    req["method"] = "ping";
    req["params"] = json::object();
    std::string s = req.dump();
    ASSERT_CONTAINS(s, "\"id\"");
    ASSERT_CONTAINS(s, "\"method\"");
    ASSERT_CONTAINS(s, "\"params\"");
}

TEST("daemon: params k=v parse: key=value split on first '='") {
    auto parseParam = [](const std::string& kv) -> std::pair<std::string,std::string> {
        auto pos = kv.find('=');
        if (pos == std::string::npos) return {kv, ""};
        return {kv.substr(0, pos), kv.substr(pos + 1)};
    };
    auto [k1, v1] = parseParam("prompt=hello world");
    ASSERT_EQ(k1, std::string("prompt"));
    ASSERT_EQ(v1, std::string("hello world"));

    auto [k2, v2] = parseParam("query=a=b=c");
    ASSERT_EQ(k2, std::string("query"));
    ASSERT_EQ(v2, std::string("a=b=c"));

    auto [k3, v3] = parseParam("novalue");
    ASSERT_EQ(k3, std::string("novalue"));
    ASSERT_EQ(v3, std::string(""));
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
