#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../core/zone_resolver.hpp"
#include <iostream>
#include <iomanip>
#include <string>
#include <vector>

namespace icmg::cli {

// =============================================================================
// Subcommands
// =============================================================================

class ZoneListCommand : public BaseCommand {
public:
    std::string name()        const override { return "zone-list"; }
    std::string description() const override { return "List all zones with node counts"; }

    int run(const std::vector<std::string>& args) override {
        bool json_out = false;
        for (auto& a : args) if (a == "--json") json_out = true;

        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        core::ZoneResolver z(db);
        auto zones = z.listZones();

        // Per-zone graph node count
        struct Stat { std::string zone; std::string glob; int nodes = 0; };
        std::vector<Stat> stats;
        for (auto& [name, glob] : zones) {
            Stat s; s.zone = name; s.glob = glob;
            db.query("SELECT COUNT(*) FROM graph_nodes WHERE zone = ?", {name},
                     [&](const core::Row& r){
                         if (!r.empty()) try { s.nodes = std::stoi(r[0]); } catch(...){}
                     });
            stats.push_back(s);
        }

        if (json_out) {
            std::cout << "[";
            for (size_t i = 0; i < stats.size(); ++i) {
                if (i) std::cout << ",";
                std::cout << "{\"zone\":\"" << stats[i].zone
                          << "\",\"glob\":\"" << stats[i].glob
                          << "\",\"nodes\":" << stats[i].nodes << "}";
            }
            std::cout << "]\n";
            return 0;
        }

        std::cout << "Zones (" << stats.size() << "):\n\n";
        std::cout << "  " << std::left << std::setw(16) << "ZONE"
                  << std::setw(8) << "NODES" << "GLOB\n";
        for (auto& s : stats) {
            std::cout << "  " << std::left << std::setw(16) << s.zone
                      << std::setw(8) << s.nodes
                      << s.glob << "\n";
        }
        return 0;
    }
};

class ZoneAddCommand : public BaseCommand {
public:
    std::string name()        const override { return "zone-add"; }
    std::string description() const override { return "Add path glob → zone mapping"; }

    int run(const std::vector<std::string>& args) override {
        if (args.size() < 2) {
            std::cerr << "icmg zone add: requires <name> <glob>\n";
            return 1;
        }
        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        core::ZoneResolver z(db);
        z.addRule(args[0], args[1]);
        std::cout << "Zone '" << args[0] << "' → '" << args[1] << "' added.\n";
        return 0;
    }
};

class ZoneRemoveCommand : public BaseCommand {
public:
    std::string name()        const override { return "zone-rm"; }
    std::string description() const override { return "Remove a zone definition"; }

    int run(const std::vector<std::string>& args) override {
        if (args.empty()) {
            std::cerr << "icmg zone rm: requires <name>\n";
            return 1;
        }
        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        core::ZoneResolver z(db);
        z.removeZone(args[0]);
        std::cout << "Zone '" << args[0] << "' removed (existing nodes keep tag until rebuild).\n";
        return 0;
    }
};

class ZoneAssignCommand : public BaseCommand {
public:
    std::string name()        const override { return "zone-assign"; }
    std::string description() const override { return "Bulk re-tag nodes matching glob"; }

    int run(const std::vector<std::string>& args) override {
        if (args.size() < 2) {
            std::cerr << "icmg zone assign: requires <glob> <zone>\n";
            return 1;
        }
        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        core::ZoneResolver z(db);
        int n = z.assign(args[0], args[1]);
        std::cout << "Re-tagged " << n << " node(s) as '" << args[1] << "'.\n";
        return 0;
    }
};

class ZoneRebuildCommand : public BaseCommand {
public:
    std::string name()        const override { return "zone-rebuild"; }
    std::string description() const override { return "Re-tag every node from current rules"; }

    int run(const std::vector<std::string>&) override {
        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        core::ZoneResolver z(db);
        int n = z.rebuild();
        std::cout << "Updated " << n << " node(s) to match current zone rules.\n";
        return 0;
    }
};

class ZoneResolveCommand : public BaseCommand {
public:
    std::string name()        const override { return "zone-resolve"; }
    std::string description() const override { return "Debug: show which zone a path resolves to"; }

