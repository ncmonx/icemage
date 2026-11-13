// TDD (2026-11-13, issue #281): `icmg grep` joined its rg argv into ONE shell
// string with naive quoting and ran it through bash `eval` / PowerShell, so a
// pattern with `|`, `(`, or `"` broke ("namaGudang(: command not found",
// "unexpected EOF while looking for matching `\"'"). Spec: the pattern reaches
// rg as a single argv element -- any regex/literal works.
// Skips cleanly when rg is not installed (e.g. a bare CI image).
#include "../test_main.hpp"
#include "../../src/core/registry.hpp"
#include "../../src/core/exec_utils.hpp"
#include "../../src/cli/base_command.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

bool rgAvailable() {
    auto r = icmg::core::safeExec({"rg", "--version"}, true, 10000);
    return r.exit_code == 0 && r.out.find("ripgrep") != std::string::npos;
}

fs::path makeFixture() {
    fs::path dir = fs::temp_directory_path() / "icmg_grep_argv_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    std::ofstream f(dir / "sample.jsx");
    f << "const a = tampilGudang;\n"
         "const b = namaGudang(x);\n"
         "<Column dataField=\"namagudang\" caption=\"Gudang\" />\n"
         "unrelated line\n";
    return dir;
}

std::string runGrep(const std::vector<std::string>& args) {
    auto cmd = icmg::core::Registry<icmg::cli::BaseCommand>::instance().create("grep");
    if (!cmd) return "<no grep command>";
    std::ostringstream out, err;
    auto* o = std::cout.rdbuf(out.rdbuf());
    auto* e = std::cerr.rdbuf(err.rdbuf());
    cmd->run(args);
    std::cout.rdbuf(o);
    std::cerr.rdbuf(e);
    return out.str() + err.str();
}

}  // namespace

TEST("grep_argv: alternation + escaped paren reach rg intact") {
    if (!rgAvailable()) { std::cerr << "  (skip: rg not on PATH)\n"; return; }
    fs::path dir = makeFixture();
    std::string out = runGrep({"-e", "tampilGudang|namaGudang\\(", "--", dir.string()});
    ASSERT_CONTAINS(out, "tampilGudang;");
    ASSERT_CONTAINS(out, "namaGudang(x)");
    ASSERT_TRUE(out.find("command not found") == std::string::npos);
    std::error_code ec; fs::remove_all(dir, ec);
}

TEST("grep_argv: double quote in a literal pattern works") {
    if (!rgAvailable()) { std::cerr << "  (skip: rg not on PATH)\n"; return; }
    fs::path dir = makeFixture();
    std::string out = runGrep({"-F", "-e", "Column dataField=\"namagudang", "--", dir.string()});
    ASSERT_CONTAINS(out, "caption=\"Gudang\"");
    ASSERT_TRUE(out.find("unexpected EOF") == std::string::npos);
    std::error_code ec; fs::remove_all(dir, ec);
}

TEST("grep_argv: shell metachars are not executed") {
    if (!rgAvailable()) { std::cerr << "  (skip: rg not on PATH)\n"; return; }
    fs::path dir = makeFixture();
    std::string out = runGrep({"-e", "x;echo PWNED_281", "--", dir.string()});
    ASSERT_TRUE(out.find("PWNED_281") == std::string::npos);
    std::error_code ec; fs::remove_all(dir, ec);
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
