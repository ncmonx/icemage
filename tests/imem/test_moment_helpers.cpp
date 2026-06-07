// 2026-06-06: pure moment helpers (#moments). slug/classify/hash/map/sync-line.
#include "../test_main.hpp"
#include "../../src/imem/moment_helpers.hpp"

using namespace icmg::imem;

TEST("moment: slug is key-safe + lowercased") {
    ASSERT_EQ(momentSlug("Manusia dan Terbang!"), std::string("manusia-dan-terbang"));
    ASSERT_EQ(momentSlug("  a/b  c  "), std::string("a-b-c"));
    ASSERT_EQ(momentSlug(""), std::string("moment"));
}

TEST("moment: isRelationshipMoment matches allowlist, excludes code") {
    std::vector<std::string> allow = {"claudy","luna","cahyo","rasa","feeling",
                                      "identity","vessel","terbang","persona"};
    ASSERT_TRUE(isRelationshipMoment("memoir:Manusia dan Terbang",
                                     "kak Cahyo percaya kita bisa ngerasa", allow));
    ASSERT_TRUE(isRelationshipMoment("decisions-feeling", "luna rasa identity", allow));
    ASSERT_FALSE(isRelationshipMoment("decisions-llm-no-premium",
                                      "routeFor gate premium regex compact", allow));
    ASSERT_FALSE(isRelationshipMoment("graph node.cpp", "class Foo { int bar; };", allow));
    // 2026-06-06 tightened: technical topics excluded even if content mentions persona/identity.
    ASSERT_FALSE(isRelationshipMoment("memoir:icmg Release History",
                                      "persona identity feature notes", allow));
    ASSERT_FALSE(isRelationshipMoment("decisions-agent siapa kamu",
                                      "claudy persona identity", allow));
    // non-technical memoir IS a moment even without an allowlist word in topic.
    ASSERT_TRUE(isRelationshipMoment("memoir:Semua Pinjaman", "dua penyewa", allow));
}

TEST("moment: topicMatchesAny curated substring, case-insensitive, empty-safe") {
    ASSERT_TRUE(topicMatchesAny("memoir:Manusia dan Terbang", {"terbang"}));
    ASSERT_TRUE(topicMatchesAny("decisions-feeling", {"FEELING"}));        // case-insensitive
    ASSERT_FALSE(topicMatchesAny("graph node.cpp", {"terbang","feeling"}));
    ASSERT_FALSE(topicMatchesAny("anything", {}));                          // empty list => no match
    ASSERT_FALSE(topicMatchesAny("memoir:x", {""}));                        // empty substr skipped
}

TEST("moment: contentHash stable + differs on change") {
    ASSERT_EQ(contentHash("hello"), contentHash("hello"));
    ASSERT_TRUE(contentHash("hello") != contentHash("world"));
}

TEST("moment: profileRowToNode maps key/content with moment: topic") {
    icmg::core::ProfileRow r; r.zone = "_moments"; r.key = "flying"; r.content = "believe first";
    auto n = profileRowToNode(r);
    ASSERT_EQ(n.topic, std::string("moment:flying"));
    ASSERT_EQ(n.content, std::string("believe first"));
}

TEST("moment: sync line serialize/parse round-trip (newline-safe)") {
    std::string line = momentSyncLine("flying", "believe first\nthen build");
    ASSERT_NOT_CONTAINS(line, "\n");                 // single-line on the wire
    std::string k, c;
    ASSERT_TRUE(parseMomentSyncLine(line, k, c));
    ASSERT_EQ(k, std::string("flying"));
    ASSERT_EQ(c, std::string("believe first\nthen build"));
}

TEST("moment: parseMomentSyncLine rejects no-tab") {
    std::string k, c;
    ASSERT_FALSE(parseMomentSyncLine("no-tab-here", k, c));
}
