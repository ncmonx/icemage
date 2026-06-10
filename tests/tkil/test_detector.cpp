#include "../test_main.hpp"
#include "../../src/tkil/detector.hpp"
#include "../../src/core/cmd_densify.hpp"
#include "../../src/core/openssl_rng.hpp"
#include "../../src/core/hook_sanitize.hpp"
#include "../../src/graph/extractor/ruby_extract.hpp"
#include "../../src/graph/extractor/swift_extract.hpp"
#include "../../src/graph/extractor/kotlin_extract.hpp"
#include "../../src/graph/extractor/scala_extract.hpp"
#include "../../src/graph/extractor/lua_extract.hpp"
#include "../../src/graph/extractor/dart_extract.hpp"
#include <cstring>
#include <algorithm>

using icmg::tkil::CmdType;
using icmg::tkil::Detector;
using icmg::core::densifyCommand;

// ---- Detector unit tests ---------------------------------------------------

TEST("detector: git log → GitLog") {
    Detector d;
    ASSERT_EQ(d.detect("git log --oneline"), CmdType::GitLog);
}

TEST("detector: git diff → GitLog") {
    Detector d;
    ASSERT_EQ(d.detect("git diff HEAD~1"), CmdType::GitLog);
}

TEST("detector: git show → GitLog") {
    Detector d;
    ASSERT_EQ(d.detect("git show abc123"), CmdType::GitLog);
}

TEST("detector: git status → GitLog") {
    Detector d;
    ASSERT_EQ(d.detect("git status"), CmdType::GitLog);
}

TEST("detector: cargo build → Rust") {  // v1.21.3 (F3): dedicated lang filter
    Detector d;
    ASSERT_EQ(d.detect("cargo build --release"), CmdType::Rust);
}

TEST("detector: cmake build → Build") {
    Detector d;
    ASSERT_EQ(d.detect("cmake --build ."), CmdType::Build);
}

TEST("detector: make → Build") {
    Detector d;
    ASSERT_EQ(d.detect("make -j8"), CmdType::Build);
}

TEST("detector: cargo test → Rust") {  // v1.21.3 (F3): dedicated lang filter
    Detector d;
    ASSERT_EQ(d.detect("cargo test"), CmdType::Rust);
}

TEST("detector: go build → Go") {  // v1.21.3 (F3)
    Detector d;
    ASSERT_EQ(d.detect("go build ./..."), CmdType::Go);
}

TEST("detector: mvn package → Java") {  // v1.21.3 (F3)
    Detector d;
    ASSERT_EQ(d.detect("mvn package -DskipTests"), CmdType::Java);
}

TEST("detector: gradlew build → Java") {  // v1.21.3 (F3)
    Detector d;
    ASSERT_EQ(d.detect("./gradlew build"), CmdType::Java);
}

TEST("detector: dotnet build → Dotnet") {  // v1.21.3 (F3)
    Detector d;
    ASSERT_EQ(d.detect("dotnet build -c Release"), CmdType::Dotnet);
}

TEST("detector: swift build → Swift") {  // v1.21.3 (F3)
    Detector d;
    ASSERT_EQ(d.detect("swift build"), CmdType::Swift);
}

TEST("detector: kotlinc → Kotlin") {  // v1.21.3 (F3)
    Detector d;
    ASSERT_EQ(d.detect("kotlinc Main.kt"), CmdType::Kotlin);
}

TEST("detector: pytest → Test") {
    Detector d;
    ASSERT_EQ(d.detect("pytest tests/"), CmdType::Test);
}

TEST("detector: jest → Test") {
    Detector d;
    ASSERT_EQ(d.detect("jest --watch"), CmdType::Test);
}

TEST("detector: grep → Search") {
    Detector d;
    ASSERT_EQ(d.detect("grep -r pattern src/"), CmdType::Search);
}

TEST("detector: rg → Search") {
    Detector d;
    ASSERT_EQ(d.detect("rg --type cpp myFunc"), CmdType::Search);
}

TEST("detector: docker → Docker") {
    Detector d;
    ASSERT_EQ(d.detect("docker build -t myapp ."), CmdType::Docker);
}

TEST("detector: npm install → PackageManager") {
    Detector d;
    ASSERT_EQ(d.detect("npm install lodash"), CmdType::PackageManager);
}

TEST("detector: pip install → PackageManager") {
    Detector d;
    ASSERT_EQ(d.detect("pip install requests"), CmdType::PackageManager);
}

TEST("detector: unknown → Default") {
    Detector d;
    ASSERT_EQ(d.detect("ls -la"), CmdType::Default);
}

TEST("detector: empty → Default") {
    Detector d;
    ASSERT_EQ(d.detect(""), CmdType::Default);
}


