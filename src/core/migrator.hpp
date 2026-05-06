#pragma once
#include "db.hpp"
#include <string>

namespace icmg::core {

class Migrator {
public:
    // migrations_dir: directory containing NNNN_*.sql files.
    // Defaults to "migrations/" relative to executable.
    explicit Migrator(const std::string& migrations_dir = "migrations");

    // Run all pending migrations (version > db.userVersion()).
    // Each migration wrapped in BEGIN/COMMIT transaction.
    // Throws on failure (with ROLLBACK).
    void runAll(Db& db);

    int latestVersion() const;

private:
    std::string dir_;

    struct Migration {
        int         version;
        std::string path;
    };

    std::vector<Migration> discover() const;
    void apply(Db& db, const Migration& m);
};

} // namespace icmg::core
