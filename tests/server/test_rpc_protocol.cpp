// v1.56 T3: icmg-server RPC protocol tests.
//
// Protocol is newline-delimited JSON. Request:
//   {"cmd":"context","args":["src/foo.cpp"],"session_id":"abc"}
// Response:
//   {"ok":true,"out":"...","exit":0}
//   {"ok":false,"err":"reason"}

#include "../test_main.hpp"
#include "../../src/server/rpc_protocol.hpp"

using namespace icmg::server;

TEST("rpc: serialize request round-trips through parse") {
    RpcRequest req;
    req.cmd = "context";
    req.args = {"src/foo.cpp", "--lines", "1-20"};
    req.session_id = "sess-123";
    std::string wire = serializeRequest(req);

    auto parsed = parseRequest(wire);
    ASSERT_TRUE(parsed.has_value());
    ASSERT_EQ(parsed->cmd, std::string("context"));
    ASSERT_EQ(parsed->args.size(), 3u);
    ASSERT_EQ(parsed->args[0], std::string("src/foo.cpp"));
    ASSERT_EQ(parsed->args[2], std::string("1-20"));
    ASSERT_EQ(parsed->session_id, std::string("sess-123"));
}

TEST("rpc: request with no args parses") {
    RpcRequest req;
    req.cmd = "status";
    std::string wire = serializeRequest(req);
    auto parsed = parseRequest(wire);
    ASSERT_TRUE(parsed.has_value());
    ASSERT_EQ(parsed->cmd, std::string("status"));
    ASSERT_TRUE(parsed->args.empty());
}

TEST("rpc: args with special chars (quotes, spaces) survive round-trip") {
    RpcRequest req;
    req.cmd = "run";
    req.args = {"grep", "-r", "hello \"world\"", "path with spaces/x.cpp"};
    std::string wire = serializeRequest(req);
    auto parsed = parseRequest(wire);
    ASSERT_TRUE(parsed.has_value());
    ASSERT_EQ(parsed->args.size(), 4u);
    ASSERT_EQ(parsed->args[2], std::string("hello \"world\""));
    ASSERT_EQ(parsed->args[3], std::string("path with spaces/x.cpp"));
}

TEST("rpc: parse rejects malformed JSON") {
    ASSERT_FALSE(parseRequest("not json").has_value());
    ASSERT_FALSE(parseRequest("{bad").has_value());
    ASSERT_FALSE(parseRequest("").has_value());
}

TEST("rpc: parse rejects request with no cmd field") {
    ASSERT_FALSE(parseRequest("{\"args\":[]}").has_value());
}

TEST("rpc: serialize response ok round-trips") {
    RpcResponse res;
    res.ok = true;
    res.out = "line1\nline2\n";
    res.exit_code = 0;
    std::string wire = serializeResponse(res);
    auto parsed = parseResponse(wire);
    ASSERT_TRUE(parsed.has_value());
    ASSERT_TRUE(parsed->ok);
    ASSERT_EQ(parsed->out, std::string("line1\nline2\n"));
    ASSERT_EQ(parsed->exit_code, 0);
}

TEST("rpc: serialize response error round-trips") {
    RpcResponse res;
    res.ok = false;
    res.err = "unknown command 'frobnicate'";
    std::string wire = serializeResponse(res);
    auto parsed = parseResponse(wire);
    ASSERT_TRUE(parsed.has_value());
    ASSERT_FALSE(parsed->ok);
    ASSERT_EQ(parsed->err, std::string("unknown command 'frobnicate'"));
}

TEST("rpc: response with newlines + quotes in out survives") {
    RpcResponse res;
    res.ok = true;
    res.out = "{\"nested\":\"json\"}\nsecond line\twith tab\n";
    std::string wire = serializeResponse(res);
    auto parsed = parseResponse(wire);
    ASSERT_TRUE(parsed.has_value());
    ASSERT_EQ(parsed->out, res.out);
}

TEST("rpc: wire format is single line (newline-delimited framing)") {
    RpcRequest req;
    req.cmd = "run";
    req.args = {"echo", "multi\nline\nvalue"};
    std::string wire = serializeRequest(req);
    // The framing newline is only the trailing one; embedded newlines in
    // values must be escaped so the message stays single-line.
    auto first_nl = wire.find('\n');
    ASSERT_TRUE(first_nl == wire.size() - 1 || first_nl == std::string::npos);
}
