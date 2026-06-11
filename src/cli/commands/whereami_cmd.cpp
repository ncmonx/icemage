// `icmg whereami` — one authoritative "ground truth" snapshot.
//
// Born from this session's friction (observability gap): time was lost to a
// stale ~/bin binary reporting an old version, config persisting to %APPDATA%
// while ~/.icmg was being inspected, and which-binary-is-on-PATH ambiguity.
// This prints — in one place — the running binary + version, the config path
// actually used, the DB paths, and the persona DB. No more guessing where the
// truth lives. Pure formatting in ../whereami_render.hpp (unit-tested).

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/path_utils.hpp"
#include "../../core/version.hpp"
#include "../whereami_render.hpp"
#include <filesystem>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace icmg::cli {

class WhereAmICommand : public BaseCommand {
public:
    std::string name()        const override { return "whereami"; }
    std::string description() const override {
        return "Ground-truth snapshot: running binary + version, config + DB paths";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg whereami\n\n"
            "Prints the authoritative locations icmg is actually using this run:\n"
            "running binary + version, config file, project/global/persona DBs.\n"
            "Use it when something behaves unexpectedly (wrong version, config not\n"
            "persisting, stale binary) before guessing.\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (hasFlag(args, "--help")) { usage(); return 0; }

        auto& cfg = core::Config::instance();
        std::error_code ec;
        std::string exe = core::selfExePath();
        std::string cfgPath = (fs::path(core::icmgGlobalDir()) / "config.json").string();

        std::vector<std::pair<std::string, std::string>> rows = {
            {"binary",     exe},
            {"version",    core::ICMG_VERSION},
            {"global-dir", core::icmgGlobalDir()},
            {"config",     cfgPath + (fs::exists(cfgPath, ec) ? "" : "  (not created yet)")},
            {"global-db",  cfg.globalDbPath()},
            {"project-db", cfg.projectDbPath(".")},
            {"persona-db", core::personaDbPath()},
            {"cwd",        fs::current_path(ec).string()},
        };
        std::cout << renderWhereAmI(rows);
        return 0;
    }
};

ICMG_REGISTER_COMMAND("whereami", WhereAmICommand);

}  // namespace icmg::cli
