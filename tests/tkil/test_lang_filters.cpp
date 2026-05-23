// v1.21.5 (TDD catchup): F3 per-language filter tests.
//
// Each lang filter: (a) keeps error/warning + summary, (b) drops the
// language-specific noise the generic Build filter doesn't recognise.

#include "../test_main.hpp"
#include "../../src/core/registry.hpp"
#include "../../src/tkil/base_filter.hpp"

using icmg::tkil::BaseFilter;
using Reg = icmg::core::Registry<icmg::tkil::BaseFilter>;

static BaseFilter* getFilter(const std::string& key) {
    static std::unordered_map<std::string, std::unique_ptr<BaseFilter>> cache;
    if (cache.find(key) == cache.end()) cache[key] = Reg::instance().create(key);
    return cache[key].get();
}

// ---- Rust ------------------------------------------------------------------

TEST("rust filter: keeps error[EXXX] + drops Compiling/Blocking noise") {
    auto* f = getFilter("rust");
    ASSERT_TRUE(f != nullptr);
    std::string raw =
        "   Compiling serde v1.0.190\n"
        "   Compiling tokio v1.34.0\n"
        "    Blocking waiting for file lock on package cache\n"
        "error[E0382]: borrow of moved value: `x`\n"
        "  --> src/main.rs:5:10\n"
        "   |\n"
        " 5 |     drop(x);\n"
        "   |          ^ value moved here\n"
        "error: aborting due to 1 previous error\n";
    auto r = f->filter(raw, "cargo build");
    ASSERT_CONTAINS(r.output, "error[E0382]");
    ASSERT_CONTAINS(r.output, "--> src/main.rs");
    ASSERT_NOT_CONTAINS(r.output, "Compiling serde");
    ASSERT_NOT_CONTAINS(r.output, "Blocking waiting");
}

TEST("rust filter: keeps test result summary") {
    auto* f = getFilter("rust");
    std::string raw =
        "running 5 tests\n"
        "test foo ... ok\n"
        "test bar ... FAILED\n"
        "test result: FAILED. 1 failed; 4 passed\n";
    auto r = f->filter(raw, "cargo test");
    ASSERT_CONTAINS(r.output, "test result:");
    ASSERT_CONTAINS(r.output, "FAILED");
}

// ---- Go --------------------------------------------------------------------

TEST("go filter: drops 'go: downloading' + keeps --- FAIL:") {
    auto* f = getFilter("go");
    ASSERT_TRUE(f != nullptr);
    std::string raw =
        "go: downloading github.com/foo/bar v1.0.0\n"
        "go: downloading github.com/baz/qux v0.5.2\n"
        "--- FAIL: TestAuth (0.01s)\n"
        "    auth_test.go:42: expected 200, got 401\n"
        "FAIL\n"
        "FAIL\tpkg/auth\t0.123s\n";
    auto r = f->filter(raw, "go test ./...");
    ASSERT_CONTAINS(r.output, "--- FAIL: TestAuth");
    ASSERT_CONTAINS(r.output, "FAIL");
    ASSERT_NOT_CONTAINS(r.output, "go: downloading");
}

TEST("go filter: keeps panic + goroutine traces") {
    auto* f = getFilter("go");
    std::string raw =
        "panic: runtime error: index out of range\n"
        "\n"
        "goroutine 1 [running]:\n"
        "main.main()\n"
        "\tmain.go:10 +0x40\n";
    auto r = f->filter(raw, "go run main.go");
    ASSERT_CONTAINS(r.output, "panic:");
    ASSERT_CONTAINS(r.output, "goroutine 1");
}

// ---- Java ------------------------------------------------------------------

TEST("java filter: drops [INFO] flood + keeps [ERROR] + summary") {
    auto* f = getFilter("java");
    ASSERT_TRUE(f != nullptr);
    std::string raw =
        "[INFO] Scanning for projects...\n"
        "[INFO] -----------------< com.example:app >-----------------\n"
        "[INFO] Building app 1.0\n"
        "Downloading from central: https://repo.maven.apache.org/...\n"
        "Progress (1): 5.4/12.8 kB\n"
        "[ERROR] Failed to execute goal\n"
        "[ERROR]   org.codehaus.mojo:exec-maven-plugin: missing input file\n"
        "BUILD FAILURE\n";
    auto r = f->filter(raw, "mvn package");
    ASSERT_CONTAINS(r.output, "[ERROR]");
    ASSERT_CONTAINS(r.output, "BUILD FAILURE");
    ASSERT_NOT_CONTAINS(r.output, "[INFO] Scanning");
    ASSERT_NOT_CONTAINS(r.output, "Downloading from central");
}

