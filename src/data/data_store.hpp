#pragma once
#include "structured_data.hpp"
#include "../core/db.hpp"
#include <vector>
#include <optional>
#include <string>

namespace icmg::data {

class DataStore {
public:
    explicit DataStore(core::Db& db);

    // Add new entry. Returns new id. Throws if name already exists.
    int64_t add(const StructuredData& d);

    // Update content, auto-version. Saves old content to data_versions.
    // Returns false if name not found.
    bool update(const std::string& name, const std::string& content,
                const std::string& note = "");

    bool remove(const std::string& name);

    std::optional<StructuredData> get(const std::string& name) const;
    std::optional<StructuredData> getById(int64_t id) const;

    // List with optional filters
    std::vector<StructuredData> list(const std::string& type  = "",
                                     const std::string& scope = "") const;

    // BM25-style keyword search on name+content+tags
    std::vector<StructuredData> search(const std::string& query, int limit = 10) const;

    // Version history for an entry
    std::vector<DataVersion> history(const std::string& name) const;

    // Revert to a specific version
    bool revert(const std::string& name, const std::string& to_version);

    // Structured data with scope_path that is a prefix of file_path
    std::vector<StructuredData> forFile(const std::string& file_path) const;

private:
    core::Db& db_;

    static StructuredData fromRow(const core::Row& r);
    static DataVersion    versionFromRow(const core::Row& r);
    static std::string    bumpVersion(const std::string& v);
};

} // namespace icmg::data
