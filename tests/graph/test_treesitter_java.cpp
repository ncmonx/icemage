#include "../test_main.hpp"
#include "../../src/core/registry.hpp"
#include "../../src/graph/symbol_extractor/base_symbol_extractor.hpp"

using icmg::graph::BaseSymbolExtractor;
using icmg::graph::Symbol;
using Reg = icmg::core::Registry<BaseSymbolExtractor>;

#ifdef ICMG_HAS_TREESITTER_JV

TEST("treesitter-java: class + method + constructor") {
    auto e = Reg::instance().create("java");
    ASSERT_TRUE(e != nullptr);
    std::string src =
        "public class Server {\n"
        "    private int port;\n"
        "\n"
        "    public Server(int port) {\n"
        "        this.port = port;\n"
        "    }\n"
        "\n"
        "    public void start() {\n"
        "        System.out.println(port);\n"
        "    }\n"
        "}\n";
    auto syms = e->extractSymbols("Server.java", src);

    bool cls = false, ctor = false, meth = false;
    for (auto& s : syms) {
        if (s.kind == "class"       && s.name == "Server")        cls  = true;
        if (s.kind == "constructor" && s.name == "Server.Server")  ctor = true;
        if (s.kind == "method"      && s.name == "Server.start")   meth = true;
    }
    ASSERT_TRUE(cls);
    ASSERT_TRUE(ctor);
    ASSERT_TRUE(meth);
}

TEST("treesitter-java: interface + enum") {
    auto e = Reg::instance().create("java");
    ASSERT_TRUE(e != nullptr);
    std::string src =
        "public interface Handler {\n"
        "    String handle(String req);\n"
        "}\n"
        "\n"
        "public enum Status {\n"
        "    OK, ERROR\n"
        "}\n";
    auto syms = e->extractSymbols("types.java", src);

    bool iface = false, en = false;
    for (auto& s : syms) {
        if (s.kind == "interface" && s.name == "Handler") iface = true;
        if (s.kind == "enum"      && s.name == "Status")  en    = true;
    }
    ASSERT_TRUE(iface);
    ASSERT_TRUE(en);
}

TEST("treesitter-java: package-wrapped class") {
    auto e = Reg::instance().create("java");
    ASSERT_TRUE(e != nullptr);
    std::string src =
        "package com.example;\n"
        "\n"
        "public class App {\n"
        "    public static void main(String[] args) {}\n"
        "}\n";
    auto syms = e->extractSymbols("App.java", src);
    bool cls = false, meth = false;
    for (auto& s : syms) {
        if (s.kind == "class"  && s.name == "App")       cls  = true;
        if (s.kind == "method" && s.name == "App.main")   meth = true;
    }
    ASSERT_TRUE(cls);
    ASSERT_TRUE(meth);
}

TEST("treesitter-java: empty + garbage no crash") {
    auto e = Reg::instance().create("java");
    ASSERT_TRUE(e != nullptr);
    auto a = e->extractSymbols("x.java", "");
    ASSERT_EQ((int)a.size(), 0);
    auto b = e->extractSymbols("x.java", "@@@ not java @@@");
    (void)b;
}

#else

TEST("treesitter-java: build skipped") { ASSERT_TRUE(true); }

#endif

int main() {
    std::cout << "=== tree-sitter Java tests ===\n";
    return icmg::test::run_all();
}