    int run(const std::vector<std::string>& args) override {
        if (args.empty()) {
            std::cerr << "icmg zone resolve: requires <path>\n";
            return 1;
        }
        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        core::ZoneResolver z(db);
        std::cout << z.resolveForPath(args[0]) << "\n";
        return 0;
    }
};

class ZoneShowCommand : public BaseCommand {
public:
    std::string name()        const override { return "zone-show"; }
    std::string description() const override { return "Show zone members"; }

    int run(const std::vector<std::string>& args) override {
        if (args.empty()) {
            std::cerr << "icmg zone show: requires <name>\n";
            return 1;
        }
        int limit = 50;
        for (size_t i = 0; i < args.size(); ++i) {
            if (args[i] == "--limit" && i + 1 < args.size()) {
                try { limit = std::stoi(args[++i]); } catch(...){}
            }
        }
        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));

        std::cout << "Zone '" << args[0] << "' members (limit " << limit << "):\n\n";
        int n = 0;
        db.query("SELECT path, lang FROM graph_nodes WHERE zone = ? ORDER BY path LIMIT ?",
                 {args[0], std::to_string(limit)},
                 [&](const core::Row& r) {
                     if (r.size() >= 2) {
                         std::cout << "  [" << r[1] << "] " << r[0] << "\n";
                         ++n;
                     }
                 });
        if (n == 0) std::cout << "  (no members)\n";
        return 0;
    }
};

// =============================================================================
// Root dispatcher
// =============================================================================

class ZoneRootCommand : public BaseCommand {
public:
    std::string name()        const override { return "zone"; }
    std::string description() const override { return "Zone management (partition graph + memory by subsystem)"; }

    void usage() const override {
        std::cout <<
            "Usage: icmg zone <subcommand> [options]\n\n"
            "Subcommands:\n"
            "  list [--json]                    List zones + node counts\n"
            "  show <name> [--limit N]          Show zone members\n"
            "  add <name> <glob>                Add path-glob → zone rule\n"
            "  rm <name>                        Remove zone definition\n"
            "  assign <glob> <zone>             Bulk re-tag matching nodes\n"
            "  rebuild                          Re-tag every node from current rules\n"
            "  resolve <path>                   Debug: which zone for this path?\n"
            "\n"
            "Use --zone <name> with `recall`, `graph`, `viz` to scope queries.\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || args[0] == "--help" || args[0] == "-h") {
            usage(); return 0;
        }
        std::string sub = args[0];
        std::vector<std::string> rest(args.begin() + 1, args.end());

        std::string registered;
        if      (sub == "list")    registered = "zone-list";
        else if (sub == "show")    registered = "zone-show";
        else if (sub == "add")     registered = "zone-add";
        else if (sub == "rm")      registered = "zone-rm";
        else if (sub == "assign")  registered = "zone-assign";
        else if (sub == "rebuild") registered = "zone-rebuild";
        else if (sub == "resolve") registered = "zone-resolve";
        else {
            std::cerr << "icmg zone: unknown subcommand: " << sub << "\n";
            usage();
            return 1;
        }

        auto& reg = icmg::core::Registry<icmg::cli::BaseCommand>::instance();
        if (!reg.has(registered)) {
            std::cerr << "icmg zone: handler missing: " << registered << "\n";
            return 1;
        }
        auto handler = reg.create(registered);
        return handler->run(rest);
    }
};

ICMG_REGISTER_COMMAND("zone",          ZoneRootCommand);
ICMG_REGISTER_COMMAND("zone-list",     ZoneListCommand);
ICMG_REGISTER_COMMAND("zone-show",     ZoneShowCommand);
ICMG_REGISTER_COMMAND("zone-add",      ZoneAddCommand);
ICMG_REGISTER_COMMAND("zone-rm",       ZoneRemoveCommand);
ICMG_REGISTER_COMMAND("zone-assign",   ZoneAssignCommand);
ICMG_REGISTER_COMMAND("zone-rebuild",  ZoneRebuildCommand);
ICMG_REGISTER_COMMAND("zone-resolve",  ZoneResolveCommand);

} // namespace icmg::cli
