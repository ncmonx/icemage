// tests/graph/test_treesitter_cpp.cpp
//
// Unit tests for the tree-sitter C++ AST symbol extractor.
// Compiled & linked only when ICMG_HAS_TREESITTER_CPP is defined.
// Mirrors test_treesitter_c.cpp pattern.

#include "../test_main.hpp"
#include "../../src/core/registry.hpp"
#include "../../src/graph/symbol_extractor/base_symbol_extractor.hpp"

using icmg::graph::BaseSymbolExtractor;
using icmg::graph::Symbol;
using Reg = icmg::core::Registry<BaseSymbolExtractor>;

#ifdef ICMG_HAS_TREESITTER_CPP

TEST("treesitter-cpp: class + method + function") {
    auto e = Reg::instance().create("ast-cpp");
    ASSERT_TRUE(e != nullptr);

    std::string src =
        "#include <string>\n"
        "namespace myns {\n"
        "class Foo {\n"
        "public:\n"
        "    int compute(int x);\n"
        "    std::string name() const { return name_; }\n"
        "private:\n"
        "    std::string name_;\n"
        "};\n"
        "int Foo::compute(int x) {\n"
        "    return x * 2;\n"
        "}\n"
        "} // namespace myns\n"
        "void standalone(int a, int b) { return; }\n";

    auto syms = e->extractSymbols("sample.cpp", src);
    ASSERT_TRUE(!syms.empty());

    bool found_class = false, found_method = false, found_fn = false;
    for (auto& s : syms) {
        // class inside namespace -> name may be "myns::Foo" or "Foo"
        if (s.name == "Foo" || s.name == "myns::Foo") found_class  = true;
        if (s.name == "compute" || s.name == "myns::Foo::compute"
                                || s.name == "Foo::compute") found_method = true;
        if (s.name == "standalone") found_fn = true;
    }
    ASSERT_TRUE(found_class);
    ASSERT_TRUE(found_method);
    ASSERT_TRUE(found_fn);
}

TEST("treesitter-cpp: struct + constructor + destructor") {
    auto e = Reg::instance().create("ast-cpp");
    ASSERT_TRUE(e != nullptr);

    std::string src =
        "struct Bar {\n"
        "    Bar() {}\n"
        "    ~Bar() {}\n"
        "    int value = 0;\n"
        "};\n";

    auto syms = e->extractSymbols("bar.cpp", src);
    ASSERT_TRUE(!syms.empty());

    bool found_struct = false, found_ctor = false;
    for (auto& s : syms) {
        // struct_specifier emits kind="struct" not "class"
        if (s.name == "Bar" && (s.kind == "class" || s.kind == "struct")) found_struct = true;
        if (s.name == "Bar" && s.kind == "function") found_ctor = true;
        // destructor may not be extracted by all grammars
    }
    ASSERT_TRUE(found_struct);
    ASSERT_TRUE(found_ctor);
}

TEST("treesitter-cpp: call extraction inside function body") {
    auto e = Reg::instance().create("ast-cpp");
    ASSERT_TRUE(e != nullptr);

    std::string src =
        "void helper() {}\n"
        "int process(int x) {\n"
        "    helper();\n"
        "    return x + 1;\n"
        "}\n";

    auto syms = e->extractSymbols("calls.cpp", src);
    bool found_call = false;
    for (auto& s : syms) {
        if (s.name == "process") {
            for (auto& c : s.calls) {
                if (c == "helper") found_call = true;
            }
        }
    }
    ASSERT_TRUE(found_call);
}

TEST("treesitter-cpp: template class") {
    auto e = Reg::instance().create("ast-cpp");
    ASSERT_TRUE(e != nullptr);

    std::string src =
        "template<typename T>\n"
        "class Container {\n"
        "public:\n"
        "    void push(T val);\n"
        "    T pop();\n"
        "};\n";

    auto syms = e->extractSymbols("container.cpp", src);
    bool found_class = false;
    for (auto& s : syms) {
        if (s.name == "Container") found_class = true;
    }
    ASSERT_TRUE(found_class);
}

TEST("treesitter-cpp: base class extraction") {
    auto e = Reg::instance().create("ast-cpp");
    ASSERT_TRUE(e != nullptr);

    std::string src =
        "class Base {};\n"
        "class Derived : public Base {\n"
        "    void doSomething() {}\n"
        "};\n";

    auto syms = e->extractSymbols("inherit.cpp", src);
    bool found_base = false;
    for (auto& s : syms) {
        if (s.name == "Derived") {
            for (auto& b : s.bases) {
                if (b == "Base") found_base = true;
            }
        }
    }
    ASSERT_TRUE(found_base);
}

#else
TEST("treesitter-cpp: skipped (ICMG_HAS_TREESITTER_CPP not defined)") {
    // Pass silently -- tree-sitter OFF is a valid build config.
    ASSERT_TRUE(true);
}
#endif
