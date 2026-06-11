// Kotlin + Swift regex symbol-extractor unit tests (2026-06-11).
// Lean (no tree-sitter grammar) — mirrors the csharp extractor approach.

#include "../test_main.hpp"
#include "../../src/core/registry.hpp"
#include "../../src/graph/symbol_extractor/base_symbol_extractor.hpp"

using icmg::graph::BaseSymbolExtractor;
using icmg::graph::Symbol;
using Reg = icmg::core::Registry<BaseSymbolExtractor>;

// ---- Kotlin ----------------------------------------------------------------

TEST("kotlin symbol: class with body line range + base") {
    auto e = Reg::instance().create("kotlin");
    ASSERT_TRUE(e != nullptr);
    std::string src =
        "package app\n"
        "class Order(val id: Int) : Base(), Loggable {\n"
        "    fun process(): Int { return id }\n"
        "}\n";
    auto syms = e->extractSymbols("Order.kt", src);
    bool cls = false, fn = false, base_ok = false;
    for (auto& s : syms) {
        if (s.kind == "class" && s.name == "Order") {
            cls = true;
            ASSERT_EQ(s.line_start, 2);
            ASSERT_TRUE(s.line_end >= 4);
            for (auto& b : s.bases) if (b == "Base" || b == "Loggable") base_ok = true;
        }
        if (s.kind == "function" && s.name == "process") fn = true;
    }
    ASSERT_TRUE(cls);
    ASSERT_TRUE(fn);
    ASSERT_TRUE(base_ok);
}

TEST("kotlin symbol: interface + object + enum class") {
    auto e = Reg::instance().create("kotlin");
    std::string src =
        "interface Repo { fun all(): List<String> }\n"
        "object Singleton { val x = 1 }\n"
        "enum class Color { RED, GREEN }\n";
    auto syms = e->extractSymbols("X.kt", src);
    bool iface = false, obj = false, en = false;
    for (auto& s : syms) {
        if (s.kind == "interface" && s.name == "Repo")  iface = true;
        if (s.kind == "object"    && s.name == "Singleton") obj = true;
        if (s.kind == "enum"      && s.name == "Color") en = true;
    }
    ASSERT_TRUE(iface);
    ASSERT_TRUE(obj);
    ASSERT_TRUE(en);
}

TEST("kotlin symbol: bodyless class + expression-body fun") {
    auto e = Reg::instance().create("kotlin");
    std::string src =
        "data class Point(val x: Int, val y: Int)\n"
        "fun double(n: Int) = n * 2\n";
    auto syms = e->extractSymbols("P.kt", src);
    bool cls = false, fn = false;
    for (auto& s : syms) {
        if (s.kind == "class" && s.name == "Point") cls = true;
        if (s.kind == "function" && s.name == "double") fn = true;
    }
    ASSERT_TRUE(cls);
    ASSERT_TRUE(fn);
}

// ---- Swift -----------------------------------------------------------------

TEST("swift symbol: struct + class with base + func") {
    auto e = Reg::instance().create("swift");
    ASSERT_TRUE(e != nullptr);
    std::string src =
        "struct Point { var x: Int }\n"
        "class Service: NSObject, Loggable {\n"
        "    func run() -> Int { return 0 }\n"
        "}\n";
    auto syms = e->extractSymbols("S.swift", src);
    bool st = false, cls = false, fn = false, base_ok = false;
    for (auto& s : syms) {
        if (s.kind == "struct" && s.name == "Point") st = true;
        if (s.kind == "class" && s.name == "Service") {
            cls = true;
            for (auto& b : s.bases) if (b == "NSObject" || b == "Loggable") base_ok = true;
        }
        if (s.kind == "function" && s.name == "run") fn = true;
    }
    ASSERT_TRUE(st);
    ASSERT_TRUE(cls);
    ASSERT_TRUE(fn);
    ASSERT_TRUE(base_ok);
}

TEST("swift symbol: protocol + enum + extension + init") {
    auto e = Reg::instance().create("swift");
    std::string src =
        "protocol Drawable { func draw() }\n"
        "enum Dir { case up, down }\n"
        "extension String { func shout() -> String { return self } }\n"
        "class V { init(x: Int) { } }\n";
    auto syms = e->extractSymbols("X.swift", src);
    bool proto = false, en = false, ext = false, ini = false;
    for (auto& s : syms) {
        if (s.kind == "protocol" && s.name == "Drawable") proto = true;
        if (s.kind == "enum" && s.name == "Dir") en = true;
        if (s.kind == "extension" && s.name == "String") ext = true;
        if (s.kind == "function" && s.name == "init") ini = true;
    }
    ASSERT_TRUE(proto);
    ASSERT_TRUE(en);
    ASSERT_TRUE(ext);
    ASSERT_TRUE(ini);
}
