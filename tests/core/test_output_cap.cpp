// TDD guard for #5 (filesystem-as-context / just-in-time reference):
//   capOutput spills over-budget output to a temp file and leaves a reference in
//   the visible text. For the reference to be USEFUL (not just "data is gone"),
//   the footer must name the spill path AND tell the caller how to retrieve it
//   (line count + a re-read hint), so a large observation lives on disk while
//   the context holds only a cheap, actionable pointer. Pure -- temp files only.
#include "../test_main.hpp"
#include "../../src/core/output_cap.hpp"

#include <filesystem>
#include <fstream>
#include <string>

using namespace icmg::core;

TEST("output-cap: under budget is returned verbatim, no spill") {
    std::string spill;
    std::string in = "small output";
    std::string out = capOutput(in, 1000, spill);
    ASSERT_TRUE(out == in);
    ASSERT_TRUE(spill.empty());
}

TEST("output-cap: over budget truncates, spills full content to file") {
    std::string spill;
    std::string big(5000, 'x');
    std::string out = capOutput(big, 1000, spill);
    ASSERT_TRUE(out.size() < big.size());           // truncated in view
    ASSERT_FALSE(spill.empty());                    // a spill path was produced
    // The spilled file holds the FULL original (filesystem-as-context).
    std::ifstream f(spill, std::ios::binary);
    std::string disk((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    ASSERT_TRUE(disk == big);
    std::error_code ec; std::filesystem::remove(spill, ec);
}

TEST("output-cap: footer is an ACTIONABLE reference (path + retrieve hint)") {
    std::string spill;
    std::string big(8000, 'y');
    std::string out = capOutput(big, 1000, spill);
    // Names the spill path so the caller can find the data.
    ASSERT_TRUE(out.find(spill) != std::string::npos);
    // States how much was spilled (bytes) so the caller knows the scale.
    ASSERT_TRUE(out.find("spilled") != std::string::npos ||
                out.find("truncated") != std::string::npos);
    // Tells the caller how to RETRIEVE it (a re-read hint), not just that it
    // exists -- this is the "just-in-time" pointer that makes the reference
    // useful instead of a dead end.
    ASSERT_TRUE(out.find("Read ") != std::string::npos ||
                out.find("read ") != std::string::npos ||
                out.find("offset") != std::string::npos);
    std::error_code ec; std::filesystem::remove(spill, ec);
}

TEST("output-cap: same input spills to a stable (idempotent) path") {
    std::string s1, s2;
    std::string big(4000, 'z');
    capOutput(big, 500, s1);
    capOutput(big, 500, s2);
    ASSERT_TRUE(s1 == s2);   // hash-named: repeat call reuses the same file
    std::error_code ec; std::filesystem::remove(s1, ec);
}
