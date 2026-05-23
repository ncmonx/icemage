// tests/graph/test_csharp_symbols.cpp
// Phase 18: C# symbol extractor unit tests.
#include "../test_main.hpp"
#include "../../src/core/registry.hpp"
#include "../../src/graph/symbol_extractor/base_symbol_extractor.hpp"

using icmg::graph::BaseSymbolExtractor;
using icmg::graph::Symbol;
using Reg = icmg::core::Registry<BaseSymbolExtractor>;

TEST("csharp symbol: extracts class with line range") {
    auto e = Reg::instance().create("csharp");
    ASSERT_TRUE(e != nullptr);
    std::string src =
        "namespace App {\n"
        "    public class Order {\n"
        "        public int Id;\n"
        "        public void Process() { return; }\n"
        "    }\n"
        "}\n";
    auto syms = e->extractSymbols("Order.cs", src);

    bool found_class = false, found_method = false;
    for (auto& s : syms) {
        if (s.kind == "class" && s.name == "Order") {
            found_class = true;
            ASSERT_EQ(s.line_start, 2);
            ASSERT_TRUE(s.line_end >= 5);
        }
        if (s.kind == "method" && s.name == "Process") found_method = true;
    }
    ASSERT_TRUE(found_class);
    ASSERT_TRUE(found_method);
}

TEST("csharp symbol: extends/implements captured") {
    auto e = Reg::instance().create("csharp");
    std::string src =
        "namespace X {\n"
        "    public class Service : BaseService, IService, ILogger {\n"
        "    }\n"
        "}\n";
    auto syms = e->extractSymbols("Service.cs", src);
    bool ok = false;
    for (auto& s : syms) {
        if (s.kind == "class" && s.name == "Service") {
            ASSERT_EQ((int)s.bases.size(), 3);
            // Order may differ; just verify presence
            bool has_base = false, has_isvc = false, has_ilog = false;
            for (auto& b : s.bases) {
                if (b == "BaseService") has_base = true;
                if (b == "IService")    has_isvc = true;
                if (b == "ILogger")     has_ilog = true;
            }
            ASSERT_TRUE(has_base && has_isvc && has_ilog);
            ok = true;
        }
    }
    ASSERT_TRUE(ok);
}

TEST("csharp symbol: method calls captured (no self-call)") {
    auto e = Reg::instance().create("csharp");
    std::string src =
        "class C {\n"
        "    public void Init() {\n"
        "        Validate();\n"
        "        Logger.Log(\"hi\");\n"
        "        if (true) { Init(); }\n"   // self-call ignored
        "    }\n"
        "}\n";
    auto syms = e->extractSymbols("C.cs", src);
    bool ok = false;
    for (auto& s : syms) {
        if (s.kind == "method" && s.name == "Init") {
            bool has_validate = false, has_log = false, has_self = false;
            for (auto& c : s.calls) {
                if (c == "Validate") has_validate = true;
                if (c == "Log")      has_log = true;
                if (c == "Init")     has_self = true;
            }
            ASSERT_TRUE(has_validate);
            ASSERT_TRUE(has_log);
            ASSERT_FALSE(has_self);
            ok = true;
        }
    }
    ASSERT_TRUE(ok);
}

TEST("csharp symbol: body_hash differs when body changes") {
    auto e = Reg::instance().create("csharp");
    std::string s1 = "class C { void M() { return; } }";
    std::string s2 = "class C { void M() { Process(); } }";
    auto sym1 = e->extractSymbols("C.cs", s1);
    auto sym2 = e->extractSymbols("C.cs", s2);
    std::string h1, h2;
    for (auto& s : sym1) if (s.name == "M") h1 = s.body_hash;
    for (auto& s : sym2) if (s.name == "M") h2 = s.body_hash;
    ASSERT_TRUE(!h1.empty());
    ASSERT_TRUE(!h2.empty());
    ASSERT_TRUE(h1 != h2);
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
