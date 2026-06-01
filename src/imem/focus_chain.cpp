#include "focus_chain.hpp"
#include <chrono>
#include <stdexcept>

namespace icmg::imem {

// ---- helpers ---------------------------------------------------------------

bool FocusChain::validStatus(const std::string& s) {
    return s == "in" || s == "done" || s == "blocked";
}

FocusItem FocusChain::rowToItem(const core::Row& row) {
    // Columns (SELECT order): id, session_id, todo, status, ord
    FocusItem item;
    if (row.size() > 0 && !row[0].empty()) {
        try { item.id = std::stoll(row[0]); } catch (...) {}
    }
    if (row.size() > 1) item.session_id = row[1];
    if (row.size() > 2) item.todo       = row[2];
    if (row.size() > 3) item.status     = row[3];
    if (row.size() > 4 && !row[4].empty()) {
        try { item.ord = std::stoi(row[4]); } catch (...) {}
    }
    return item;
}

// ---- FocusChain ------------------------------------------------------------

FocusChain::FocusChain(core::Db& db) : db_(db) {}

int64_t FocusChain::add(const std::string& session_id, const std::string& todo) {
    // Compute next ord for this session.
    int next_ord = 1;
    db_.query(
        "SELECT COALESCE(MAX(ord),0) FROM focus_chain WHERE session_id=?",
        {session_id},
        [&](const core::Row& row) {
            if (!row.empty() && !row[0].empty()) {
                try { next_ord = std::stoi(row[0]) + 1; } catch (...) {}
            }
        }
    );

    int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    db_.run(
        "INSERT INTO focus_chain(session_id, todo, status, ord, created_at, updated_at) "
        "VALUES (?, ?, 'in', ?, ?, ?)",
        {session_id, todo, std::to_string(next_ord),
         std::to_string(now), std::to_string(now)}
    );
    return db_.lastInsertId();
}

bool FocusChain::setStatus(int64_t id, const std::string& status) {
    if (!validStatus(status)) return false;

    int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    // Check row exists first.
    bool found = false;
    db_.query(
        "SELECT id FROM focus_chain WHERE id=?",
        {std::to_string(id)},
        [&](const core::Row& row) { if (!row.empty()) found = true; }
    );
    if (!found) return false;

    db_.run(
        "UPDATE focus_chain SET status=?, updated_at=? WHERE id=?",
        {status, std::to_string(now), std::to_string(id)}
    );
    return true;
}

bool FocusChain::removeBySession(const std::string& session_id) {
    int count = 0;
    db_.query(
        "SELECT COUNT(*) FROM focus_chain WHERE session_id=?",
        {session_id},
        [&](const core::Row& row) {
            if (!row.empty() && !row[0].empty()) {
                try { count = std::stoi(row[0]); } catch (...) {}
            }
        }
    );
    if (count == 0) return false;
    db_.run("DELETE FROM focus_chain WHERE session_id=?", {session_id});
    return true;
}

std::vector<FocusItem> FocusChain::list(const std::string& session_id,
                                          const std::string& status_filter,
                                          int limit) const {
    std::vector<FocusItem> result;
    std::string sql;
    std::vector<std::string> params;

    if (status_filter.empty()) {
        sql = "SELECT id, session_id, todo, status, ord "
              "FROM focus_chain WHERE session_id=? "
              "ORDER BY ord ASC LIMIT ?";
        params = {session_id, std::to_string(limit)};
    } else {
        sql = "SELECT id, session_id, todo, status, ord "
              "FROM focus_chain WHERE session_id=? AND status=? "
              "ORDER BY ord ASC LIMIT ?";
        params = {session_id, status_filter, std::to_string(limit)};
    }

    db_.query(sql, params, [&](const core::Row& row) {
        result.push_back(rowToItem(row));
    });
    return result;
}

} // namespace icmg::imem
