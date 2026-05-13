#include "../test_main.hpp"
#include "../../src/core/registry.hpp"
#include "../../src/graph/symbol_extractor/base_symbol_extractor.hpp"

using icmg::graph::BaseSymbolExtractor;
using icmg::graph::Symbol;
using Reg = icmg::core::Registry<BaseSymbolExtractor>;

#ifdef ICMG_HAS_TREESITTER_GO

TEST("treesitter-go: function + struct + method") {
    auto e = Reg::instance().create("go");
    ASSERT_TRUE(e != nullptr);
    std::string src =
        "package main\n"
        "\n"
        "type Server struct {\n"
        "    port int\n"
        "}\n"
        "\n"
        "func NewServer(port int) *Server {\n"
        "    return &Server{port: port}\n"
        "}\n"
        "\n"
        "func (s *Server) Start() {\n"
        "}\n";
    auto syms = e->extractSymbols("main.go", src);

    bool fn_new = false, st_server = false, meth_start = false;
    for (auto& s : syms) {
        if (s.kind == "function" && s.name == "NewServer") fn_new    = true;
        if (s.kind == "struct"   && s.name == "Server")    st_server = true;
        if (s.kind == "method"   && s.name == "Start")     meth_start = true;
    }
    ASSERT_TRUE(fn_new);
    ASSERT_TRUE(st_server);
    ASSERT_TRUE(meth_start);
}

TEST("treesitter-go: interface + type alias") {
    auto e = Reg::instance().create("go");
    ASSERT_TRUE(e != nullptr);
    std::string src =
        "package main\n"
        "\n"
        "type Handler interface {\n"
        "    ServeHTTP() string\n"
        "}\n"
        "\n"
        "type MyInt int\n";
    auto syms = e->extractSymbols("iface.go", src);

    bool iface = false, alias = false;
    for (auto& s : syms) {
        if (s.kind == "interface" && s.name == "Handler") iface = true;
        if (s.name == "MyInt")                            alias = true;
    }
    ASSERT_TRUE(iface);
    ASSERT_TRUE(alias);
}

TEST("treesitter-go: line numbers") {
    auto e = Reg::instance().create("go");
    ASSERT_TRUE(e != nullptr);
    std::string src =
        "package main\n"           // line 1
        "\n"                       // line 2
        "func Foo() {}\n";         // line 3
    auto syms = e->extractSymbols("x.go", src);
    bool ok = false;
    for (auto& s : syms)
        if (s.name == "Foo" && s.line_start == 3) ok = true;
    ASSERT_TRUE(ok);
}

TEST("treesitter-go: empty + garbage no crash") {
    auto e = Reg::instance().create("go");
    ASSERT_TRUE(e != nullptr);
    auto a = e->extractSymbols("x.go", "");
    ASSERT_EQ((int)a.size(), 0);
    auto b = e->extractSymbols("x.go", "@@@ not go @@@");
    (void)b;
}

#else

TEST("treesitter-go: build skipped") { ASSERT_TRUE(true); }

#endif

int main() {
    std::cout << "=== tree-sitter Go tests ===\n";
    return icmg::test::run_all();
}
