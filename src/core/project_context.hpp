#pragma once
#include "db.hpp"
#include <string>
#include <memory>

namespace icmg::core {

class ProjectContext {
public:
    // Resolve from --project name (or empty = use CWD).
    // Throws std::runtime_error if named project not registered.
    static ProjectContext resolve(const std::string& project_name = "");

    core::Db& db();
    const std::string& name()     const { return name_; }
    const std::string& rootPath() const { return root_path_; }
    const std::string& dbPath()   const { return db_path_; }
    bool               isRemote() const { return remote_; }

private:
    std::string              name_;
    std::string              root_path_;
    std::string              db_path_;
    bool                     remote_ = false;
    std::unique_ptr<core::Db> db_;
};

} // namespace icmg::core
