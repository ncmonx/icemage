// me-everywhere #4 — cross-session messaging core (pure). Directed (or broadcast)
// messages between live sessions; an inbox reads messages addressed to me (or "*")
// newer than my cursor, excluding my own. Pure serialize/filter -> TDD-able.
#include "../test_main.hpp"
#include "../../src/core/msg.hpp"
#include <vector>
#include <string>

using namespace icmg::core;

static Message mm(int64_t ts, const std::string& from, const std::string& to, const std::string& body) {
    Message m; m.ts = ts; m.from = from; m.to = to; m.body = body; return m;
}

TEST("msg: line round-trips (body with tab/newline/backslash)") {
    Message m = mm(1717, "A", "B", "you hold auth?\tstatus\nline2\\x");
    Message got;
    ASSERT_TRUE(msgFromLine(msgToLine(m), got));
    ASSERT_EQ(got.ts, (int64_t)1717);
    ASSERT_EQ(got.from, std::string("A"));
    ASSERT_EQ(got.to, std::string("B"));
    ASSERT_EQ(got.body, m.body);
}

TEST("msg: malformed line -> false") {
    Message got;
    ASSERT_TRUE(!msgFromLine("", got));
    ASSERT_TRUE(!msgFromLine("1\tA\tB", got));        // too few fields
    ASSERT_TRUE(!msgFromLine("x\tA\tB\thi", got));    // non-numeric ts
    ASSERT_TRUE(!msgFromLine("1\t\tB\thi", got));     // empty from
    ASSERT_TRUE(!msgFromLine("1\tA\t\thi", got));     // empty to
}

TEST("msg: inboxSince delivers direct + broadcast, hides own + others' direct") {
    std::vector<Message> all = {
        mm(100, "A", "B", "to B directly"),
        mm(110, "C", "*", "broadcast"),
        mm(120, "A", "C", "to C, not B"),
        mm(130, "B", "A", "my own (from B)"),
    };
    auto inbox = inboxSince(all, /*me=*/"B", /*since=*/0);
    ASSERT_EQ(inbox.size(), (size_t)2);              // direct-to-B + broadcast
    ASSERT_EQ(inbox[0].body, std::string("to B directly"));
    ASSERT_EQ(inbox[1].body, std::string("broadcast"));
}

TEST("msg: inboxSince respects the cursor (strictly newer)") {
    std::vector<Message> all = {
        mm(100, "A", "B", "old"),
        mm(200, "A", "B", "new"),
    };
    auto inbox = inboxSince(all, "B", 100);          // strictly > 100
    ASSERT_EQ(inbox.size(), (size_t)1);
    ASSERT_EQ(inbox[0].body, std::string("new"));
}
