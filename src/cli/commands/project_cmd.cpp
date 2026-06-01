#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/global_db.hpp"
#include "../../core/project_context.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include <iostream>
#include <iomanip>
#include <filesystem>
#include <chrono>

namespace icmg::cli {

class ProjectCommand : public BaseCommand {
public:
    std::string name()        const override { return "project"; }
    std::string description() const override { return "Multi-project registry"; }

    void usage() const override {
        std::cout <<
            "Usage: icmg project <subcommand> [options]\n\n"
            "Subcommands:\n"
            "  add <name> <path> [--description <desc>]\n"
            "      Register a project. path: absolute path to project root.\n"
            "  list [--json]\n"
            "  remove <name>\n"
            "  info <name>\n"
            "  current          Show active project from CWD\n\n"
            "Cross-project query (any command):\n"
            "  icmg recall \"query\" --project <name>\n"
            "  icmg data list   --project <name>\n"
            "  icmg rule list   --project <name>\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || args[0] == "--help" || args[0] == "-h") {
            usage(); return 0;
        }

        // Ensure global DB initialized
        auto& gdb = core::GlobalDb::instance();
        gdb.init();

        const std::string& sub = args[0];
        if (sub == "add")     return doAdd(gdb, args);
        if (sub == "list")    return doList(gdb, args);
        if (sub == "remove")  return doRemove(gdb, args);
        if (sub == "info")    return doInfo(gdb, args);
        if (sub == "current") return doCurrent();

        std::cerr << "icmg project: unknown subcommand '" << sub << "'\n";
        usage(); return 1;
    }

private:
    // ---- add ---------------------------------------------------------------
    int doAdd(core::GlobalDb& gdb, const std::vector<std::string>& args) {
        std::string desc = flagValue(args, "--description");
        if (desc.empty()) desc = flagValue(args, "--desc");

        std::vector<std::string> pos;
        for (size_t i = 1; i < args.size(); ++i) {
            const auto& a = args[i];
            if (a == "--description" || a == "--desc") { ++i; continue; }
            if (!a.empty() && a[0] == '-') continue;
            pos.push_back(a);
        }

        if (pos.size() < 2) {
            std::cerr << "icmg project add: requires <name> <path>\n"; return 1;
        }

        core::Project p;
        p.name        = pos[0];
        p.path        = pos[1];
        p.description = desc;

        try {
            int64_t id = gdb.addProject(p);
            // Re-fetch to show canonical paths
            auto stored = gdb.getProject(p.name);
            std::cout << "Registered project '" << p.name << "' (id=" << id << ")\n";
            if (stored) {
                std::cout << "  Path:    " << stored->path    << "\n"
                          << "  DB:      " << stored->db_path << "\n";
            }
        } catch (const core::SecurityError& e) {
            std::cerr << "Security error: " << e.what() << "\n"; return 1;
        } catch (const std::runtime_error& e) {
            std::cerr << e.what() << "\n"; return 1;
        }
        return 0;
    }

    // ---- list --------------------------------------------------------------
    int doList(core::GlobalDb& gdb, const std::vector<std::string>& args) {
        bool json_out = hasFlag(args, "--json");
        auto projects = gdb.listProjects();

        // Detect current project
        std::string cur_path;
        try {
            auto ctx = core::ProjectContext::resolve();
            cur_path = ctx.rootPath();
            // normalise
            for (char& c : cur_path) if (c == '\\') c = '/';
        } catch (...) {}

        if (json_out) {
            std::cout << "[\n";
            for (size_t i = 0; i < projects.size(); ++i) {
                auto& p = projects[i];
                std::cout << "  {\"name\":\"" << escJ(p.name) << "\""
                    << ",\"path\":\"" << escJ(p.path) << "\""
                    << ",\"db_path\":\"" << escJ(p.db_path) << "\""
                    << ",\"description\":\"" << escJ(p.description) << "\"}";
                if (i + 1 < projects.size()) std::cout << ",";
                std::cout << "\n";
            }
            std::cout << "]\n";
        } else {
            if (projects.empty()) { std::cout << "(no registered projects)\n"; return 0; }
            std::cout << std::left
                      << std::setw(16) << "NAME"
                      << std::setw(40) << "PATH"
                      << "DB\n"
                      << std::string(80, '-') << "\n";
            for (auto& p : projects) {
                std::string pp = p.path;
                for (char& c : pp) if (c == '\\') c = '/';
                bool is_cur = (!cur_path.empty() && pp == cur_path);

                std::cout << (is_cur ? "* " : "  ");
                std::cout << std::setw(14) << p.name;
                std::string short_path = p.path.size() > 38
                    ? "..." + p.path.substr(p.path.size() - 35)
                    : p.path;
                std::cout << std::setw(40) << short_path;
                std::cout << p.db_path;
                if (is_cur) std::cout << "  (current)";
                std::cout << "\n";
            }
        }
        return 0;
    }

    // ---- remove ------------------------------------------------------------
    int doRemove(core::GlobalDb& gdb, const std::vector<std::string>& args) {
        if (args.size() < 2) { std::cerr << "icmg project remove: requires <name>\n"; return 1; }
        gdb.removeProject(args[1]);
        std::cout << "Removed project: " << args[1] << "\n";
        return 0;
    }

    // ---- info --------------------------------------------------------------
    int doInfo(core::GlobalDb& gdb, const std::vector<std::string>& args) {
        if (args.size() < 2) { std::cerr << "icmg project info: requires <name>\n"; return 1; }
        auto p = gdb.getProject(args[1]);
        if (!p) { std::cerr << "Project not found: " << args[1] << "\n"; return 1; }

        std::cout << "Name:        " << p->name        << "\n"
                  << "Path:        " << p->path        << "\n"
                  << "DB:          " << p->db_path     << "\n"
                  << "Description: " << p->description << "\n";

        // Try to open DB and show quick stats
        try {
            core::Db db(p->db_path);
            int mem_count = 0, node_count = 0;
            db.query("SELECT COUNT(*) FROM memory_nodes WHERE deleted_at IS NULL", {},
                     [&](const core::Row& r) { if (!r.empty()) try { mem_count = std::stoi(r[0]); } catch (...) {} });
            db.query("SELECT COUNT(*) FROM graph_nodes", {},
                     [&](const core::Row& r) { if (!r.empty()) try { node_count = std::stoi(r[0]); } catch (...) {} });
            std::cout << "Memory nodes: " << mem_count  << "\n"
                      << "Graph nodes:  " << node_count << "\n";
        } catch (...) {
            std::cout << "(DB not accessible — project may not be initialized)\n";
        }
        return 0;
    }

    // ---- current -----------------------------------------------------------
    int doCurrent() {
        try {
            auto ctx = core::ProjectContext::resolve();
            std::cout << "Current project: " << ctx.name()     << "\n"
                      << "Root:            " << ctx.rootPath() << "\n"
                      << "DB:              " << ctx.dbPath()   << "\n"
                      << "Remote:          " << (ctx.isRemote() ? "yes" : "no") << "\n";
        } catch (const std::exception& e) {
            std::cerr << e.what() << "\n"; return 1;
        }
        return 0;
    }

    static std::string escJ(const std::string& s) {
        std::string out;
        for (char c : s) {
            if      (c == '"')  out += "\\\"";
            else if (c == '\\') out += "\\\\";
            else                out += c;
        }
        return out;
    }
};

ICMG_REGISTER_COMMAND("project", ProjectCommand);

} // namespace icmg::cli