// ---- Command densifier (pre-exec) -----------------------------------------
TEST("densify: git status -> porcelain+branch") {
    ASSERT_EQ(densifyCommand("git status"), std::string("git status --porcelain=v2 --branch"));
}
TEST("densify: git log -> oneline; pytest -> quiet/tb; tsc -> pretty false") {
    ASSERT_EQ(densifyCommand("git log"), std::string("git log --oneline"));
    ASSERT_EQ(densifyCommand("pytest tests/"), std::string("pytest tests/ -q --tb=line"));
    ASSERT_EQ(densifyCommand("tsc"), std::string("tsc --pretty false"));
    ASSERT_EQ(densifyCommand("pip list"), std::string("pip list --format=freeze"));
    ASSERT_EQ(densifyCommand("npm ls"), std::string("npm ls --depth=0"));
}
TEST("densify: idempotent + skips when user gave a conflicting flag") {
    // already-dense -> unchanged (idempotent)
    ASSERT_EQ(densifyCommand("git status --porcelain=v2 --branch"),
              std::string("git status --porcelain=v2 --branch"));
    ASSERT_EQ(densifyCommand("git log --oneline"), std::string("git log --oneline"));
    ASSERT_EQ(densifyCommand("pytest -v"), std::string("pytest -v"));   // explicit verbose wins
}
TEST("densify: bails on shell composition + leaves unknown commands alone") {
    ASSERT_EQ(densifyCommand("git status && echo hi"), std::string("git status && echo hi"));
    ASSERT_EQ(densifyCommand("echo $(git status)"), std::string("echo $(git status)"));
    ASSERT_EQ(densifyCommand("git status | head"), std::string("git status | head"));
    ASSERT_EQ(densifyCommand("ls -la"), std::string("ls -la"));         // no rule -> unchanged
}

// ---- OpenSSL RNG override (BCrypt) -- Server 2019 err126 root fix ----------
#if defined(_WIN32)
TEST("openssl_rng: bcryptFill yields entropy (non-zero, varies, n=0 ok)") {
    unsigned char a[32] = {0}, b[32] = {0};
    ASSERT_TRUE(icmg::core::bcryptFill(a, sizeof(a)));
    ASSERT_TRUE(icmg::core::bcryptFill(b, sizeof(b)));
    bool a_allzero = true; for (unsigned char c : a) if (c) { a_allzero = false; break; }
    ASSERT_FALSE(a_allzero);                       // real entropy, not zeros
    ASSERT_TRUE(std::memcmp(a, b, sizeof(a)) != 0); // two draws differ
    ASSERT_TRUE(icmg::core::bcryptFill(nullptr, 0)); // n=0 is a no-op success
}

TEST("openssl_rng: install routes OpenSSL RNG onto BCrypt") {
    ASSERT_TRUE(icmg::core::installBCryptOpenSSLRand());
}
#endif

// ---- hook_sanitize: drop dead python precompact-snapshot hook -------------
TEST("hook_sanitize: removes python snapshot cmd, keeps native, prunes empties") {
    auto cfg = nlohmann::json::parse(R"({
      "hooks": {
        "PreCompact": [
          { "hooks": [
              { "type": "command", "command": "python3 ~/.claude/hooks/icmg-precompact-snapshot.py" },
              { "type": "command", "command": "icmg hook precompact" }
          ]},
          { "hooks": [
              { "type": "command", "command": "python3 ~/.claude/hooks/icmg-precompact-snapshot.py" }
          ]}
        ]
      }
    })");
    int removed = icmg::core::removeStaleSnapshotHooks(cfg);
    ASSERT_EQ(removed, 2);
    // entry 1 keeps only the native command; entry 2 (now empty) is pruned.
    ASSERT_EQ((int)cfg["hooks"]["PreCompact"].size(), 1);
    ASSERT_EQ((int)cfg["hooks"]["PreCompact"][0]["hooks"].size(), 1);
    ASSERT_CONTAINS(cfg["hooks"]["PreCompact"][0]["hooks"][0]["command"].get<std::string>(), "icmg hook precompact");
}

TEST("hook_sanitize: ensureStatusLine adds when absent, never clobbers") {
    auto a = nlohmann::json::object();
    ASSERT_TRUE(icmg::core::ensureStatusLine(a, "icmg statusline"));
    ASSERT_CONTAINS(a["statusLine"]["command"].get<std::string>(), "icmg statusline");
    ASSERT_EQ(a["statusLine"]["type"].get<std::string>(), std::string("command"));
    // already has one -> untouched
    auto b = nlohmann::json::parse(R"({"statusLine":{"type":"command","command":"mybar"}})");
    ASSERT_FALSE(icmg::core::ensureStatusLine(b, "icmg statusline"));
    ASSERT_EQ(b["statusLine"]["command"].get<std::string>(), std::string("mybar"));
}

