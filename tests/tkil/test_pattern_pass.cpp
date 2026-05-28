// v1.56 T1 Stage 3: pattern-collapse profiles.
//
// Each profile keys off the command line and transforms repetitive output
// patterns into a single summary line. Registry-driven so profiles can be
// added without touching dispatcher code.

#include "../test_main.hpp"
#include "../../src/tkil/pattern_pass.hpp"

using namespace icmg::tkil;

// ---------------- generic dispatch ----------------

TEST("patternPass: empty input returns empty") {
    ASSERT_EQ(patternPass("", "anything"), std::string(""));
}

TEST("patternPass: unknown cmdline returns input unchanged") {
    std::string in = "line1\nline2\nline3\n";
    ASSERT_EQ(patternPass(in, "unknown-tool --weird"), in);
}

// ---------------- npm install profile ----------------

TEST("npm install: collapses 'added N packages' banners") {
    std::string in =
        "npm WARN deprecated foo@1.0: use bar\n"
        "added 421 packages, and audited 423 packages in 12s\n"
        "found 0 vulnerabilities\n";
    std::string out = patternPass(in, "npm install");
    // The 'added 421 packages' line should remain (it IS the summary).
    ASSERT_TRUE(out.find("added 421 packages") != std::string::npos);
    // deprecated warnings should pass through (real signal).
    ASSERT_TRUE(out.find("deprecated") != std::string::npos);
}

TEST("npm install: collapses 'npm http fetch' burst") {
    std::string in;
    for (int i = 0; i < 50; ++i) {
        in += "npm http fetch GET 200 https://registry.npmjs.org/pkg-" + std::to_string(i) + "\n";
    }
    in += "added 50 packages\n";
    std::string out = patternPass(in, "npm install");
    // Burst should collapse to single summary line.
    ASSERT_TRUE(out.size() < in.size() / 2);
    ASSERT_TRUE(out.find("added 50 packages") != std::string::npos);
}

// ---------------- cmake --build profile ----------------

TEST("cmake --build: collapses 'Building CXX object' lines") {
    std::string in;
    for (int i = 1; i <= 100; ++i) {
        in += "[" + std::to_string(i) + "/100] Building CXX object src/foo_"
              + std::to_string(i) + ".cpp.o\n";
    }
    in += "[100/100] Linking CXX executable myapp\n";
    std::string out = patternPass(in, "cmake --build build --target myapp");
    // 100 build lines should collapse to a single summary.
    ASSERT_TRUE(out.size() < in.size() / 4);
    // Linking line must survive — it's the outcome.
    ASSERT_TRUE(out.find("Linking CXX executable myapp") != std::string::npos);
}

TEST("cmake --build: error lines never collapsed") {
    std::string in =
        "[1/3] Building CXX object foo.cpp.o\n"
        "[2/3] Building CXX object bar.cpp.o\n"
        "src/bar.cpp(42): error C2065: undeclared identifier 'foo'\n"
        "[3/3] Building CXX object baz.cpp.o\n";
    std::string out = patternPass(in, "cmake --build build");
    ASSERT_TRUE(out.find("C2065") != std::string::npos);
    ASSERT_TRUE(out.find("undeclared identifier") != std::string::npos);
}

// ---------------- gh api profile ----------------

TEST("gh api: collapses large JSON object dump to '{N keys}'") {
    // Realistic-size gh api response — typical GitHub object dumps are
    // 500-2000 bytes single-line. Profile threshold is 300 bytes so we
    // build a 400+ byte fixture by padding the body field.
    std::string body_pad(280, 'x');
    std::string in =
        "{\"id\":1,\"name\":\"a\",\"url\":\"https://api.github.com/repos/x/y\""
        ",\"created_at\":\"2026-05-28T10:00:00Z\",\"updated_at\":\"2026-05-28T10:00:00Z\""
        ",\"author\":{\"login\":\"u\",\"id\":1},\"body\":\"" + body_pad + "\""
        ",\"state\":\"open\",\"labels\":[],\"milestone\":null,\"assignee\":null"
        ",\"comments\":0,\"draft\":false,\"merged\":false}\n";
    std::string out = patternPass(in, "gh api repos/x/y/issues/1");
    ASSERT_TRUE(out.size() < in.size() / 2);
    ASSERT_TRUE(out.find("keys") != std::string::npos);
}

TEST("gh api: short JSON passes through unchanged") {
    std::string in = "{\"ok\":true}\n";
    std::string out = patternPass(in, "gh api repos/x");
    ASSERT_EQ(out, in);
}

// ---------------- git push profile ----------------

TEST("git push: collapses 'remote:' banner lines") {
    std::string in =
        "Enumerating objects: 5, done.\n"
        "remote: Resolving deltas: 100% (3/3), completed with 1 local object.\n"
        "remote: \n"
        "remote: GitHub found 0 vulnerabilities on ncmonx/icm-graph's default branch (1 deep scan...)\n"
        "remote: \n"
        "remote: To learn more about how to fix this issue, visit ...\n"
        "To https://github.com/ncmonx/icm-graph.git\n"
        "   abc123..def456  release/v1.56.0 -> release/v1.56.0\n";
    std::string out = patternPass(in, "git push private release/v1.56.0");
    // 'remote:' banner block should compress.
    ASSERT_TRUE(out.size() < in.size());
    // The "To <url>" + ref-update line MUST survive — that's the outcome.
    ASSERT_TRUE(out.find("To https://") != std::string::npos);
    ASSERT_TRUE(out.find("release/v1.56.0 -> release/v1.56.0") != std::string::npos);
}

TEST("git push: error lines never collapsed") {
    std::string in =
        "remote: error: GH013: Repository rule violations found for ref ...\n"
        "remote: Some refs were not updated\n"
        "To https://github.com/x/y.git\n"
        " ! [remote rejected] main -> main\n"
        "error: failed to push some refs to 'github.com:x/y.git'\n";
    std::string out = patternPass(in, "git push origin main");
    // Both error lines and the 'remote rejected' marker must survive.
    ASSERT_TRUE(out.find("GH013") != std::string::npos);
    ASSERT_TRUE(out.find("remote rejected") != std::string::npos);
    ASSERT_TRUE(out.find("failed to push") != std::string::npos);
}