TEST("java filter: keeps Tests run summary") {
    auto* f = getFilter("java");
    std::string raw =
        "[INFO] Running com.example.FooTest\n"
        "Tests run: 5, Failures: 1, Errors: 0, Skipped: 0\n"
        "[INFO] BUILD SUCCESS\n";
    auto r = f->filter(raw, "mvn test");
    ASSERT_CONTAINS(r.output, "Tests run:");
    ASSERT_CONTAINS(r.output, "BUILD SUCCESS");
}

// ---- .NET ------------------------------------------------------------------

TEST("dotnet filter: drops Restore noise + keeps error CSxxxx") {
    auto* f = getFilter("dotnet");
    ASSERT_TRUE(f != nullptr);
    std::string raw =
        "  Determining projects to restore...\n"
        "  Restored C:/proj/app.csproj (in 1.2 sec).\n"
        "  Restore complete (1.3s)\n"
        "MSBuild version 17.8.0\n"
        "Program.cs(12,5): error CS0103: The name 'Foo' does not exist\n"
        "Build FAILED.\n";
    auto r = f->filter(raw, "dotnet build");
    ASSERT_CONTAINS(r.output, "error CS0103");
    ASSERT_CONTAINS(r.output, "Build FAILED");
    ASSERT_NOT_CONTAINS(r.output, "Determining projects");
    ASSERT_NOT_CONTAINS(r.output, "MSBuild version");
}

TEST("dotnet filter: keeps test summary") {
    auto* f = getFilter("dotnet");
    std::string raw =
        "Test Run Successful.\n"
        "Total tests: 12\n"
        "     Passed: 10\n"
        "     Failed: 2\n";
    auto r = f->filter(raw, "dotnet test");
    ASSERT_CONTAINS(r.output, "Failed:");
    ASSERT_CONTAINS(r.output, "Total tests:");
}

// ---- Swift -----------------------------------------------------------------

TEST("swift filter: drops CompileC/Ld + keeps .swift errors") {
    auto* f = getFilter("swift");
    ASSERT_TRUE(f != nullptr);
    std::string raw =
        "CompileC build/Foo.o Foo.swift\n"
        "Ld build/MyApp normal x86_64\n"
        "CodeSign build/MyApp\n"
        "/path/to/Foo.swift:12:8: error: cannot find 'bar' in scope\n"
        "** BUILD FAILED **\n";
    auto r = f->filter(raw, "xcodebuild");
    ASSERT_CONTAINS(r.output, "Foo.swift:12:8");
    ASSERT_CONTAINS(r.output, "** BUILD FAILED **");
    ASSERT_NOT_CONTAINS(r.output, "CompileC build");
    ASSERT_NOT_CONTAINS(r.output, "CodeSign");
}

// ---- Kotlin ----------------------------------------------------------------

TEST("kotlin filter: keeps e:/w: prefixes + .kt errors") {
    auto* f = getFilter("kotlin");
    ASSERT_TRUE(f != nullptr);
    std::string raw =
        "> Task :compileKotlin UP-TO-DATE\n"
        "> Task :compileTestKotlin\n"
        "e: Main.kt: (5, 1): unresolved reference: foo\n"
        "w: Main.kt: (10, 5): variable 'x' is never used\n"
        "FAILURE: Build failed with an exception.\n";
    auto r = f->filter(raw, "./gradlew build");
    ASSERT_CONTAINS(r.output, "e: Main.kt");
    ASSERT_CONTAINS(r.output, "w: Main.kt");
    ASSERT_CONTAINS(r.output, "FAILURE:");
    ASSERT_NOT_CONTAINS(r.output, "UP-TO-DATE");
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
