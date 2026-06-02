// v2.0.0 TE3 (token-efficiency v2): runtime/dynamic dependency edges. Parse stack
// traces / runtime logs into ordered frames, then derive caller->callee runtime
// edges (distinct from static `calls`). Pure + model-free so it is unit-testable.

#include "../test_main.hpp"
#include "../../src/core/trace_parse.hpp"

#include <string>
#include <vector>

using namespace icmg::core;

TEST("trace: python traceback -> ordered frames (file+func+line)") {
    std::string tb =
        "Traceback (most recent call last):\n"
        "  File \"app.py\", line 10, in main\n"
        "    run()\n"
        "  File \"svc.py\", line 42, in run\n"
        "    do_work()\n"
        "  File \"svc.py\", line 88, in do_work\n"
        "ValueError: boom";
    auto f = parseStackFrames(tb);
    ASSERT_EQ((int)f.size(), 3);
    ASSERT_EQ(f[0].file, std::string("app.py"));
    ASSERT_EQ(f[0].func, std::string("main"));
    ASSERT_EQ(f[0].line, 10);
    ASSERT_EQ(f[2].func, std::string("do_work"));
}

TEST("trace: node/js stack -> frames") {
    std::string tb =
        "Error: nope\n"
        "    at handle (/srv/router.js:15:7)\n"
        "    at dispatch (/srv/server.js:200:3)\n";
    auto f = parseStackFrames(tb);
    ASSERT_EQ((int)f.size(), 2);
    ASSERT_EQ(f[0].func, std::string("handle"));
    ASSERT_CONTAINS(f[0].file, std::string("router.js"));
    ASSERT_EQ(f[1].line, 200);
}

TEST("trace: gdb/c++ frames (#n func at file:line)") {
    std::string tb =
        "#0  resolveTarget at graph_store.cpp:120\n"
        "#1  insertEdge at graph_store.cpp:88\n";
    auto f = parseStackFrames(tb);
    ASSERT_EQ((int)f.size(), 2);
    ASSERT_EQ(f[0].func, std::string("resolveTarget"));
    ASSERT_EQ(f[1].line, 88);
}

TEST("trace: non-trace text -> no frames") {
    auto f = parseStackFrames("just a normal log line\nanother line\n");
    ASSERT_EQ((int)f.size(), 0);
}

TEST("runtime edges: consecutive frames -> caller->callee (top calls next)") {
    std::vector<StackFrame> f{
        {"app.py", "main", 10},
        {"svc.py", "run", 42},
        {"svc.py", "do_work", 88},
    };
    auto e = buildRuntimeEdges(f);
    ASSERT_EQ((int)e.size(), 2);
    ASSERT_EQ(e[0].from_file, std::string("app.py"));
    ASSERT_EQ(e[0].to_file, std::string("svc.py"));
    ASSERT_EQ(e[1].from_func, std::string("run"));
    ASSERT_EQ(e[1].to_func, std::string("do_work"));
}

TEST("runtime edges: single/empty frame -> no edges") {
    std::vector<StackFrame> one{{"a.py","f",1}};
    ASSERT_EQ((int)buildRuntimeEdges(one).size(), 0);
    ASSERT_EQ((int)buildRuntimeEdges({}).size(), 0);
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
