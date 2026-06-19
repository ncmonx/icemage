// Slice-1 of Adaptive Output Gate: LearnedGlossary — a cross-session vocabulary
// that accumulates which compression aliases actually recur, so the compressor
// can be SEEDED with proven-useful mappings instead of recomputing from zero
// each call. This is the "brain" that makes `icmg compress` self-improving.
//
// Behaviors under test:
//   (1) recordUse increments hits per phrase across calls
//   (2) suggest(minHits) returns only phrases at/above the hit threshold
//   (3) persistence: a fresh store on the SAME db still sees prior hits
//       (i.e. learning survives a session boundary)
//   (4) tok_saved accumulates and orders suggestions (most-valuable first)

#include "../test_main.hpp"
#include "../../src/core/db.hpp"
#include "../../src/compress/learned_glossary.hpp"

using namespace icmg;

namespace {
std::map<std::string,std::string> g(std::initializer_list<std::pair<const std::string,std::string>> l) {
    return std::map<std::string,std::string>(l.begin(), l.end());
}
} // namespace

TEST("LG: recordUse increments hits per phrase") {
    core::Db db(":memory:");
    compress::LearnedGlossary lg(db);
    lg.recordUse(g({{"G1", "CMakeLists.txt"}}), 3);
    lg.recordUse(g({{"G1", "CMakeLists.txt"}}), 3);
    lg.recordUse(g({{"G1", "CMakeLists.txt"}}), 3);
    ASSERT_EQ(lg.hits("CMakeLists.txt"), 3);
}

TEST("LG: suggest returns only phrases at/above minHits") {
    core::Db db(":memory:");
    compress::LearnedGlossary lg(db);
    lg.recordUse(g({{"G1", "frequently used phrase"}}), 5);
    lg.recordUse(g({{"G1", "frequently used phrase"}}), 5);
    lg.recordUse(g({{"G2", "one off phrase"}}), 5);   // only 1 hit

    auto s = lg.suggest(/*minHits=*/2);
    bool has_freq = false, has_oneoff = false;
    for (auto& kv : s) {
        if (kv.second == "frequently used phrase") has_freq = true;
        if (kv.second == "one off phrase")        has_oneoff = true;
    }
    ASSERT_TRUE(has_freq);
    ASSERT_FALSE(has_oneoff);
}

TEST("LG: learning persists across a fresh store on same db (session boundary)") {
    // file-backed db so a second store instance reopens the same data.
    core::Db db("learned_glossary_test.db");
    {
        compress::LearnedGlossary lg(db);
        lg.recordUse(g({{"G1", "persisted phrase value"}}), 4);
        lg.recordUse(g({{"G1", "persisted phrase value"}}), 4);
    }
    // fresh store, same db handle -> must still see the accumulated hits
    compress::LearnedGlossary lg2(db);
    ASSERT_EQ(lg2.hits("persisted phrase value"), 2);
    auto s = lg2.suggest(2);
    bool found = false;
    for (auto& kv : s) if (kv.second == "persisted phrase value") found = true;
    ASSERT_TRUE(found);
}

TEST("LG: suggestions ordered by accumulated tok_saved (most valuable first)") {
    core::Db db(":memory:");
    compress::LearnedGlossary lg(db);
    // low-value phrase: many hits but tiny savings
    lg.recordUse(g({{"A", "lo"}}), 1);
    lg.recordUse(g({{"A", "lo"}}), 1);
    lg.recordUse(g({{"A", "lo"}}), 1);
    // high-value phrase: fewer hits but big savings
    lg.recordUse(g({{"B", "a very long boilerplate header block"}}), 50);
    lg.recordUse(g({{"B", "a very long boilerplate header block"}}), 50);

    auto ranked = lg.suggestRanked(/*minHits=*/2, /*limit=*/10);
    ASSERT_TRUE(ranked.size() >= 2);
    ASSERT_EQ(ranked.front().phrase, "a very long boilerplate header block");
}

TEST("LG: perEntrySaved divides total savings across glossary entries") {
    // 1000 in -> 600 out = 400 saved, spread over 4 entries = 100 each.
    ASSERT_EQ(compress::LearnedGlossary::perEntrySaved(1000, 600, 4), 100);
    // no entries -> never divide by zero
    ASSERT_EQ(compress::LearnedGlossary::perEntrySaved(1000, 600, 0), 0);
    // negative/no savings clamps to 0 (never reward a bad compress)
    ASSERT_EQ(compress::LearnedGlossary::perEntrySaved(500, 700, 4), 0);
}
