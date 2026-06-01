#pragma once
#include "abbreviation.hpp"
#include "../core/db.hpp"
#include <optional>
#include <vector>
#include <string>

namespace icmg::abbreviation {

class AbbrConflictError : public std::runtime_error {
public:
    explicit AbbrConflictError(const std::string& msg) : std::runtime_error(msg) {}
};

class AbbrStore {
public:
    explicit AbbrStore(core::Db& db);

    // Learn an abbreviation. Throws AbbrConflictError on duplicate (short_form+domain)
    // unless update=true (upsert).
    int64_t learn(const Abbreviation& abbr, bool update = false);

    // Remove by short_form (+ optional domain filter).
    bool remove(const std::string& short_form, const std::string& domain = "");

    // Expand all known abbreviations in text (whole-word replacement).
    // cwd used for scope_path priority.
    std::string expand(const std::string& text, const std::string& cwd = "") const;

    // Look up a single abbreviation (best match by priority, given cwd).
    std::optional<Abbreviation> get(const std::string& short_form,
                                    const std::string& cwd = "") const;

    // List all abbreviations, optionally filtered by domain.
    std::vector<Abbreviation> list(const std::string& domain = "") const;

    // Search by short_form or full_form substring.
    std::vector<Abbreviation> search(const std::string& query) const;

    // Increment frequency counter.
    void bumpFrequency(const std::string& short_form, const std::string& domain = "");

private:
    core::Db& db_;

    // Priority order: longest scope_path prefix match → non-"general" domain → id DESC
    std::vector<Abbreviation> candidates(const std::string& short_form,
                                         const std::string& cwd) const;
};

} // namespace icmg::abbreviation
