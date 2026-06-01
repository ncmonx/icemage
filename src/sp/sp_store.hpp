#pragma once
#include "stored_procedure.hpp"
#include "../core/db.hpp"
#include <optional>
#include <vector>
#include <string>

namespace icmg::sp {

class SpStore {
public:
    explicit SpStore(core::Db& db);

    // Add new SP. Bumps version if already exists (upsert).
    int64_t add(const StoredProcedure& sp);

    // Update SQL content, saving old version to history.
    bool update(const std::string& name, const std::string& sql,
                const std::string& note = "");

    bool remove(const std::string& name);

    std::optional<StoredProcedure> get(const std::string& name) const;

    std::vector<StoredProcedure> list(
        const std::string& db_type    = "",
        const std::string& database   = "") const;

    // Full-text search: name + context + tables_used + tags
    std::vector<StoredProcedure> search(const std::string& query,
                                        int limit = 10) const;

    // SPs that use a given table
    std::vector<StoredProcedure> usesTable(const std::string& table) const;

    // SPs that call a given SP
    std::vector<StoredProcedure> calledBy(const std::string& sp_name) const;

    // Version history for a given SP
    std::vector<SpVersion> history(const std::string& name) const;

private:
    core::Db& db_;

    StoredProcedure rowToSp(const core::Row& r) const;
    SpVersion       rowToVer(const core::Row& r) const;

    static std::string jsonArray(const std::vector<std::string>& v);
    static std::vector<std::string> parseJsonArray(const std::string& json);
    static std::string jsonParams(const std::vector<SpParameter>& v);
    static std::vector<SpParameter> parseJsonParams(const std::string& json);
};

} // namespace icmg::sp
