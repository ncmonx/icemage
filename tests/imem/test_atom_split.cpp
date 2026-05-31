// v1.79.0 ICM dual-memory — heuristic atom splitter tests.
#include "../test_main.hpp"
#include "../../src/imem/atom_split.hpp"

TEST("atom_split: splits on sentence boundaries") {
    auto v = icmg::imem::atomSplit("Fix auth bug. Token check uses < not <=. Added test.");
    ASSERT_EQ((int)v.size(), 3);
    ASSERT_EQ(v[0], std::string("Fix auth bug."));
    ASSERT_EQ(v[2], std::string("Added test."));
}

TEST("atom_split: splits bullet lines into atoms") {
    auto v = icmg::imem::atomSplit("- decision A chosen\n- decision B rejected\n- open item C");
    ASSERT_EQ((int)v.size(), 3);
    ASSERT_EQ(v[1], std::string("decision B rejected"));
}

TEST("atom_split: drops empties + trims; single short text = one atom") {
    auto v = icmg::imem::atomSplit("   single fact   ");
    ASSERT_EQ((int)v.size(), 1);
    ASSERT_EQ(v[0], std::string("single fact"));
}

TEST("atom_split: does not split inside code fence") {
    auto v = icmg::imem::atomSplit("Use this. ```a. b. c.``` Done.");
    ASSERT_EQ((int)v.size(), 3);
    ASSERT_TRUE(v[1].find("```") != std::string::npos);
}

#include "../../src/imem/atom_llm.hpp"

TEST("atom_llm: parses one-fact-per-line model output") {
    auto v = icmg::imem::parseLlmAtoms("- user fixed auth bug\n- token check off-by-one\n\n- added regression test\n");
    ASSERT_EQ((int)v.size(), 3);
    ASSERT_EQ(v[1], std::string("token check off-by-one"));
}

TEST("atom_llm: empty model output falls back to heuristic split") {
    auto v = icmg::imem::llmAtomizeOrFallback("Fact A. Fact B.", "");
    ASSERT_EQ((int)v.size(), 2);
}
