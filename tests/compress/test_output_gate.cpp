// Slice-2 of Adaptive Output Gate: gateOutput — the decision layer that links
// compression into the output-budget path.
//
// When a tool output exceeds the byte budget, the existing capOutput() truncates
// (head+tail, middle lost). gateOutput() instead tries LOSSLESS compress first:
//   - under budget            -> pass through unchanged
//   - over budget, compresses  -> emit compressed (reversible, nothing lost)
//   - over budget, won't shrink-> fall back to capOutput (truncate + spill)
//
// Behaviors under test:
//   (1) under budget: unchanged, not compressed, not capped
//   (2) over budget + highly repetitive: compressed=true, bytes_out < bytes_in
//   (3) over budget + incompressible: capped=true with a spill pointer
//   (4) never throws; bytes_in always = full input size

#include "../test_main.hpp"
#include "../../src/compress/output_gate.hpp"
#include <string>

using namespace icmg;

namespace {
// Build a long, highly-compressible blob: a long path repeated many times so the
// Compressor's glossary substitution (min_path_len=20, freq>=3) kicks in.
std::string repetitive(int reps) {
    std::string out;
    for (int i = 0; i < reps; ++i)
        out += "see /very/long/repeated/project/path/src/module/component.cpp for details\n";
    return out;
}
} // namespace

TEST("Gate: under budget passes through unchanged") {
    std::string in = "short output line\n";
    auto r = compress::gateOutput(in, /*byte_budget=*/4096);
    ASSERT_EQ(r.text, in);
    ASSERT_FALSE(r.compressed);
    ASSERT_FALSE(r.capped);
    ASSERT_EQ(r.bytes_in, (int)in.size());
}

TEST("Gate: over budget + repetitive compresses losslessly (nothing capped)") {
    std::string in = repetitive(200);                 // big + very compressible
    auto r = compress::gateOutput(in, /*byte_budget=*/1024);
    ASSERT_TRUE(r.compressed);
    ASSERT_TRUE(r.bytes_out < r.bytes_in);
    ASSERT_EQ(r.bytes_in, (int)in.size());
    ASSERT_TRUE(r.spill_path.empty());                // no truncation spill needed
}

TEST("Gate: over budget + incompressible falls back to cap+spill") {
    // Pseudo-random-ish unique lines: no repeated long paths -> compress can't help.
    std::string in;
    for (int i = 0; i < 4000; ++i) in += "ln" + std::to_string(i * 7919) + "\n";
    auto r = compress::gateOutput(in, /*byte_budget=*/512);
    ASSERT_TRUE(r.capped);
    ASSERT_TRUE(r.text.size() < in.size());
    ASSERT_EQ(r.bytes_in, (int)in.size());
}

TEST("Gate: empty input never throws, passes through") {
    auto r = compress::gateOutput("", 100);
    ASSERT_EQ(r.text, std::string());
    ASSERT_FALSE(r.compressed);
    ASSERT_FALSE(r.capped);
}
