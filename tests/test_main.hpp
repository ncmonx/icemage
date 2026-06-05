#pragma once
// Minimal self-contained test harness — no third-party deps.
// Usage:
//   TEST("my test") { ASSERT_EQ(1+1, 2); }
//   int main() { return icmg::test::run_all(); }

#include <iostream>
#include <string>
#include <vector>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <filesystem>
#include <system_error>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace icmg::test {

struct TestCase {
    std::string name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> r;
    return r;
}

// v1.29.0 mono-test groundwork: between-tests hook. In per-exe mode this
// is a no-op. In mono mode (ICMG_MONO_TEST_RESET defined at compile time
// for icmg_test target only), it resets shared singletons so each TEST()
// starts from a clean state. Currently resets Scorer; add more singletons
// here as mono mode discovers cross-test pollution.
#ifdef ICMG_MONO_TEST_RESET
}  // namespace icmg::test (close briefly for forward-decl)
namespace icmg::imem { class Scorer; }
namespace icmg::test {
inline void betweenTests();  // declared, defined in mono_main.cpp
#else
inline void betweenTests() { /* no-op in per-exe mode */ }
#endif

// v1.26.0 (B1): filter-aware run_all. When `filter` non-empty, runs only
// TEST() cases whose name CONTAINS the filter substring. Single mono test
// binary (`icmg_test`) dispatches via ctest with --filter <suite-prefix>.
inline int run_all(const std::string& filter = "") {
#if defined(_WIN32)
    // Mirror main.cpp: suppress the modal "insert disk in drive B:" popup that
    // Windows raises when a spawned child process (CreateProcess) probes a dead
    // PATH drive. The test entrypoint must set this itself — main.cpp's call
    // does not cover the test binary's own process.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
#endif
    // Reproducibility: delete stale per-run temp DBs left by a PRIOR invocation.
    // Tests share fixed "*_test.db" filenames in the cwd; SQLite WAL/SHM sidecars
    // (-wal/-shm) carry committed rows across separate runs, which caused false
    // failures on a second back-to-back run (#30952). Clearing once per run keeps
    // mono-test results deterministic; within-run isolation stays via distinct
    // user_ids per TEST.
    {
        std::error_code ec;
        for (auto it = std::filesystem::directory_iterator(".", ec);
             !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
            const std::string fn = it->path().filename().string();
            if (fn.find("_test.db") != std::string::npos)  // matches .db, -wal, -shm
                std::filesystem::remove(it->path(), ec);
        }
    }
    int passed = 0, failed = 0, skipped = 0;
    for (auto& tc : registry()) {
        if (!filter.empty() && tc.name.find(filter) == std::string::npos) {
            ++skipped;
            continue;
        }
        try {
            tc.fn();
            std::cout << "  [PASS] " << tc.name << "\n";
            ++passed;
        } catch (const std::exception& e) {
            std::cout << "  [FAIL] " << tc.name << "\n"
                      << "         " << e.what() << "\n";
            ++failed;
        }
        // v1.29.0 mono-test groundwork: reset singletons between TESTs so
        // mono icmg_test (when enabled) doesn't leak state. No-op in
        // per-exe mode (default).
        betweenTests();
    }
    std::cout << "\n" << passed << " passed, " << failed << " failed";
    if (skipped > 0) std::cout << " (" << skipped << " skipped by filter)";
    std::cout << "\n";
    // v1.26.0: ctest entry with mis-matched filter returns 0-passed. Treat
    // that as failure so the misalignment is visible (v1.27 audit will rename
    // TEST() cases to align with filename stems).
    if (!filter.empty() && passed == 0 && failed == 0) {
        std::cerr << "ERROR: filter '" << filter << "' matched 0 tests\n";
        return 2;
    }
    return (failed > 0) ? 1 : 0;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) {
        registry().push_back({name, std::move(fn)});
    }
};

// Assertion helpers — throw on failure so catch block catches them
inline void assert_true(bool cond, const char* expr, const char* file, int line) {
    if (!cond) {
        std::ostringstream os;
        os << file << ":" << line << ": ASSERT_TRUE failed: " << expr;
        throw std::runtime_error(os.str());
    }
}

template<typename A, typename B>
inline void assert_eq(const A& a, const B& b, const char* ea, const char* eb,
                      const char* file, int line) {
    if (!(a == b)) {
        std::ostringstream os;
        os << file << ":" << line << ": ASSERT_EQ(" << ea << ", " << eb << ")\n"
           << "         lhs = " << a << "\n"
           << "         rhs = " << b;
        throw std::runtime_error(os.str());
    }
}

inline void assert_contains(const std::string& haystack, const std::string& needle,
                             const char* file, int line) {
    if (haystack.find(needle) == std::string::npos) {
        std::ostringstream os;
        os << file << ":" << line << ": ASSERT_CONTAINS failed\n"
           << "         needle   = \"" << needle << "\"\n"
           << "         haystack = \"" << haystack.substr(0, 120) << "\"";
        throw std::runtime_error(os.str());
    }
}

inline void assert_not_contains(const std::string& haystack, const std::string& needle,
                                 const char* file, int line) {
    if (haystack.find(needle) != std::string::npos) {
        std::ostringstream os;
        os << file << ":" << line << ": ASSERT_NOT_CONTAINS: found \"" << needle << "\"";
        throw std::runtime_error(os.str());
    }
}

} // namespace icmg::test

// ---- Macros ----------------------------------------------------------------
// Double-indirection required so __LINE__ expands before token-paste (##).
#define ICMG_TEST_CONCAT_(a, b) a##b
#define ICMG_TEST_CONCAT(a, b)  ICMG_TEST_CONCAT_(a, b)
#define ICMG_TEST_FN(line)      ICMG_TEST_CONCAT(_test_fn_, line)
#define ICMG_TEST_REG(line)     ICMG_TEST_CONCAT(_reg_, line)

#define TEST(name) \
    static void ICMG_TEST_FN(__LINE__)(); \
    static ::icmg::test::Registrar ICMG_TEST_REG(__LINE__)(name, ICMG_TEST_FN(__LINE__)); \
    static void ICMG_TEST_FN(__LINE__)()

#define ASSERT_TRUE(cond) \
    ::icmg::test::assert_true(!!(cond), #cond, __FILE__, __LINE__)

#define ASSERT_FALSE(cond) \
    ::icmg::test::assert_true(!(cond), "!" #cond, __FILE__, __LINE__)

#define ASSERT_EQ(a, b) \
    ::icmg::test::assert_eq((a), (b), #a, #b, __FILE__, __LINE__)

#define ASSERT_CONTAINS(haystack, needle) \
    ::icmg::test::assert_contains((haystack), (needle), __FILE__, __LINE__)

#define ASSERT_NOT_CONTAINS(haystack, needle) \
    ::icmg::test::assert_not_contains((haystack), (needle), __FILE__, __LINE__)
