// TDD (2026-08-25): brain v2.22 #3 -- quick-note promotion by heat.
// Research: MemoryOS (arXiv 2506.06326) short->mid-term promotion by access
// heat. quick:<epoch> notes with proven recall value get a permanent topic.
#include "../test_main.hpp"
#include "../../src/imem/quick_promote.hpp"

using namespace icmg::imem;

static MemoryNode qnode(int64_t id, const std::string& topic, int freq,
                        const std::string& kw = "", const std::string& content = "note text") {
    MemoryNode n;
    n.id = id; n.topic = topic; n.frequency = freq;
    n.keywords = kw; n.content = content;
    return n;
}

TEST("promote: hot quick note flagged, cold one not") {
    std::vector<MemoryNode> v = {
        qnode(1, "quick:100", 5, "daemon,mutex,spam"),
        qnode(2, "quick:200", 1, "one,off,note"),
    };
    auto p = findQuickPromotions(v, 3, 10);
    ASSERT_EQ((int)p.size(), 1);
    ASSERT_EQ(p[0].id, (int64_t)1);
}

TEST("promote: non-quick topics never touched") {
    std::vector<MemoryNode> v = {
        qnode(1, "decisions-db", 50, "wal,mode"),
        qnode(2, "bug:linker", 50, "lld,link"),
    };
    ASSERT_EQ((int)findQuickPromotions(v, 3, 10).size(), 0);
}

TEST("promote: suggested topic is hot: + keyword tokens") {
    auto n = qnode(1, "quick:100", 5, "Daemon,Mutex,Spam,extra");
    ASSERT_EQ(suggestPromotedTopic(n), std::string("hot:daemon-mutex-spam"));
}

TEST("promote: falls back to content words when no keywords") {
    auto n = qnode(1, "quick:100", 5, "", "vulkan shader hang during ninja build");
    ASSERT_EQ(suggestPromotedTopic(n), std::string("hot:vulkan-shader-hang"));
}

TEST("promote: hottest first, capped, deleted/invalidated skipped") {
    std::vector<MemoryNode> v;
    for (int i = 1; i <= 8; ++i) v.push_back(qnode(i, "quick:" + std::to_string(i), i, "kw" + std::to_string(i) + "x,two"));
    v[7].deleted_at = 123;          // id 8 (freq 8) deleted -> skipped
    v[6].invalidated_at = 123;      // id 7 (freq 7) superseded -> skipped
    auto p = findQuickPromotions(v, 3, 2);
    ASSERT_EQ((int)p.size(), 2);
    ASSERT_EQ(p[0].id, (int64_t)6);  // hottest surviving
    ASSERT_EQ(p[1].id, (int64_t)5);
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
