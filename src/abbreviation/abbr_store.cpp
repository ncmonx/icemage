#include "abbr_store.hpp"
#include <algorithm>
#include <sstream>
#include <regex>
#include <chrono>

namespace icmg::abbreviation {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static int64_t nowEpoch() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

// Longest common prefix length between two strings (path-based)
static size_t prefixLen(const std::string& scope, const std::string& cwd) {
    if (scope.empty()) return 0;
    size_t n = std::min(scope.size(), cwd.size());
    size_t i = 0;
    while (i < n && scope[i] == cwd[i]) ++i;
    return i;
}

static Abbreviation rowToAbbr(const core::Row& r) {
    Abbreviation a;
    if (r.size() > 0) try { a.id         = std::stoll(r[0]); } catch (...) {}
    if (r.size() > 1)      a.short_form  = r[1];
    if (r.size() > 2)      a.full_form   = r[2];
    if (r.size() > 3)      a.domain      = r[3];
    if (r.size() > 4)      a.scope_path  = r[4];
    if (r.size() > 5) try { a.frequency  = std::stoi(r[5]); } catch (...) {}
    if (r.size() > 6) try { a.created_at = std::stoll(r[6]); } catch (...) {}
    return a;
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

AbbrStore::AbbrStore(core::Db& db) : db_(db) {}

// ---------------------------------------------------------------------------
// learn()
// ---------------------------------------------------------------------------

int64_t AbbrStore::learn(const Abbreviation& abbr, bool update) {
    // Normalise empty domain to ""
    std::string dom = abbr.domain;

    if (update) {
        // Check if exists
        bool exists = false;
        db_.query(
            "SELECT id FROM abbreviations WHERE short_form=? AND domain=?",
            {abbr.short_form, dom},
            [&](const core::Row& r) { if (!r.empty()) exists = true; });

        if (exists) {
            db_.run(
                "UPDATE abbreviations SET full_form=?,scope_path=? "
                "WHERE short_form=? AND domain=?",
                {abbr.full_form,
                 abbr.scope_path,
                 abbr.short_form,
                 dom});
            int64_t rowid = 0;
            db_.query("SELECT id FROM abbreviations WHERE short_form=? AND domain=?",
                      {abbr.short_form, dom},
                      [&](const core::Row& r) { if (!r.empty()) try { rowid = std::stoll(r[0]); } catch (...) {} });
            return rowid;
        }
    }

    try {
        db_.run(
            "INSERT INTO abbreviations(short_form,full_form,domain,scope_path,frequency,created_at) "
            "VALUES(?,?,?,?,0,?)",
            {abbr.short_form, abbr.full_form, dom, abbr.scope_path, std::to_string(nowEpoch())});
        return db_.lastInsertId();
    } catch (const core::DbError& e) {
        std::string msg(e.what());
        if (msg.find("UNIQUE") != std::string::npos) {
            // Find existing full_form for better error message
            std::string existing;
            db_.query("SELECT full_form FROM abbreviations WHERE short_form=? AND domain=?",
                      {abbr.short_form, dom},
                      [&](const core::Row& r) { if (!r.empty()) existing = r[0]; });

            std::string err = "\"" + abbr.short_form + "\" already exists";
            if (!dom.empty()) err += " in domain \"" + dom + "\"";
            err += " (=\"" + existing + "\").\n"
                   "  Use --update to replace, or specify a different --domain.";
            throw AbbrConflictError(err);
        }
        throw;
    }
}

// ---------------------------------------------------------------------------
// remove()
// ---------------------------------------------------------------------------

bool AbbrStore::remove(const std::string& short_form, const std::string& domain) {
    if (domain.empty()) {
        int cnt = 0;
        db_.query("SELECT COUNT(*) FROM abbreviations WHERE short_form=?",
                  {short_form},
                  [&](const core::Row& r) { if (!r.empty()) try { cnt = std::stoi(r[0]); } catch (...) {} });
        if (cnt == 0) return false;
        db_.run("DELETE FROM abbreviations WHERE short_form=?", {short_form});
    } else {
        int cnt = 0;
        db_.query("SELECT COUNT(*) FROM abbreviations WHERE short_form=? AND domain=?",
                  {short_form, domain},
                  [&](const core::Row& r) { if (!r.empty()) try { cnt = std::stoi(r[0]); } catch (...) {} });
        if (cnt == 0) return false;
        db_.run("DELETE FROM abbreviations WHERE short_form=? AND domain=?",
                {short_form, domain});
    }
    return true;
}

// ---------------------------------------------------------------------------
// candidates() — all rows for short_form, priority-sorted
// ---------------------------------------------------------------------------

std::vector<Abbreviation> AbbrStore::candidates(const std::string& short_form,
                                                  const std::string& cwd) const {
    std::vector<Abbreviation> result;
    db_.query(
        "SELECT id,short_form,full_form,domain,scope_path,frequency,created_at "
        "FROM abbreviations WHERE short_form=? ORDER BY id DESC",
        {short_form},
        [&](const core::Row& r) { result.push_back(rowToAbbr(r)); });

    // Sort by: (1) scope prefix match length DESC (2) non-"general" domain first (3) id DESC
    std::stable_sort(result.begin(), result.end(),
        [&](const Abbreviation& a, const Abbreviation& b) {
            size_t pa = prefixLen(a.scope_path, cwd);
            size_t pb = prefixLen(b.scope_path, cwd);
            if (pa != pb) return pa > pb;
            // non-"general" and non-empty domain wins
            bool da = !a.domain.empty() && a.domain != "general";
            bool db_ = !b.domain.empty() && b.domain != "general";
            if (da != db_) return da > db_;
            return a.id > b.id; // id DESC → most recent wins
        });

    return result;
}

// ---------------------------------------------------------------------------
// get()
// ---------------------------------------------------------------------------

std::optional<Abbreviation> AbbrStore::get(const std::string& short_form,
                                             const std::string& cwd) const {
    auto c = candidates(short_form, cwd);
    if (c.empty()) return std::nullopt;
    return c[0];
}

// ---------------------------------------------------------------------------
// expand()
// ---------------------------------------------------------------------------

std::string AbbrStore::expand(const std::string& text, const std::string& cwd) const {
    if (text.empty()) return text;

    // Load all short_forms (deduplicated best-match per short_form)
    std::vector<std::string> shorts;
    db_.query("SELECT DISTINCT short_form FROM abbreviations ORDER BY length(short_form) DESC",
              {},
              [&](const core::Row& r) { if (!r.empty()) shorts.push_back(r[0]); });

    std::string result = text;
    for (auto& sf : shorts) {
        auto best = get(sf, cwd);
        if (!best) continue;
        // Whole-word replacement (word boundary: non-alphanumeric or start/end)
        try {
            std::regex re("\\b" + std::regex_replace(sf, std::regex(R"([-[\]{}()*+?.,\\^$|#\s])"), R"(\$&)") + "\\b");
            result = std::regex_replace(result, re, best->full_form);
        } catch (...) {
            // Malformed short_form — skip silently
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// list()
// ---------------------------------------------------------------------------

std::vector<Abbreviation> AbbrStore::list(const std::string& domain) const {
    std::vector<Abbreviation> result;
    if (domain.empty()) {
        db_.query(
            "SELECT id,short_form,full_form,domain,scope_path,frequency,created_at "
            "FROM abbreviations ORDER BY short_form, domain",
            {},
            [&](const core::Row& r) { result.push_back(rowToAbbr(r)); });
    } else {
        db_.query(
            "SELECT id,short_form,full_form,domain,scope_path,frequency,created_at "
            "FROM abbreviations WHERE domain=? ORDER BY short_form",
            {domain},
            [&](const core::Row& r) { result.push_back(rowToAbbr(r)); });
    }
    return result;
}

// ---------------------------------------------------------------------------
// search()
// ---------------------------------------------------------------------------

std::vector<Abbreviation> AbbrStore::search(const std::string& query) const {
    std::string like = "%" + query + "%";
    std::vector<Abbreviation> result;
    db_.query(
        "SELECT id,short_form,full_form,domain,scope_path,frequency,created_at "
        "FROM abbreviations WHERE short_form LIKE ? OR full_form LIKE ? "
        "ORDER BY frequency DESC, short_form",
        {like, like},
        [&](const core::Row& r) { result.push_back(rowToAbbr(r)); });
    return result;
}

// ---------------------------------------------------------------------------
// bumpFrequency()
// ---------------------------------------------------------------------------

void AbbrStore::bumpFrequency(const std::string& short_form, const std::string& domain) {
    if (domain.empty()) {
        db_.run("UPDATE abbreviations SET frequency=frequency+1 WHERE short_form=?",
                {short_form});
    } else {
        db_.run("UPDATE abbreviations SET frequency=frequency+1 WHERE short_form=? AND domain=?",
                {short_form, domain});
    }
}

} // namespace icmg::abbreviation
