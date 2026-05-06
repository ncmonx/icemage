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

namespace icmg::test {

struct TestCase {
    std::string name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> r;
    return r;
}

inline int run_all() {
    int passed = 0, failed = 0;
    for (auto& tc : registry()) {
        try {
            tc.fn();
            std::cout << "  [PASS] " << tc.name << "\n";
            ++passed;
        } catch (const std::exception& e) {
            std::cout << "  [FAIL] " << tc.name << "\n"
                      << "         " << e.what() << "\n";
            ++failed;
        }
    }
    std::cout << "\n" << passed << " passed, " << failed << " failed\n";
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
#define TEST(name) \
    static void _test_fn_##__LINE__(); \
    static ::icmg::test::Registrar _reg_##__LINE__(name, _test_fn_##__LINE__); \
    static void _test_fn_##__LINE__()

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
