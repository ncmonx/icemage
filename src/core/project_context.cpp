#include "project_context.hpp"
#include "global_db.hpp"
#include "config.hpp"
#include "path_utils.hpp"
#include <filesystem>
#include <stdexcept>

namespace fs = std::filesystem;
namespace icmg::core {

ProjectContext ProjectContext::resolve(const std::string& project_name) {
    ProjectContext ctx;

    if (project_name.empty()) {
        // Auto-detect: walk up from CWD looking for .icmg/data.db
        fs::path cwd = fs::current_path();
        fs::path cur = cwd;
        while (true) {
            fs::path candidate = cur / ".icmg" / "data.db";
            std::error_code ec;
            if (fs::exists(candidate, ec)) {
                ctx.root_path_ = cur.string();
                ctx.db_path_   = candidate.string();
                ctx.name_      = cur.filename().string();
                ctx.remote_    = false;
                return ctx;
            }
            fs::path parent = cur.parent_path();
            if (parent == cur) break; // reached filesystem root
            cur = parent;
        }
        // Fallback: use CWD (db may not exist yet — ensureProjectDb handles it)
        ctx.root_path_ = cwd.string();
        ctx.db_path_   = Config::instance().projectDbPath(cwd.string());
        ctx.name_      = cwd.filename().string();
        ctx.remote_    = false;
        return ctx;
    }

    // Named project: look up in global registry
    auto& gdb = GlobalDb::instance();
    gdb.init();
    auto proj = gdb.getProject(project_name);
    if (!proj) {
        throw std::runtime_error(
            "Project '" + project_name + "' not registered. "
            "Run: icmg project add " + project_name + " <path>");
    }

    ctx.name_      = proj->name;
    ctx.root_path_ = proj->path;
    ctx.db_path_   = proj->db_path;
    ctx.remote_    = true;
    return ctx;
}

core::Db& ProjectContext::db() {
    if (!db_) db_ = std::make_unique<core::Db>(db_path_);
    return *db_;
}

} // namespace icmg::core
