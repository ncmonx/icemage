#include "../test_main.hpp"
#include "../../src/core/registry.hpp"
#include "../../src/graph/symbol_extractor/base_symbol_extractor.hpp"

using icmg::graph::BaseSymbolExtractor;
using icmg::graph::Symbol;
using Reg = icmg::core::Registry<BaseSymbolExtractor>;

#ifdef ICMG_HAS_TREESITTER_RS

TEST("treesitter-rust: fn + struct + impl method") {
    auto e = Reg::instance().create("rust");
    ASSERT_TRUE(e != nullptr);
    std::string src =
        "pub struct Config {\n"
        "    pub port: u16,\n"
        "}\n"
        "\n"
        "impl Config {\n"
        "    pub fn new(port: u16) -> Self {\n"
        "        Config { port }\n"
        "    }\n"
        "}\n"
        "\n"
        "pub fn run(cfg: Config) {\n"
        "    println!(\"{}\", cfg.port);\n"
        "}\n";
    auto syms = e->extractSymbols("lib.rs", src);

    bool fn_run = false, st_cfg = false, meth_new = false;
    for (auto& s : syms) {
        if (s.kind == "function" && s.name == "run")        fn_run   = true;
        if (s.kind == "struct"   && s.name == "Config")     st_cfg   = true;
        if (s.kind == "method"   && s.name == "Config::new") meth_new = true;
    }
    ASSERT_TRUE(fn_run);
    ASSERT_TRUE(st_cfg);
    ASSERT_TRUE(meth_new);
}

TEST("treesitter-rust: trait + enum") {
    auto e = Reg::instance().create("rust");
    ASSERT_TRUE(e != nullptr);
    std::string src =
        "pub trait Handler {\n"
        "    fn handle(&self) -> String;\n"
        "}\n"
        "\n"
        "pub enum Status {\n"
        "    Ok,\n"
        "    Err,\n"
        "}\n";
    auto syms = e->extractSymbols("types.rs", src);

    bool tr = false, en = false;
    for (auto& s : syms) {
        if (s.kind == "trait" && s.name == "Handler") tr = true;
        if (s.kind == "enum"  && s.name == "Status")  en = true;
    }
    ASSERT_TRUE(tr);
    ASSERT_TRUE(en);
}

TEST("treesitter-rust: body_hash set") {
    auto e = Reg::instance().create("rust");
    ASSERT_TRUE(e != nullptr);
    std::string src = "fn foo() { let x = 1; }\n";
    auto syms = e->extractSymbols("x.rs", src);
    bool ok = false;
    for (auto& s : syms)
        if (s.name == "foo" && !s.body_hash.empty()) ok = true;
    ASSERT_TRUE(ok);
}

TEST("treesitter-rust: empty + garbage no crash") {
    auto e = Reg::instance().create("rust");
    ASSERT_TRUE(e != nullptr);
    auto a = e->extractSymbols("x.rs", "");
    ASSERT_EQ((int)a.size(), 0);
    auto b = e->extractSymbols("x.rs", "@@@ not rust @@@");
    (void)b;
}

#else

TEST("treesitter-rust: build skipped") { ASSERT_TRUE(true); }

#endif

int main() {
    std::cout << "=== tree-sitter Rust tests ===\n";
    return icmg::test::run_all();
}
