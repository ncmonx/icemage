#include "../test_main.hpp"
#include "../../src/core/registry.hpp"
#include "../../src/tkil/base_filter.hpp"
#include <string>
#include <cstdio>

using icmg::core::Registry;
using icmg::tkil::BaseFilter;

// ---- GitFilter: 7-char short-hash regex bug -------------------------------
// Root cause (2026-07-08): found while investigating why a sibling project's
// savings-dashboard baseline still sat at ~42% even after fixing the shell-
// wrapper bugs (bash -c / powershell -File). GitFilter (registered under
// "git" in the tkil filter Registry) detects a commit-hash line via a regex
// requiring an 8-40 char hex prefix, but `git log --oneline` (the
// overwhelmingly common default) prints a 7-char SHORT hash. Since the
// regex never matched a 7-char hash, the per-entry counter it drives never
// incremented, so the "cap at 30 entries" truncation path was NEVER
// reachable for typical --oneline output -- confirmed in production
// telemetry: a single `git log --oneline origin/main..HEAD` call emitted
// 76,551 raw bytes with filtered_bytes IDENTICAL (0% saved).
//
// GitFilter has no public header (defined + registered inside git_filter.cpp)
// -- accessed here via the same Registry<BaseFilter> lookup the real Tkil
// pipeline uses (icmg::core::Registry<icmg::tkil::BaseFilter>::instance()
// .create("git")), so this exercises the exact same code path as production.

TEST("GitFilter: git log --oneline with 7-char short hashes gets truncated past the cap") {
    auto f = Registry<BaseFilter>::instance().create("git");
    std::string big;
    for (int i = 0; i < 100; ++i) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%07x", i + 0xA00000);
        big += std::string(buf) + " some commit subject line number " + std::to_string(i) + "\n";
    }
    auto result = f->filter(big, "git log --oneline");
    // GitFilter's own "cap at 30 entries" logic appends this exact marker --
    // it does NOT set res.was_truncated (that flag belongs to applyHardLimit's
    // separate 500-line cap). Check for the marker + a materially smaller
    // output, which is what the bug actually breaks.
    ASSERT_CONTAINS(result.output, "truncated at 30 entries");
    ASSERT_TRUE(result.output.size() < big.size());
}

TEST("GitFilter: 8+ char hashes (verbose/collision-safe) also still work (no regression)") {
    auto f = Registry<BaseFilter>::instance().create("git");
    std::string big;
    for (int i = 0; i < 100; ++i) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%08x", i + 0xA00000);
        big += std::string(buf) + " some commit subject line number " + std::to_string(i) + "\n";
    }
    auto result = f->filter(big, "git log --oneline --abbrev=8");
    ASSERT_CONTAINS(result.output, "truncated at 30 entries");
    ASSERT_TRUE(result.output.size() < big.size());
}

TEST("GitFilter: small output (under the cap) passes through unchanged, 7-char hashes") {
    auto f = Registry<BaseFilter>::instance().create("git");
    std::string small = "a1b2c3d fix: something small\nb2c3d4e docs: update readme\n";
    auto result = f->filter(small, "git log --oneline");
    ASSERT_FALSE(result.output.find("truncated") != std::string::npos);
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
