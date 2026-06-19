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

// ---- Slice-4: durability (recency-decay + prune) ----------------------------
// Append-only memory floods with stale noise; what makes it AWET (durable yet
// relevant) is that recently-used vocab stays strong and dead weight fades.

TEST("LG: decayedValue halves at one half-life, full when fresh") {
    const int64_t day = 86400;
    int64_t now = 1'000'000'000;
    // fresh (last_seen == now) -> no decay
    ASSERT_EQ((int)compress::LearnedGlossary::decayedValue(100, now, now, 10.0), 100);
    // exactly one half-life old -> ~half
    double half = compress::LearnedGlossary::decayedValue(100, now - 10 * day, now, 10.0);
    ASSERT_TRUE(half > 49.0 && half < 51.0);
    // halflife <= 0 disables decay (always full value)
    ASSERT_EQ((int)compress::LearnedGlossary::decayedValue(100, now - 100 * day, now, 0.0), 100);
}

TEST("LG: recency re-ranks a fresh phrase above a stale high-value one") {
    core::Db db(":memory:");
    compress::LearnedGlossary lg(db);
    const int64_t day = 86400;
    int64_t now = (int64_t)::time(nullptr);
    // STALE big-savings phrase (last used long ago).
    lg.recordUse(g({{"A", "old expensive boilerplate header"}}), 100, now - 60 * day);
    lg.recordUse(g({{"A", "old expensive boilerplate header"}}), 100, now - 60 * day);
    // FRESH smaller-savings phrase (used just now).
    lg.recordUse(g({{"B", "fresh recurring config key"}}), 40, now);
    lg.recordUse(g({{"B", "fresh recurring config key"}}), 40, now);

    // With a short half-life, recency dominates: fresh phrase ranks first.
    auto ranked = lg.suggestRanked(/*minHits=*/2, /*limit=*/10, now, /*halflife_days=*/14.0);
    ASSERT_TRUE(ranked.size() >= 2);
    ASSERT_EQ(ranked.front().phrase, "fresh recurring config key");
}

TEST("LG: prune removes stale low-hit dead weight, keeps live signal") {
    core::Db db(":memory:");
    compress::LearnedGlossary lg(db);
    const int64_t day = 86400;
    int64_t now = (int64_t)::time(nullptr);
    // dead weight: 1 hit, very old
    lg.recordUse(g({{"A", "abandoned one off phrase"}}), 5, now - 120 * day);
    // live: many hits, recent
    lg.recordUse(g({{"B", "still useful recurring phrase"}}), 5, now);
    lg.recordUse(g({{"B", "still useful recurring phrase"}}), 5, now);
    lg.recordUse(g({{"B", "still useful recurring phrase"}}), 5, now);

    int removed = lg.prune(/*max_age_days=*/90, /*min_hits=*/2, now);
    ASSERT_EQ(removed, 1);
    ASSERT_EQ(lg.hits("abandoned one off phrase"), 0);     // gone
    ASSERT_EQ(lg.hits("still useful recurring phrase"), 3); // kept
}