TEST("hook_sanitize: no-op when no snapshot ref present") {
    auto cfg = nlohmann::json::parse(R"({"hooks":{"Stop":[{"hooks":[{"command":"icmg hook stop"}]}]}})");
    ASSERT_EQ(icmg::core::removeStaleSnapshotHooks(cfg), 0);
    ASSERT_EQ((int)cfg["hooks"]["Stop"].size(), 1);              // untouched
    auto empty = nlohmann::json::object();
    ASSERT_EQ(icmg::core::removeStaleSnapshotHooks(empty), 0);  // no hooks key
}

// ---- ruby extractor (new language) ----------------------------------------
TEST("ruby_extract: require/module/class/def -> imports/namespaces/classes/functions") {
    auto r = icmg::graph::extractRuby(
        "require 'json'\n"
        "require_relative '../lib/foo'\n"
        "module Billing\n"
        "  class Invoice < Base\n"
        "    def total; end\n"
        "    def self.create; end\n"
        "  end\n"
        "end\n");
    auto has = [](const std::vector<std::string>& v, const std::string& x) {
        return std::find(v.begin(), v.end(), x) != v.end();
    };
    ASSERT_TRUE(has(r.imports, "json"));
    ASSERT_TRUE(has(r.imports, "../lib/foo"));
    ASSERT_TRUE(has(r.namespaces, "Billing"));
    ASSERT_TRUE(has(r.classes, "Invoice"));
    ASSERT_TRUE(has(r.functions, "total"));
    ASSERT_TRUE(has(r.functions, "create"));     // def self.create
}

TEST("ruby_extract: empty + non-ruby content -> no symbols") {
    auto r = icmg::graph::extractRuby("");
    ASSERT_EQ((int)(r.imports.size() + r.classes.size() + r.functions.size()), 0);
}

static bool sym_has(const std::vector<std::string>& v, const std::string& x) {
    return std::find(v.begin(), v.end(), x) != v.end();
}

TEST("swift_extract: import/struct/class/func") {
    auto r = icmg::graph::extractSwift(
        "import Foundation\n"
        "struct Point { let x: Int }\n"
        "final class Service {\n"
        "  func run() {}\n"
        "  static func make() {}\n"
        "}\n");
    ASSERT_TRUE(sym_has(r.imports, "Foundation"));
    ASSERT_TRUE(sym_has(r.classes, "Point"));
    ASSERT_TRUE(sym_has(r.classes, "Service"));
    ASSERT_TRUE(sym_has(r.functions, "run"));
    ASSERT_TRUE(sym_has(r.functions, "make"));
}

TEST("kotlin_extract: package/import/class/fun") {
    auto r = icmg::graph::extractKotlin(
        "package com.app\n"
        "import kotlin.collections.List\n"
        "data class User(val id: Int)\n"
        "object Repo {\n"
        "  suspend fun fetch(): User? = null\n"
        "}\n");
    ASSERT_TRUE(sym_has(r.namespaces, "com.app"));
    ASSERT_TRUE(sym_has(r.imports, "kotlin.collections.List"));
    ASSERT_TRUE(sym_has(r.classes, "User"));
    ASSERT_TRUE(sym_has(r.classes, "Repo"));
    ASSERT_TRUE(sym_has(r.functions, "fetch"));
}

TEST("scala_extract: package/import/object/trait/def") {
    auto r = icmg::graph::extractScala(
        "package billing\n"
        "import scala.collection.mutable\n"
        "sealed trait Event\n"
        "case class Paid(amount: Int) extends Event\n"
        "object Ledger {\n"
        "  def post(e: Event): Unit = {}\n"
        "}\n");
    ASSERT_TRUE(sym_has(r.namespaces, "billing"));
    ASSERT_TRUE(sym_has(r.imports, "scala.collection.mutable"));
    ASSERT_TRUE(sym_has(r.classes, "Event"));
    ASSERT_TRUE(sym_has(r.classes, "Paid"));
    ASSERT_TRUE(sym_has(r.classes, "Ledger"));
    ASSERT_TRUE(sym_has(r.functions, "post"));
}

TEST("lua_extract: require + function forms") {
    auto r = icmg::graph::extractLua(
        "local json = require 'json'\n"
        "function M.greet(name) end\n"
        "local function helper() end\n"
        "Handler = function() end\n");
    ASSERT_TRUE(sym_has(r.imports, "json"));
    ASSERT_TRUE(sym_has(r.functions, "M.greet"));
    ASSERT_TRUE(sym_has(r.functions, "helper"));
    ASSERT_TRUE(sym_has(r.functions, "Handler"));
}

TEST("dart_extract: import/class/mixin/enum") {
    auto r = icmg::graph::extractDart(
        "import 'package:flutter/material.dart';\n"
        "abstract class Widget {}\n"
        "mixin Clickable {}\n"
        "enum Color { red, green }\n");
    ASSERT_TRUE(sym_has(r.imports, "package:flutter/material.dart"));
    ASSERT_TRUE(sym_has(r.classes, "Widget"));
    ASSERT_TRUE(sym_has(r.classes, "Clickable"));
    ASSERT_TRUE(sym_has(r.classes, "Color"));
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
