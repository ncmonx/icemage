#include "../test_main.hpp"
#include "../../src/rtk/detector.hpp"

using icmg::rtk::CmdType;
using icmg::rtk::Detector;

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

TEST("detector: cargo build → Build") {
    Detector d;
    ASSERT_EQ(d.detect("cargo build --release"), CmdType::Build);
}

TEST("detector: cmake build → Build") {
    Detector d;
    ASSERT_EQ(d.detect("cmake --build ."), CmdType::Build);
}

TEST("detector: make → Build") {
    Detector d;
    ASSERT_EQ(d.detect("make -j8"), CmdType::Build);
}

TEST("detector: cargo test → Test") {
    Detector d;
    ASSERT_EQ(d.detect("cargo test"), CmdType::Test);
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

int main() {
    std::cout << "=== Detector tests ===\n";
    return icmg::test::run_all();
}
