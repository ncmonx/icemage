// v1.56 T1 Stage 4: outcome-only mode.
//
// For "fire-and-forget" commands, AI only needs the final result line(s).
// outcomeOnly() detects eligible cmdlines and reduces output to the
// outcome (URL, hash, ref-update, byte count) + any error/fatal lines.

#include "../test_main.hpp"
#include "../../src/tkil/outcome_extractor.hpp"

using namespace icmg::tkil;

// ---------------- eligibility ----------------

TEST("outcomeEligible: gh release create matches") {
    ASSERT_TRUE(outcomeEligible("gh release create v1.0.0 file.zip"));
}

TEST("outcomeEligible: gh release upload matches") {
    ASSERT_TRUE(outcomeEligible("gh release upload v1.0.0 file.zip"));
}

TEST("outcomeEligible: git push matches") {
    ASSERT_TRUE(outcomeEligible("git push origin main"));
}

TEST("outcomeEligible: curl -o matches") {
    ASSERT_TRUE(outcomeEligible("curl -sSL -o file.tgz https://example.com/x.tgz"));
}

TEST("outcomeEligible: cmake --build matches") {
    ASSERT_TRUE(outcomeEligible("cmake --build build --target icmg"));
}

TEST("outcomeEligible: arbitrary cmd does NOT match") {
    ASSERT_FALSE(outcomeEligible("echo hello"));
    ASSERT_FALSE(outcomeEligible("ls -la"));
    ASSERT_FALSE(outcomeEligible("npm test"));
}

// ---------------- outcome extraction ----------------

TEST("outcomeOnly: gh release create extracts release URL") {
    std::string in =
        "Successfully created v1.55.0\n"
        "uploading icmg-1.55.0-win-x64.zip ... done\n"
        "uploading icmg-1.55.0-win-x64.zip.sha256 ... done\n"
        "https://github.com/ncmonx/icm-graph/releases/tag/v1.55.0\n";
    std::string out = outcomeOnly(in, "gh release create v1.55.0 file.zip");
    ASSERT_TRUE(out.find("https://github.com/ncmonx/icm-graph/releases/tag/v1.55.0") != std::string::npos);
    ASSERT_TRUE(out.size() < in.size());
}

TEST("outcomeOnly: git push extracts To+ref-update") {
    std::string in =
        "Enumerating objects: 5, done.\n"
        "Counting objects: 100% (5/5), done.\n"
        "Delta compression using up to 8 threads\n"
        "Compressing objects: 100% (3/3), done.\n"
        "Writing objects: 100% (3/3), 600 bytes\n"
        "remote: Resolving deltas: 100% (3/3)\n"
        "To https://github.com/ncmonx/icm-graph.git\n"
        "   abc123..def456  release/v1.56.0 -> release/v1.56.0\n";
    std::string out = outcomeOnly(in, "git push private release/v1.56.0");
    ASSERT_TRUE(out.find("To https://") != std::string::npos);
    ASSERT_TRUE(out.find("release/v1.56.0 -> release/v1.56.0") != std::string::npos);
    ASSERT_TRUE(out.size() < in.size() / 2);
}

TEST("outcomeOnly: cmake --build extracts final Linking line") {
    std::string in =
        "[1/100] Building CXX object src/a.cpp.o\n"
        "[2/100] Building CXX object src/b.cpp.o\n"
        "[3/100] Building CXX object src/c.cpp.o\n"
        "[99/100] Linking CXX static library icmg_lib.lib\n"
        "[100/100] Linking CXX executable icmg.exe\n";
    std::string out = outcomeOnly(in, "cmake --build build --target icmg");
    ASSERT_TRUE(out.find("Linking CXX executable icmg.exe") != std::string::npos);
    ASSERT_TRUE(out.size() < in.size() / 3);
}

TEST("outcomeOnly: errors preserved verbatim") {
    std::string in =
        "[1/3] Building CXX object foo.cpp.o\n"
        "src/foo.cpp(42): error C2065: undeclared identifier 'bar'\n"
        "FAILED: [code=1] foo.cpp.o\n"
        "[2/3] Building CXX object baz.cpp.o\n";
    std::string out = outcomeOnly(in, "cmake --build build");
    ASSERT_TRUE(out.find("C2065") != std::string::npos);
    ASSERT_TRUE(out.find("FAILED") != std::string::npos);
}

TEST("outcomeOnly: non-eligible cmd returns input unchanged") {
    std::string in = "line1\nline2\nline3\n";
    ASSERT_EQ(outcomeOnly(in, "echo something"), in);
}

TEST("outcomeOnly: empty input returns empty") {
    ASSERT_EQ(outcomeOnly("", "git push origin main"), std::string(""));
}

TEST("outcomeOnly: curl -o extracts only HTTP errors + last byte line") {
    std::string in =
        "  % Total    % Received % Xferd  Average Speed   Time    Time     Time  Current\n"
        "                                 Dload  Upload   Total   Spent    Left  Speed\n"
        "  0     0    0     0    0     0      0      0 --:--:-- --:--:-- --:--:--     0\n"
        "100 1024k  100 1024k    0     0  10.0M      0 --:--:-- --:--:-- --:--:-- 10.0M\n";
    std::string out = outcomeOnly(in, "curl -sSL -o file.tgz https://example.com/x.tgz");
    // Should keep the last progress line (final byte count) but drop the
    // header noise.
    ASSERT_TRUE(out.size() < in.size());
}
