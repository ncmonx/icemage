#include "../../tests/test_main.hpp"
#include "../../src/imem/skill_chunker.hpp"

#include <string>
#include <vector>

using icmg::imem::SkillChunk;
using icmg::imem::SkillChunker;

// ---------------------------------------------------------------------------
// 1. Split returns one chunk per H2
// ---------------------------------------------------------------------------
TEST("split returns chunk per H2") {
    std::string md =
        "## Section One\n"
        "content one\n"
        "## Section Two\n"
        "content two\n"
        "## Section Three\n"
        "content three\n";

    auto chunks = SkillChunker::split(md, "skill:test");
    ASSERT_EQ(chunks.size(), static_cast<std::size_t>(3));
    ASSERT_EQ(chunks[0].heading, std::string("Section One"));
    ASSERT_EQ(chunks[1].heading, std::string("Section Two"));
    ASSERT_EQ(chunks[2].heading, std::string("Section Three"));
}

// ---------------------------------------------------------------------------
// 2. Slugify heading into parent_path
// ---------------------------------------------------------------------------
TEST("split slugifies heading into parent_path") {
    std::string md =
        "## Pasal 528 — Pewarisan\n"
        "some content here\n";

    auto chunks = SkillChunker::split(md, "skill:legal");
    ASSERT_EQ(chunks.size(), static_cast<std::size_t>(1));
    ASSERT_EQ(chunks[0].parent_path, std::string("skill:legal/pasal-528-pewarisan"));
}

// ---------------------------------------------------------------------------
// 3. Intro before first H2 captured as (intro) chunk
// ---------------------------------------------------------------------------
TEST("intro before first H2 captured as (intro) chunk") {
    std::string md =
        "This is the intro paragraph.\n"
        "It spans multiple lines.\n"
        "\n"
        "## First Section\n"
        "section content\n";

    auto chunks = SkillChunker::split(md, "skill:test");
    ASSERT_EQ(chunks.size(), static_cast<std::size_t>(2));
    ASSERT_EQ(chunks[0].heading, std::string("(intro)"));
    ASSERT_TRUE(chunks[0].content.find("intro paragraph") != std::string::npos);
    ASSERT_EQ(chunks[1].heading, std::string("First Section"));
}

// ---------------------------------------------------------------------------
// 4. Large H2 section promotes H3 to own chunks
// ---------------------------------------------------------------------------
TEST("large H2 section promotes H3 to chunks") {
    // Build an H2 section > 4096 bytes by padding H3 content
    std::string padding(1500, 'x');
    std::string md =
        "## Big Section\n"
        "### Sub One\n" + padding + "\n"
        "### Sub Two\n" + padding + "\n"
        "### Sub Three\n" + padding + "\n";

    // Total H2 body is well over 4 KB (3 * ~1500 = ~4500+ chars)
    auto chunks = SkillChunker::split(md, "skill:test");

    // Expect 3 H3 chunks promoted (H2 intro is empty, so dropped)
    // OR 4 chunks if the empty H2 header chunk is kept — we document: empty H2 header dropped
    ASSERT_EQ(chunks.size(), static_cast<std::size_t>(3));
    ASSERT_EQ(chunks[0].heading, std::string("Sub One"));
    ASSERT_EQ(chunks[1].heading, std::string("Sub Two"));
    ASSERT_EQ(chunks[2].heading, std::string("Sub Three"));
}

// ---------------------------------------------------------------------------
// 5. Empty input returns empty vector
// ---------------------------------------------------------------------------
TEST("empty input returns empty vector") {
    auto chunks = SkillChunker::split("", "skill:empty");
    ASSERT_EQ(chunks.size(), static_cast<std::size_t>(0));
}

// ---------------------------------------------------------------------------
// 6. Max 500 chunks cap applied
// ---------------------------------------------------------------------------
TEST("max 500 chunks cap applied") {
    std::string md;
    for (int i = 0; i < 501; ++i) {
        md += "## Section " + std::to_string(i) + "\ncontent\n";
    }
    auto chunks = SkillChunker::split(md, "skill:big");
    ASSERT_EQ(chunks.size(), static_cast<std::size_t>(500));
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
