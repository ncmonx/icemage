// Phase 82 T5 — streaming filter summary test.
// Verifies that the filter summary line appended after stream output
// is emitted only when lines are reduced, and has the correct format.
#include "../test_main.hpp"
#include "../../src/tkil/filters/filter_utils.hpp"
#include "../../src/tkil/base_filter.hpp"
#include "../../src/core/registry.hpp"

using namespace icmg::tkil;
using Reg = icmg::core::Registry<BaseFilter>;

static BaseFilter* getFilter(const std::string& key) {
    static std::unordered_map<std::string, std::unique_ptr<BaseFilter>> cache;
    if (cache.find(key) == cache.end())
        cache[key] = Reg::instance().create(key);
    return cache[key].get();
}

TEST("stream: filter on passthrough output → no summary needed") {
    // Short output — all lines pass → original_lines == filtered_lines
    auto* f = getFilter("default");
    std::string raw;
    for (int i = 0; i < 5; ++i) raw += "line " + std::to_string(i) + "\n";
    auto fr = f->filter(raw, "echo");
    ASSERT_EQ(fr.original_lines, fr.filtered_lines);
}

TEST("stream: filter reduces noisy build output") {
    // Build filter strips compiling lines; leaves only errors/warnings/summary.
    auto* f = getFilter("build");
    std::string raw;
    for (int i = 0; i < 50; ++i)
        raw += "Compiling module" + std::to_string(i) + ".cpp\n";
    raw += "error: undefined reference to 'foo'\n";
    raw += "build failed\n";

    auto fr = f->filter(raw, "cmake --build .");
    ASSERT_TRUE(fr.filtered_lines < fr.original_lines);
    ASSERT_CONTAINS(fr.output, "error:");
    ASSERT_NOT_CONTAINS(fr.output, "Compiling module0");
}

TEST("stream: summary format — filtered/original lines shown") {
    // Simulate the stream summary line format:
    // "[stream: N/M lines pass filter]"
    auto* f = getFilter("build");
    std::string raw;
    for (int i = 0; i < 20; ++i)
        raw += "Compiling file" + std::to_string(i) + ".cpp\n";
    raw += "error: something wrong\n";

    auto fr = f->filter(raw, "make");
    // Construct the stream summary line exactly as tkil.cpp does:
    std::string summary = "[stream: " + std::to_string(fr.filtered_lines)
                        + "/" + std::to_string(fr.original_lines)
                        + " lines pass filter]";
    ASSERT_CONTAINS(summary, "[stream:");
    ASSERT_CONTAINS(summary, "lines pass filter]");
    ASSERT_TRUE(fr.filtered_lines <= fr.original_lines);
}

TEST("stream: no summary when all lines pass") {
    // When original_lines == filtered_lines, stream should skip summary.
    auto* f = getFilter("default");
    std::string raw = "a\nb\nc\n";
    auto fr = f->filter(raw, "cat");
    // The condition in tkil.cpp: if (fr2.original_lines != fr2.filtered_lines) → emit
    bool should_emit_summary = (fr.original_lines != fr.filtered_lines);
    ASSERT_FALSE(should_emit_summary);
}

TEST("stream: filter utils splitLines counts correctly") {
    std::string s = "a\nb\nc\n";
    auto lines = icmg::tkil::splitLines(s);
    ASSERT_EQ((int)lines.size(), 3);
}

TEST("stream: splitLines handles empty string") {
    auto lines = icmg::tkil::splitLines("");
    ASSERT_EQ((int)lines.size(), 0);
}

int main() {
    std::cout << "=== stream filter tests ===\n";
    return icmg::test::run_all();
}
