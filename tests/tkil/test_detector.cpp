#include "../test_main.hpp"
#include "../../src/tkil/detector.hpp"

using icmg::tkil::CmdType;
using icmg::tkil::Detector;

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


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
