// me-everywhere #1 — event bus core (pure). Sessions emit typed action events
// (edit/claim/done/note) to a shared append-only log; others tail events newer
// than their cursor -> "see each other move". Pure serialize/filter -> TDD-able.
#include "../test_main.hpp"
#include "../../src/core/event_bus.hpp"
#include <vector>
#include <string>

using namespace icmg::core;

static BusEvent ev(int64_t ts, const std::string& actor, const std::string& kind,
                   const std::string& target, const std::string& detail) {
    BusEvent e; e.ts = ts; e.actor = actor; e.kind = kind; e.target = target; e.detail = detail; return e;
}

TEST("bus: line round-trips (detail with tab/newline/backslash)") {
    BusEvent e = ev(1717, "sess-A", "edit", "src/foo.cpp", "line 42\tcol 3\nwrap\\path");
    BusEvent got;
    ASSERT_TRUE(eventFromLine(eventToLine(e), got));
    ASSERT_EQ(got.ts, (int64_t)1717);
    ASSERT_EQ(got.actor, std::string("sess-A"));
    ASSERT_EQ(got.kind, std::string("edit"));
    ASSERT_EQ(got.target, std::string("src/foo.cpp"));
    ASSERT_EQ(got.detail, e.detail);                    // tab+newline+backslash preserved
}

TEST("bus: empty target + empty detail round-trip") {
    BusEvent e = ev(10, "B", "done", "", "");
    BusEvent got;
    ASSERT_TRUE(eventFromLine(eventToLine(e), got));
    ASSERT_EQ(got.target, std::string(""));
    ASSERT_EQ(got.detail, std::string(""));
}

TEST("bus: malformed line -> false") {
    BusEvent got;
    ASSERT_TRUE(!eventFromLine("", got));
    ASSERT_TRUE(!eventFromLine("a\tb\tc", got));             // too few fields
    ASSERT_TRUE(!eventFromLine("notnum\tA\tedit\tx\ty", got)); // non-numeric ts
    ASSERT_TRUE(!eventFromLine("10\t\tedit\tx\ty", got));      // empty actor
    ASSERT_TRUE(!eventFromLine("10\tA\t\tx\ty", got));         // empty kind
}

TEST("bus: eventsSince returns strictly newer events") {
    std::vector<BusEvent> all = {
        ev(100, "A", "edit", "a.cpp", ""),
        ev(150, "B", "claim", "b.cpp", ""),
        ev(150, "A", "note", "", "same ts as B"),
        ev(200, "C", "done", "", ""),
    };
    auto since = eventsSince(all, 150);                 // strictly > 150
    ASSERT_EQ(since.size(), (size_t)1);
    ASSERT_EQ(since[0].actor, std::string("C"));
}

TEST("bus: eventsSince(0) returns all") {
    std::vector<BusEvent> all = { ev(1, "A", "x", "", ""), ev(2, "B", "y", "", "") };
    ASSERT_EQ(eventsSince(all, 0).size(), (size_t)2);
}
