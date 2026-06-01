#include "../test_main.hpp"
#include "../../src/core/registry.hpp"
#include "../../src/graph/symbol_extractor/base_symbol_extractor.hpp"

using icmg::graph::BaseSymbolExtractor;
using icmg::graph::Symbol;
using Reg = icmg::core::Registry<BaseSymbolExtractor>;

#ifdef ICMG_HAS_TREESITTER_TS

TEST("treesitter-ts: function + class + interface + type + enum") {
    auto e = Reg::instance().create("ast-ts");
    ASSERT_TRUE(e != nullptr);
    std::string src =
        "interface User { id: number; name: string; }\n"
        "type Point = { x: number; y: number; };\n"
        "enum Color { RED, GREEN, BLUE }\n"
        "class UserService {\n"
        "    addUser(u: User) { return u.id; }\n"
        "    findById(id: number) { return null; }\n"
        "}\n"
        "function greet(name: string) { return 'hello ' + name; }\n"
        "export function exported() { greet('x'); }\n";
    auto syms = e->extractSymbols("a.ts", src);

    bool fn=false, cls=false, ifc=false, ty=false, en=false, mt=false, ex=false;
    for (auto& s : syms) {
        if (s.kind == "function" && s.name == "greet") fn = true;
        if (s.kind == "class"    && s.name == "UserService") cls = true;
        if (s.kind == "interface" && s.name == "User") ifc = true;
        if (s.kind == "type"     && s.name == "Point") ty = true;
        if (s.kind == "enum"     && s.name == "Color") en = true;
        if (s.kind == "method"   && s.name == "UserService.addUser") mt = true;
        if (s.kind == "function" && s.name == "exported") ex = true;
    }
    ASSERT_TRUE(fn); ASSERT_TRUE(cls); ASSERT_TRUE(ifc);
    ASSERT_TRUE(ty); ASSERT_TRUE(en); ASSERT_TRUE(mt); ASSERT_TRUE(ex);
}

TEST("treesitter-ts: empty + garbage no crash") {
    auto e = Reg::instance().create("ast-ts");
    ASSERT_TRUE(e != nullptr);
    auto a = e->extractSymbols("x.ts", "");
    ASSERT_EQ((int)a.size(), 0);
    auto b = e->extractSymbols("x.ts", "@@@ not ts @@@");
    (void)b;
}

#else

TEST("treesitter-ts: build skipped") { ASSERT_TRUE(true); }

#endif


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
