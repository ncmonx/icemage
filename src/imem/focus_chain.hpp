#pragma once
// FocusChain — persistent session-scoped todo list stored in focus_chain table.
// Migration 0029 must be applied before use.
//
// Usage:
//   FocusChain fc(db);
//   int64_t id = fc.add("my-session", "fix auth bug");
//   fc.setStatus(id, "done");
//   auto items = fc.list("my-session", "in");

#include "../core/db.hpp"
#include <string>
#include <vector>
#include <cstdint>

namespace icmg::imem {

struct FocusItem {
    int64_t     id         = 0;
    std::string session_id;
    std::string todo;
    std::string status;   // "in" | "done" | "blocked"
    int         ord        = 0;
};

class FocusChain {
public:
    explicit FocusChain(core::Db& db);

    // Append a new todo for the given session. Returns new row id (>0 on success).
    int64_t add(const std::string& session_id, const std::string& todo);

    // Update status. Validates against {"in","done","blocked"}; returns false on
    // invalid enum or unknown id.
    bool setStatus(int64_t id, const std::string& status);

    // Delete all rows for the given session. Returns true if ≥1 row deleted.
    bool removeBySession(const std::string& session_id);

    // List items for session. status_filter="" returns all statuses. limit=100 default.
    std::vector<FocusItem> list(const std::string& session_id,
                                 const std::string& status_filter = "",
                                 int limit = 100) const;

private:
    core::Db& db_;

    static bool validStatus(const std::string& s);
    static FocusItem rowToItem(const core::Row& row);
};

} // namespace icmg::imem
