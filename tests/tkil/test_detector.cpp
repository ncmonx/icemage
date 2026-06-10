#include "../test_main.hpp"
#include "../../src/tkil/detector.hpp"
#include "../../src/core/cmd_densify.hpp"
#include "../../src/core/openssl_rng.hpp"
#include <cstring>

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

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
