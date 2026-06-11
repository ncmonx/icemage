// Ruby + Scala regex symbol-extractor unit tests (2026-06-11).
// Lean (no tree-sitter grammar) — completes symbol extraction for the
// languages claimed in v2.1 that previously had import-edges only.

#include "../test_main.hpp"
#include "../../src/core/registry.hpp"
#include "../../src/graph/symbol_extractor/base_symbol_extractor.hpp"

using icmg::graph::BaseSymbolExtractor;
using icmg::graph::Symbol;
using Reg = icmg::core::Registry<BaseSymbolExtractor>;

// ---- Ruby ------------------------------------------------------------------

TEST("ruby symbol: class with superclass + method + end-range") {
    auto e = Reg::instance().create("ruby");
    ASSERT_TRUE(e != nullptr);
    std::string src =
        "class Order < Base\n"
        "  def process\n"
        "    @id\n"
        "  end\n"
        "end\n";
    auto syms = e->extractSymbols("order.rb", src);
    bool cls = false, meth = false, base_ok = false;
    for (auto& s : syms) {
        if (s.kind == "class" && s.name == "Order") {
            cls = true;
            ASSERT_EQ(s.line_start, 1);
            ASSERT_TRUE(s.line_end >= 5);          // matches the outer `end`
            for (auto& b : s.bases) if (b == "Base") base_ok = true;
        }
        if (s.kind == "method" && s.name == "process") {
            meth = true;
            ASSERT_TRUE(s.line_end >= 4);          // inner `end`
        }
    }
    ASSERT_TRUE(cls);
    ASSERT_TRUE(meth);
    ASSERT_TRUE(base_ok);
}

TEST("ruby symbol: module + self.method") {
    auto e = Reg::instance().create("ruby");
    std::string src =
        "module Util\n"
        "  def self.run(x)\n"
        "    x * 2\n"
        "  end\n"
        "end\n";
    auto syms = e->extractSymbols("util.rb", src);
    bool mod = false, meth = false;
    for (auto& s : syms) {
        if (s.kind == "module" && s.name == "Util") mod = true;
        if (s.kind == "method" && s.name == "run") meth = true;
    }
    ASSERT_TRUE(mod);
    ASSERT_TRUE(meth);
}

// ---- Scala -----------------------------------------------------------------

TEST("scala symbol: class extends with + object + trait + def") {
    auto e = Reg::instance().create("scala");
    ASSERT_TRUE(e != nullptr);
    std::string src =
        "trait Loggable { def log(): Unit }\n"
        "class Service(id: Int) extends Base with Loggable {\n"
        "  def run(): Int = { id }\n"
        "}\n"
        "object Service { val default = 0 }\n";
    auto syms = e->extractSymbols("Service.scala", src);
    bool tr = false, cls = false, obj = false, fn = false, base_ok = false;
    for (auto& s : syms) {
        if (s.kind == "trait" && s.name == "Loggable") tr = true;
        if (s.kind == "class" && s.name == "Service") {
            cls = true;
            for (auto& b : s.bases) if (b == "Base" || b == "Loggable") base_ok = true;
        }
        if (s.kind == "object" && s.name == "Service") obj = true;
        if (s.kind == "method" && s.name == "run") fn = true;
    }
    ASSERT_TRUE(tr);
    ASSERT_TRUE(cls);
    ASSERT_TRUE(obj);
    ASSERT_TRUE(fn);
    ASSERT_TRUE(base_ok);
}

TEST("scala symbol: case class + expression-body def") {
    auto e = Reg::instance().create("scala");
    std::string src =
        "case class Point(x: Int, y: Int)\n"
        "object M {\n"
        "  def double(n: Int) = n * 2\n"
        "}\n";
    auto syms = e->extractSymbols("P.scala", src);
    bool cls = false, fn = false;
    for (auto& s : syms) {
        if (s.kind == "class" && s.name == "Point") cls = true;
        if (s.kind == "method" && s.name == "double") fn = true;
    }
    ASSERT_TRUE(cls);
    ASSERT_TRUE(fn);
}
