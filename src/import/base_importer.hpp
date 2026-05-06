#pragma once
#include "../core/db.hpp"
#include <string>
#include <vector>
#include <stdexcept>

namespace icmg {

struct ImportStats {
    int memory_nodes  = 0;
    int graph_nodes   = 0;
    int graph_edges   = 0;
    int abbreviations = 0;
    int stored_procs  = 0;
    int rules         = 0;
    int errors        = 0;
    std::vector<std::string> error_messages;
};

class ImportError : public std::runtime_error {
public:
    explicit ImportError(const std::string& msg) : std::runtime_error(msg) {}
};

class ValidationError : public std::runtime_error {
public:
    explicit ValidationError(const std::string& msg) : std::runtime_error(msg) {}
};

class BaseImporter {
public:
    virtual ~BaseImporter() = default;

    // Main entry: wraps doImport in a transaction (A1).
    ImportStats import(const std::string& source,
                       core::Db& target_db,
                       const std::string& project_name = "") {
        target_db.run("BEGIN TRANSACTION");
        try {
            auto stats = doImport(source, target_db, project_name);
            target_db.run("COMMIT");
            return stats;
        } catch (...) {
            target_db.run("ROLLBACK");
            throw;
        }
    }

    virtual std::string name()        const = 0;
    virtual std::string description() const = 0;

protected:
    virtual ImportStats doImport(const std::string& source,
                                  core::Db& target_db,
                                  const std::string& project_name) = 0;

    // A7: null-byte check
    static void validateString(const std::string& s, const std::string& field) {
        if (s.find('\0') != std::string::npos)
            throw ValidationError("Field '" + field + "' contains null byte");
    }

    // A3: check SQLite magic bytes
    static bool isSQLiteFile(const std::string& path);

    // A4: 100MB size limit
    static void checkFileSize(const std::string& path);
};

} // namespace icmg
