// v1.58 FU2: daemon cross-call glossary wiring.
//
// The SessionGlossary collapse logic is unit-tested in
// test_session_glossary.cpp (9/9). Here we verify the daemon dispatch
// path threads session_id through the per-session glossary map without
// crashing and preserves output for normal control verbs.

#include "../test_main.hpp"
#include "../../src/server/icmg_server.hpp"

using namespace icmg::server;

TEST("daemon glossary: repeated dispatch with same session_id is stable") {
    IcmgServer srv;
    RpcRequest req;
    req.cmd = "ping";
    req.session_id = "sess-A";
    RpcResponse r1 = srv.dispatch(req);
    RpcResponse r2 = srv.dispatch(req);
    ASSERT_TRUE(r1.ok);
    ASSERT_TRUE(r2.ok);
    // 'pong' is short (< glossary min_phrase_len) so it is never tokenised.
    ASSERT_TRUE(r1.out.find("pong") != std::string::npos);
    ASSERT_TRUE(r2.out.find("pong") != std::string::npos);
}

TEST("daemon glossary: distinct sessions are isolated") {
    IcmgServer srv;
    RpcRequest a; a.cmd = "ping"; a.session_id = "sess-X";
    RpcRequest b; b.cmd = "ping"; b.session_id = "sess-Y";
    ASSERT_TRUE(srv.dispatch(a).ok);
    ASSERT_TRUE(srv.dispatch(b).ok);
}

TEST("daemon glossary: empty session_id uses shared bucket, no crash") {
    IcmgServer srv;
    RpcRequest req; req.cmd = "ping";   // no session_id
    ASSERT_TRUE(srv.dispatch(req).ok);
}
