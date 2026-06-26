// tests/graph/test_shell_extractor.cpp
//
// TDD tests for ShellExtractor.
//
// Encoding conventions:
//   functions << "deploy"            -> function symbol
//   imports  << "sources:./lib.sh"  -> edge_type="sources"

#include "../test_main.hpp"
#include "../../src/graph/extractor/base_extractor.hpp"
#include "../../src/core/registry.hpp"
#include <string>

static icmg::graph::BaseExtractor* getShExt() {
    static auto inst = []() -> std::unique_ptr<icmg::graph::BaseExtractor> {
        auto& reg = icmg::core::Registry<icmg::graph::BaseExtractor>::instance();
        if (!reg.has("shell")) return nullptr;
        return reg.create("shell");
    }();
    return inst.get();
}

TEST("shell-extractor: registered") {
    ASSERT_TRUE(getShExt() != nullptr);
}

TEST("shell-extractor: function foo() -> functions entry") {
    auto* ext = getShExt();
    ASSERT_TRUE(ext != nullptr);

    std::string src =
        "#!/usr/bin/env bash\n"
        "function deploy() {\n"
        "  echo hello\n"
        "}\n"
        "build() {\n"
        "  cmake --build .\n"
        "}\n";
    auto r = ext->extract("deploy.sh", src);

    bool dep = false, bld = false;
    for (auto& f : r.functions) {
        if (f == "deploy") dep = true;
        if (f == "build")  bld = true;
    }
    ASSERT_TRUE(dep);
    ASSERT_TRUE(bld);
}

TEST("shell-extractor: source ./lib.sh -> sources: import") {
    auto* ext = getShExt();
    ASSERT_TRUE(ext != nullptr);

    std::string src = "source ./lib/helpers.sh\necho done\n";
    auto r = ext->extract("main.sh", src);

    bool found = false;
    for (auto& imp : r.imports)
        if (imp == "sources:./lib/helpers.sh") found = true;
    ASSERT_TRUE(found);
}

TEST("shell-extractor: dot-source '. ./lib.sh' -> sources: import") {
    auto* ext = getShExt();
    ASSERT_TRUE(ext != nullptr);

    std::string src = ". ./common.sh\n";
    auto r = ext->extract("run.sh", src);

    bool found = false;
    for (auto& imp : r.imports)
        if (imp == "sources:./common.sh") found = true;
    ASSERT_TRUE(found);
}

TEST("shell-extractor: handles .ps1 function") {
    auto* ext = getShExt();
    ASSERT_TRUE(ext != nullptr);

    std::string src = "function Setup {\n  Write-Host hi\n}\n";
    auto r = ext->extract("build.ps1", src);

    bool found = false;
    for (auto& f : r.functions)
        if (f == "Setup") found = true;
    ASSERT_TRUE(found);
}

TEST("shell-extractor: no crash on empty input") {
    auto* ext = getShExt();
    ASSERT_TRUE(ext != nullptr);
    auto r = ext->extract("empty.sh", "");
    (void)r;
}
