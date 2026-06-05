// me-everywhere step 1 — presence core (pure). Who is alive, on what, NOW.
// Liveness by heartbeat TTL (mirrors agent_lease), upsert-by-session, and a
// TSV line round-trip (focus free-text escaped). Pure -> fully unit-testable.
#include "../test_main.hpp"
#include "../../src/core/presence.hpp"
#include <vector>
#include <string>

using namespace icmg::core;

static PresenceEntry mk(const std::string& id, int64_t pid, const std::string& focus, int64_t hb) {
    PresenceEntry e; e.session_id = id; e.pid = pid; e.focus = focus; e.heartbeat_at = hb; return e;
}

TEST("presence: livePresence keeps fresh, drops stale") {
    std::vector<PresenceEntry> all = {
        mk("a", 1, "editing settings.js", 100),   // 100s ago -> stale if ttl<...
        mk("b", 2, "running tests", 195),
    };
    auto live = livePresence(all, /*now=*/200, /*ttl=*/30);
    ASSERT_EQ(live.size(), (size_t)1);             // only b (200-195=5 <= 30); a (100) stale
    ASSERT_EQ(live[0].session_id, std::string("b"));
}

TEST("presence: livePresence boundary is inclusive") {
    std::vector<PresenceEntry> all = { mk("a", 1, "x", 70) };
    ASSERT_EQ(livePresence(all, 100, 30).size(), (size_t)1);   // 100-70=30 == ttl -> live
    ASSERT_EQ(livePresence(all, 101, 30).size(), (size_t)0);   // 31 > 30 -> stale
}

TEST("presence: upsert replaces same session, appends new") {
    std::vector<PresenceEntry> all = { mk("a", 1, "old", 10) };
    all = upsertPresence(all, mk("a", 1, "new", 20));
    ASSERT_EQ(all.size(), (size_t)1);
    ASSERT_EQ(all[0].focus, std::string("new"));
    ASSERT_EQ(all[0].heartbeat_at, (int64_t)20);
    all = upsertPresence(all, mk("b", 2, "other", 21));
    ASSERT_EQ(all.size(), (size_t)2);
}

TEST("presence: line round-trips (incl focus with tab/newline)") {
    PresenceEntry e = mk("sess-1", 4321, "edit\tfoo.cpp\nstep 2", 1717);
    std::string line = presenceToLine(e);
    PresenceEntry got;
    ASSERT_TRUE(presenceFromLine(line, got));
    ASSERT_EQ(got.session_id, e.session_id);
    ASSERT_EQ(got.pid, e.pid);
    ASSERT_EQ(got.heartbeat_at, e.heartbeat_at);
    ASSERT_EQ(got.focus, e.focus);                 // tab + newline preserved
}

TEST("presence: malformed line -> false") {
    PresenceEntry got;
    ASSERT_TRUE(!presenceFromLine("", got));
    ASSERT_TRUE(!presenceFromLine("only-one-field", got));
    ASSERT_TRUE(!presenceFromLine("id\tnotanumber\t10\tfocus", got));
}

TEST("presence: latestPerSession keeps newest beat per session") {
    std::vector<PresenceEntry> log = {
        mk("a", 1, "first", 10),
        mk("b", 2, "bee",   12),
        mk("a", 1, "second", 25),    // newer beat for a
    };
    auto l = latestPerSession(log);
    ASSERT_EQ(l.size(), (size_t)2);
    for (auto& e : l) if (e.session_id == "a") ASSERT_EQ(e.focus, std::string("second"));
}
