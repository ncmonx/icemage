#include "../test_main.hpp"
#include "../../src/core/registry.hpp"
#include "../../src/rtk/base_filter.hpp"

// Filters are registered via static init from their .cpp TUs (linked via icmg_lib).
// Access them through the Registry.

using icmg::rtk::BaseFilter;
using Reg = icmg::core::Registry<icmg::rtk::BaseFilter>;

static BaseFilter* getFilter(const std::string& key) {
    static std::unordered_map<std::string, std::unique_ptr<BaseFilter>> cache;
    if (cache.find(key) == cache.end())
        cache[key] = Reg::instance().create(key);
    return cache[key].get();
}

// ---- Default filter --------------------------------------------------------

TEST("default filter: short output — no truncation") {
    auto* f = getFilter("default");
    std::string raw;
    for (int i = 0; i < 10; ++i) raw += "line " + std::to_string(i) + "\n";
    auto r = f->filter(raw, "ls");
    ASSERT_EQ(r.original_lines, 10);
    ASSERT_EQ(r.filtered_lines, 10);
    ASSERT_FALSE(r.was_truncated);
}

TEST("default filter: long output — head+tail kept") {
    auto* f = getFilter("default");
    std::string raw;
    for (int i = 0; i < 200; ++i) raw += "line " + std::to_string(i) + "\n";
    auto r = f->filter(raw, "ls");
    ASSERT_EQ(r.original_lines, 200);
    ASSERT_TRUE(r.was_truncated);
    ASSERT_CONTAINS(r.output, "line 0");       // head present
    ASSERT_CONTAINS(r.output, "line 199");     // tail present
    ASSERT_CONTAINS(r.output, "lines omitted");
}

// ---- Build filter ----------------------------------------------------------

TEST("build filter: keeps errors") {
    auto* f = getFilter("build");
    std::string raw =
        "Compiling foo.cpp\n"
        "error: use of undeclared identifier 'bar'\n"
        "Compiling baz.cpp\n"
        "build failed\n";
    auto r = f->filter(raw, "make");
    ASSERT_CONTAINS(r.output, "error:");
    ASSERT_NOT_CONTAINS(r.output, "Compiling");
}

TEST("build filter: keeps warnings") {
    auto* f = getFilter("build");
    std::string raw =
        "Compiling foo.cpp\n"
        "warning: unused variable 'x'\n"
        "build successful\n";
    auto r = f->filter(raw, "cargo build");
    ASSERT_CONTAINS(r.output, "warning:");
}

TEST("build filter: clean build — shows summary") {
    auto* f = getFilter("build");
    std::string raw =
        "Compiling foo.cpp\n"
        "Compiling bar.cpp\n"
        "Fresh icmg\n"
        "0 errors, 0 warnings\n";
    auto r = f->filter(raw, "cargo build");
    // Last line should be present (summary)
    ASSERT_CONTAINS(r.output, "0 errors");
}

// ---- Test filter -----------------------------------------------------------

TEST("test filter: keeps failures") {
    auto* f = getFilter("test");
    std::string raw =
        "running 10 tests\n"
        "test foo ... ok\n"
        "test bar ... FAILED\n"
        "failures:\n"
        "    bar: assertion failed\n"
        "test result: FAILED. 1 failed; 9 passed\n";
    auto r = f->filter(raw, "cargo test");
    ASSERT_CONTAINS(r.output, "FAILED");
    ASSERT_NOT_CONTAINS(r.output, "test foo ... ok");
}

TEST("test filter: all passing — shows summary") {
    auto* f = getFilter("test");
    std::string raw =
        "running 5 tests\n"
        "test a ... ok\n"
        "test b ... ok\n"
        "test result: ok. 5 passed\n";
    auto r = f->filter(raw, "cargo test");
    ASSERT_CONTAINS(r.output, "test result:");
}

// ---- Search filter ---------------------------------------------------------

TEST("search filter: groups by file") {
    auto* f = getFilter("search");
    std::string raw =
        "src/foo.cpp:10: match one\n"
        "src/foo.cpp:20: match two\n"
        "src/bar.cpp:5: another match\n";
    auto r = f->filter(raw, "grep -r pattern src/");
    ASSERT_CONTAINS(r.output, "src/foo.cpp");
    ASSERT_CONTAINS(r.output, "src/bar.cpp");
}

int main() {
    std::cout << "=== RTK filters tests ===\n";
    return icmg::test::run_all();
}
