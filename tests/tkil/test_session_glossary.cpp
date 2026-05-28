// v1.56 T1 Stage 5: session glossary.
//
// Tracks line frequency within a session. When a line crosses the
// frequency threshold, subsequent appearances are replaced by a short
// token (e.g. "$S1"). The glossary maps tokens back to original
// phrases so AI can expand on demand.

#include "../test_main.hpp"
#include "../../src/tkil/session_glossary.hpp"

using namespace icmg::tkil;

TEST("SessionGlossary: empty input returns empty") {
    SessionGlossary g;
    ASSERT_EQ(g.apply(""), std::string(""));
}

TEST("SessionGlossary: single line passes through unchanged") {
    SessionGlossary g;
    ASSERT_EQ(g.apply("hello\n"), std::string("hello\n"));
}

TEST("SessionGlossary: line below threshold passes verbatim across calls") {
    SessionGlossary g;
    const std::string phrase = "this is a repeated phrase\n";  // >= 8 chars
    g.apply(phrase);
    g.apply(phrase);
    // Two occurrences across calls — still below threshold (3).
    std::string out = g.apply(phrase);
    // The third occurrence crosses threshold; it IS replaced with a token
    // and the glossary registers the mapping.
    ASSERT_TRUE(out.find("$S") != std::string::npos);
    ASSERT_TRUE(g.expand("$S1").has_value());
    ASSERT_EQ(*g.expand("$S1"), std::string("this is a repeated phrase"));
}

TEST("SessionGlossary: distinct lines get distinct tokens") {
    SessionGlossary g;
    // Push each phrase past the threshold (both >=8 chars).
    for (int i = 0; i < 3; ++i) g.apply("alpha phrase here\n");
    for (int i = 0; i < 3; ++i) g.apply("beta phrase here\n");
    auto a = g.expand("$S1");
    auto b = g.expand("$S2");
    ASSERT_TRUE(a.has_value());
    ASSERT_TRUE(b.has_value());
    ASSERT_TRUE(*a != *b);
}

TEST("SessionGlossary: error lines never tokenised") {
    SessionGlossary g;
    std::string err = "error: LNK2001 missing symbol\n";
    for (int i = 0; i < 5; ++i) g.apply(err);
    std::string out = g.apply(err);
    // The line must remain verbatim (no $S token in its place).
    ASSERT_TRUE(out.find("error: LNK2001 missing symbol") != std::string::npos);
    ASSERT_TRUE(out.find("$S") == std::string::npos);
}

TEST("SessionGlossary: expand on unknown token returns nullopt") {
    SessionGlossary g;
    ASSERT_FALSE(g.expand("$S999").has_value());
    ASSERT_FALSE(g.expand("not-a-token").has_value());
}

TEST("SessionGlossary: multi-line input replaces only over-threshold lines") {
    SessionGlossary g;
    for (int i = 0; i < 3; ++i) g.apply("repeat-me\n");
    // Now "repeat-me" is in glossary. New input has it + a fresh line.
    std::string out = g.apply("repeat-me\nfresh-line\n");
    ASSERT_TRUE(out.find("$S") != std::string::npos);
    ASSERT_TRUE(out.find("fresh-line") != std::string::npos);
    ASSERT_TRUE(out.find("repeat-me") == std::string::npos);
}

TEST("SessionGlossary: reset clears state") {
    SessionGlossary g;
    const std::string phrase = "long enough phrase to qualify\n";
    for (int i = 0; i < 3; ++i) g.apply(phrase);
    ASSERT_TRUE(g.expand("$S1").has_value());
    g.reset();
    ASSERT_FALSE(g.expand("$S1").has_value());
    // After reset, line counter starts fresh.
    g.apply(phrase);
    g.apply(phrase);
    std::string out = g.apply(phrase);
    // Third occurrence after reset crosses threshold again -> token.
    ASSERT_TRUE(out.find("$S") != std::string::npos);
}

TEST("SessionGlossary: short lines (<= 8 chars) not tokenised — not worth saving") {
    SessionGlossary g;
    // "ok" is 2 chars — tokenising would COST more than passing through.
    for (int i = 0; i < 10; ++i) g.apply("ok\n");
    std::string out = g.apply("ok\n");
    ASSERT_TRUE(out.find("ok") != std::string::npos);
    ASSERT_TRUE(out.find("$S") == std::string::npos);
}
