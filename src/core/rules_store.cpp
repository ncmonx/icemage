#include "rules_store.hpp"
#include <chrono>
#include <stdexcept>

namespace icmg::core {

static int64_t nowEpoch() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

RulesStore::RulesStore(Db& db) : db_(db) {}

RuleEntry RulesStore::fromRow(const Row& r) {
    RuleEntry e;
    if (r.size() > 0) try { e.id = std::stoll(r[0]); } catch (...) {}
    if (r.size() > 1) e.path    = r[1];
    if (r.size() > 2) e.content = r[2];
    if (r.size() > 3) e.tag     = r[3];
    if (r.size() > 4) try { e.active = r[4] != "0"; } catch (...) {}
    if (r.size() > 5) try { e.updated_at = std::stoll(r[5]); } catch (...) {}
    return e;
}

int64_t RulesStore::upsert(const std::string& path, const std::string& content,
                            const std::string& tag) {
    int64_t ts = nowEpoch();
    db_.run(
        "INSERT INTO rules_bank(path, content, tag, active, updated_at)"
        " VALUES(?,?,?,1,?)"
        " ON CONFLICT(path) DO UPDATE SET"
        "   content=excluded.content,"
        "   tag=excluded.tag,"
        "   updated_at=excluded.updated_at",
        {path, content, tag, std::to_string(ts)}
    );
    return db_.lastInsertId();
}

bool RulesStore::setActive(const std::string& path, bool active) {
    int64_t ts = nowEpoch();
    db_.run(
        "UPDATE rules_bank SET active=?, updated_at=? WHERE path=?",
        {active ? "1" : "0", std::to_string(ts), path}
    );
    return true;
}

std::vector<RuleEntry> RulesStore::list() const {
    std::vector<RuleEntry> rows;
    db_.query(
        "SELECT id,path,content,tag,active,updated_at FROM rules_bank ORDER BY path",
        {},
        [&](const Row& r) { rows.push_back(fromRow(r)); }
    );
    return rows;
}

std::vector<RuleEntry> RulesStore::listActive() const {
    std::vector<RuleEntry> rows;
    db_.query(
        "SELECT id,path,content,tag,active,updated_at FROM rules_bank"
        " WHERE active=1 ORDER BY path",
        {},
        [&](const Row& r) { rows.push_back(fromRow(r)); }
    );
    return rows;
}

} // namespace icmg::core
