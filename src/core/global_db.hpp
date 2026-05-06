#pragma once
#include "db.hpp"
#include <string>
#include <vector>
#include <optional>
#include <memory>
#include <stdexcept>

namespace icmg::core {

struct Project {
    int64_t     id            = 0;
    std::string name;
    std::string path;
    std::string db_path;
    std::string description;
    int64_t     registered_at = 0;
};

struct SecurityError : std::runtime_error {
    explicit SecurityError(const std::string& msg) : std::runtime_error(msg) {}
};

class GlobalDb {
public:
    static GlobalDb& instance();

    // Create/open ~/.icmg/global.db, run migrations (A3)
    void init();

    // A1: verifies ownership + path safety before insert
    int64_t addProject(const Project& p);

    bool removeProject(const std::string& name);
    std::optional<Project> getProject(const std::string& name) const;
    std::vector<Project> listProjects() const;
    bool projectExists(const std::string& name) const;

    core::Db& db();

private:
    GlobalDb() = default;
    std::unique_ptr<core::Db> db_;

    void runGlobalMigrations();
    static Project fromRow(const core::Row& r);
};

} // namespace icmg::core
