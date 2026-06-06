// me-everywhere #5 — shared hot working-memory core (pure). A volatile shared
// whiteboard (key -> value) across live sessions: a scratch tier alongside the
// long-term archival memory. Pure serialize + last-write-wins dedup -> TDD-able.
#include "../test_main.hpp"
#include "../../src/core/hot.hpp"
#include <vector>
#include <string>

using namespace icmg::core;

static HotEntry he(const std::string& k, int64_t ts, const std::string& v) {
    HotEntry e; e.key = k; e.ts = ts; e.value = v; return e;
}

TEST("hot: line round-trips (value with tab/newline/backslash)") {
    HotEntry e = he("build:status", 1717, "green\t1525/0\nnext: ship\\batch");
    HotEntry got;
    ASSERT_TRUE(hotFromLine(hotToLine(e), got));
    ASSERT_EQ(got.key, std::string("build:status"));
    ASSERT_EQ(got.ts, (int64_t)1717);
    ASSERT_EQ(got.value, e.value);
}

TEST("hot: malformed line -> false") {
    HotEntry got;
    ASSERT_TRUE(!hotFromLine("", got));
    ASSERT_TRUE(!hotFromLine("key\t10", got));      // missing value field
    ASSERT_TRUE(!hotFromLine("key\tx\tv", got));    // non-numeric ts
    ASSERT_TRUE(!hotFromLine("\t10\tv", got));      // empty key
}

TEST("hot: latestPerKey - last write wins") {
    std::vector<HotEntry> log = {
        he("focus", 10, "BPE"),
        he("owner", 11, "sess-A"),
        he("focus", 25, "me-everywhere"),   // newer write to focus
    };
    auto cur = latestPerKey(log);
    ASSERT_EQ(cur.size(), (size_t)2);
    for (auto& e : cur) if (e.key == "focus") ASSERT_EQ(e.value, std::string("me-everywhere"));
}

TEST("hot: empty value round-trips (a cleared key)") {
    HotEntry e = he("k", 5, "");
    HotEntry got;
    ASSERT_TRUE(hotFromLine(hotToLine(e), got));
    ASSERT_EQ(got.value, std::string(""));
}
