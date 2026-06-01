#include "rule_store.hpp"
#include <optional>
#include <stdexcept>

namespace icmg::rules {

RuleStore::RuleStore(core::Db& db) : db_(db) {}

// ---- helpers ---------------------------------------------------------------

Rule RuleStore::fromRow(const core::Row& r) {
    Rule rule;
    if (r.size() > 0) try { rule.id         = std::stoll(r[0]); } catch (...) {}
    if (r.size() > 1) rule.scope_path = r[1];
    if (r.size() > 2) rule.rule_type  = r[2];
    if (r.size() > 3) rule.name       = r[3];
    if (r.size() > 4) rule.content    = r[4];
    if (r.size() > 5) try { rule.priority   = std::stoi(r[5]); } catch (...) {}
    if (r.size() > 6) try { rule.active     = (std::stoi(r[6]) != 0); } catch (...) {}
    if (r.size() > 7) try { rule.created_at      = std::stoll(r[7]); } catch (...) {}
    if (r.size() > 8) try { rule.supersedes_id   = std::stoll(r[8]); } catch (...) {}
    if (r.size() > 9) try { rule.trial_mode      = std::stoi(r[9]);  } catch (...) {}
    if (r.size() > 10) try { rule.trial_prompts  = std::stoi(r[10]); } catch (...) {}
    if (r.size() > 11) try { rule.trial_threshold= std::stoi(r[11]); } catch (...) {}
    return rule;
}

// ---- add -------------------------------------------------------------------

int64_t RuleStore::add(const Rule& rule, bool update) {
    // Normalize scope_path: must end with "/" or be "/"
    std::string scope = rule.scope_path;
    if (scope.empty()) scope = "/";
    if (scope != "/" && scope.back() != '/') scope += '/';

    try {
        if (update) {
            db_.run(
                "INSERT INTO rules(scope_path,rule_type,name,content,priority,active,"
                " created_at)"
                " VALUES(?,?,?,?,?,1,(strftime('%s','now')))"
                " ON CONFLICT(scope_path,rule_type,name) DO UPDATE SET"
                " content=excluded.content,"
                " priority=excluded.priority,"
                " active=1",
                {scope, rule.rule_type, rule.name, rule.content,
                 std::to_string(rule.priority)});
        } else {
            db_.run(
                "INSERT INTO rules(scope_path,rule_type,name,content,priority,active,"
                " created_at)"
                " VALUES(?,?,?,?,?,1,(strftime('%s','now')))",
                {scope, rule.rule_type, rule.name, rule.content,
                 std::to_string(rule.priority)});
        }
    } catch (const std::runtime_error& e) {
        std::string msg = e.what();
        if (msg.find("UNIQUE") != std::string::npos) {
            throw RuleConflictError(
                "Rule already exists: " + scope + " / " + rule.rule_type +
                " / " + rule.name + ". Use --update to overwrite.");
        }
        throw;
    }

    int64_t id = 0;
    db_.query("SELECT last_insert_rowid()", {}, [&](const core::Row& r) {
        if (!r.empty()) try { id = std::stoll(r[0]); } catch (...) {}
    });
    return id;
}

// ---- remove / activate -----------------------------------------------------

bool RuleStore::remove(int64_t id) {
    db_.run("DELETE FROM rules WHERE id=?", {std::to_string(id)});
    return true;
}

bool RuleStore::setActive(int64_t id, bool active) {
    db_.run("UPDATE rules SET active=? WHERE id=?",
            {active ? "1" : "0", std::to_string(id)});
    return true;
}

// ---- queries ---------------------------------------------------------------

std::vector<Rule> RuleStore::all() const {
    std::vector<Rule> result;
    db_.query(
        "SELECT id,scope_path,rule_type,name,content,priority,active,created_at,"
        "COALESCE(supersedes_id,0),trial_mode,trial_prompts,trial_threshold"
        " FROM rules ORDER BY scope_path,priority,id",
        {},
        [&](const core::Row& r) { result.push_back(fromRow(r)); });
    return result;
}

std::vector<Rule> RuleStore::forPath(const std::string& path) const {
    // Normalize query path: add trailing "/" so "." matches "./" scope
    std::string norm = path;
    if (!norm.empty() && norm.back() != '/') norm += '/';

    // Load all active rules; filter + sort in C++ (A3 trie optimises later)
    std::vector<Rule> result;
    db_.query(
        "SELECT id,scope_path,rule_type,name,content,priority,active,created_at,"
        "COALESCE(supersedes_id,0),trial_mode,trial_prompts,trial_threshold"
        " FROM rules WHERE active=1 ORDER BY length(scope_path),priority,id",
        {},
        [&](const core::Row& r) {
            Rule rule = fromRow(r);
            // Match: file_path starts_with scope_path, or scope="/"
            if (rule.scope_path == "/" || norm.find(rule.scope_path) == 0)
                result.push_back(rule);
        });
    return result;
}

std::optional<Rule> RuleStore::get(int64_t id) const {
    std::optional<Rule> result;
    db_.query(
        "SELECT id,scope_path,rule_type,name,content,priority,active,created_at,"
        "COALESCE(supersedes_id,0),trial_mode,trial_prompts,trial_threshold"
        " FROM rules WHERE id=?",
        {std::to_string(id)},
        [&](const core::Row& r) { result = fromRow(r); });
    return result;
}

// ---- trial / supersession --------------------------------------------------

bool RuleStore::supersede(int64_t new_id, int64_t old_id, int trial_threshold) {
    auto old_rule = get(old_id);
    auto new_rule = get(new_id);
    if (!old_rule || !new_rule) return false;

    // Lower old rule priority (subtract 100) so new one wins on conflict checks
    int new_priority = old_rule->priority - 100;
    db_.run("UPDATE rules SET priority=? WHERE id=?",
            {std::to_string(new_priority), std::to_string(old_id)});

    // Mark new rule as trial, link it to old
    db_.run("UPDATE rules SET trial_mode=1, trial_prompts=0, trial_threshold=?, "
            "supersedes_id=? WHERE id=?",
            {std::to_string(trial_threshold), std::to_string(old_id),
             std::to_string(new_id)});
    return true;
}

int RuleStore::trialTick() {
    int confirmed = 0;
    // Get all trial rules
    std::vector<Rule> trial_rules;
    db_.query(
        "SELECT id,scope_path,rule_type,name,content,priority,active,created_at,"
        "COALESCE(supersedes_id,0),trial_mode,trial_prompts,trial_threshold"
        " FROM rules WHERE trial_mode=1",
        {}, [&](const core::Row& r) { trial_rules.push_back(fromRow(r)); });

    for (auto& tr : trial_rules) {
        int new_prompts = tr.trial_prompts + 1;
        if (new_prompts >= tr.trial_threshold && tr.supersedes_id > 0) {
            // Auto-confirm: delete old (superseded) rule, mark new as confirmed
            db_.run("DELETE FROM rules WHERE id=?",
                    {std::to_string(tr.supersedes_id)});
            db_.run("UPDATE rules SET trial_mode=0, supersedes_id=NULL, "
                    "trial_prompts=? WHERE id=?",
                    {std::to_string(new_prompts), std::to_string(tr.id)});
            ++confirmed;
        } else {
            db_.run("UPDATE rules SET trial_prompts=? WHERE id=?",
                    {std::to_string(new_prompts), std::to_string(tr.id)});
        }
    }
    return confirmed;
}

bool RuleStore::revert(int64_t new_rule_id) {
    auto new_rule = get(new_rule_id);
    if (!new_rule || new_rule->trial_mode == 0) return false;

    if (new_rule->supersedes_id > 0) {
        // Restore old rule's priority (add back the 100 we subtracted)
        auto old_rule = get(new_rule->supersedes_id);
        if (old_rule) {
            db_.run("UPDATE rules SET priority=? WHERE id=?",
                    {std::to_string(old_rule->priority + 100),
                     std::to_string(new_rule->supersedes_id)});
        }
    }
    // Delete the trial rule
    db_.run("DELETE FROM rules WHERE id=?", {std::to_string(new_rule_id)});
    return true;
}

std::vector<Rule> RuleStore::trials() const {
    std::vector<Rule> result;
    db_.query(
        "SELECT id,scope_path,rule_type,name,content,priority,active,created_at,"
        "COALESCE(supersedes_id,0),trial_mode,trial_prompts,trial_threshold"
        " FROM rules WHERE trial_mode=1 ORDER BY id",
        {}, [&](const core::Row& r) { result.push_back(fromRow(r)); });
    return result;
}

} // namespace icmg::rules
