#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../data/data_store.hpp"
#include <iostream>
#include <iomanip>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <chrono>

namespace icmg::cli {

class DataCommand : public BaseCommand {
public:
    std::string name()        const override { return "data"; }
    std::string description() const override { return "Structured data store (model/view/behavior/schema)"; }

    void usage() const override {
        std::cout <<
            "Usage: icmg data <subcommand> [options]\n\n"
            "Subcommands:\n"
            "  add <type> <name> <content> [--scope <path>] [--tags <t>] [--file <f>]\n"
            "      type: model|view|behavior|schema\n"
            "  list [--type <type>] [--scope <path>] [--json]\n"
            "  show <name> [--json]\n"
            "  update <name> <content> [--note <msg>] [--file <f>]\n"
            "  remove <name>\n"
            "  search <query> [--limit N] [--json]\n"
            "  history <name>\n"
            "  revert <name> --to <version>\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || args[0] == "--help" || args[0] == "-h") {
            usage(); return 0;
        }

        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        data::DataStore store(db);

        const std::string& sub = args[0];

        if (sub == "add")     return doAdd(store, args);
        if (sub == "list")    return doList(store, args);
        if (sub == "show")    return doShow(store, args);
        if (sub == "update")  return doUpdate(store, args);
        if (sub == "remove")  return doRemove(store, args);
        if (sub == "search")  return doSearch(store, args);
        if (sub == "history") return doHistory(store, args);
        if (sub == "revert")  return doRevert(store, args);

        std::cerr << "icmg data: unknown subcommand '" << sub << "'\n";
        usage(); return 1;
    }

private:
    // ---- add ---------------------------------------------------------------
    int doAdd(data::DataStore& store, const std::vector<std::string>& args) {
        std::string scope = flagValue(args, "--scope");
        std::string tags  = flagValue(args, "--tags");
        std::string file  = flagValue(args, "--file");

        // Collect positional: type, name, content...
        std::vector<std::string> pos;
        for (size_t i = 1; i < args.size(); ++i) {
            const auto& a = args[i];
            if (a == "--scope" || a == "--tags" || a == "--file") { ++i; continue; }
            if (!a.empty() && a[0] == '-') continue;
            pos.push_back(a);
        }

        if (pos.size() < 2) {
            std::cerr << "icmg data add: requires <type> <name> [<content>]\n"; return 1;
        }

        data::StructuredData d;
        d.data_type  = pos[0];
        d.name       = pos[1];
        d.scope_path = scope;
        d.tags       = tags;

        // Validate type
        static const std::vector<std::string> valid_types = {"model","view","behavior","schema"};
        if (std::find(valid_types.begin(), valid_types.end(), d.data_type) == valid_types.end()) {
            std::cerr << "Unknown type '" << d.data_type
                      << "'. Valid: model|view|behavior|schema\n"; return 1;
        }

        // Content: from --file or positional
        if (!file.empty()) {
            std::ifstream f(file);
            if (!f) { std::cerr << "Cannot open file: " << file << "\n"; return 1; }
            d.content = std::string(std::istreambuf_iterator<char>(f), {});
        } else if (pos.size() >= 3) {
            for (size_t i = 2; i < pos.size(); ++i) {
                if (!d.content.empty()) d.content += " ";
                d.content += pos[i];
            }
        } else {
            std::cerr << "icmg data add: requires <content> or --file\n"; return 1;
        }

        // A2: warn if scope path doesn't exist
        if (!scope.empty()) {
            std::error_code ec;
            if (!std::filesystem::exists(scope, ec)) {
                std::cerr << "Warning: scope path '" << scope
                          << "' does not exist. Data will still be stored.\n";
            }
        }

        try {
            int64_t id = store.add(d);
            std::cout << "Added [" << d.data_type << "] " << d.name << " (id=" << id << ")\n";
        } catch (const std::runtime_error& e) {
            std::cerr << e.what() << "\n"; return 1;
        }
        return 0;
    }

    // ---- list --------------------------------------------------------------
    int doList(data::DataStore& store, const std::vector<std::string>& args) {
        std::string type  = flagValue(args, "--type");
        std::string scope = flagValue(args, "--scope");
        bool json_out     = hasFlag(args, "--json");

        auto list = store.list(type, scope);

        if (json_out) {
            std::cout << "[\n";
            for (size_t i = 0; i < list.size(); ++i) {
                auto& d = list[i];
                std::cout << "  {\"id\":" << d.id
                    << ",\"type\":\"" << escJ(d.data_type) << "\""
                    << ",\"name\":\"" << escJ(d.name) << "\""
                    << ",\"version\":\"" << escJ(d.version) << "\""
                    << ",\"scope\":\"" << escJ(d.scope_path) << "\""
                    << ",\"tags\":\"" << escJ(d.tags) << "\"}";
                if (i + 1 < list.size()) std::cout << ",";
                std::cout << "\n";
            }
            std::cout << "]\n";
        } else {
            if (list.empty()) { std::cout << "(no entries)\n"; return 0; }
            std::cout << std::left
                      << std::setw(10) << "TYPE"
                      << std::setw(20) << "NAME"
                      << std::setw(7)  << "VER"
                      << std::setw(14) << "SCOPE"
                      << "TAGS\n"
                      << std::string(70, '-') << "\n";
            for (auto& d : list) {
                std::cout << std::setw(10) << d.data_type
                          << std::setw(20) << d.name
                          << std::setw(7)  << d.version
                          << std::setw(14) << (d.scope_path.empty() ? "(global)" : d.scope_path)
                          << d.tags << "\n";
            }
        }
        return 0;
    }

    // ---- show --------------------------------------------------------------
    int doShow(data::DataStore& store, const std::vector<std::string>& args) {
        if (args.size() < 2) { std::cerr << "icmg data show: requires <name>\n"; return 1; }
        bool json_out = hasFlag(args, "--json");
        std::string name = args[1];

        auto d = store.get(name);
        if (!d) { std::cerr << "Not found: " << name << "\n"; return 1; }

        if (json_out) {
            std::cout << "{"
                << "\"id\":" << d->id
                << ",\"type\":\"" << escJ(d->data_type) << "\""
                << ",\"name\":\"" << escJ(d->name) << "\""
                << ",\"version\":\"" << escJ(d->version) << "\""
                << ",\"scope\":\"" << escJ(d->scope_path) << "\""
                << ",\"tags\":\"" << escJ(d->tags) << "\""
                << ",\"content\":\"" << escJ(d->content) << "\""
                << "}\n";
        } else {
            std::string scope = d->scope_path.empty() ? "(global)" : d->scope_path;
            std::cout << "[" << d->data_type << "] " << d->name
                      << "  v" << d->version
                      << "  (scope: " << scope << ")\n";
            if (!d->tags.empty()) std::cout << "Tags: " << d->tags << "\n";
            std::cout << "\n" << d->content << "\n";
        }
        return 0;
    }

    // ---- update ------------------------------------------------------------
    int doUpdate(data::DataStore& store, const std::vector<std::string>& args) {
        if (args.size() < 2) { std::cerr << "icmg data update: requires <name>\n"; return 1; }
        std::string name = args[1];
        std::string note = flagValue(args, "--note");
        std::string file = flagValue(args, "--file");

        std::string content;
        if (!file.empty()) {
            std::ifstream f(file);
            if (!f) { std::cerr << "Cannot open file: " << file << "\n"; return 1; }
            content = std::string(std::istreambuf_iterator<char>(f), {});
        } else {
            // Join remaining positional args as content
            for (size_t i = 2; i < args.size(); ++i) {
                if (args[i] == "--note" || args[i] == "--file") { ++i; continue; }
                if (!args[i].empty() && args[i][0] == '-') continue;
                if (!content.empty()) content += " ";
                content += args[i];
            }
        }

        if (content.empty()) { std::cerr << "icmg data update: requires <content>\n"; return 1; }

        if (!store.update(name, content, note)) {
            std::cerr << "Not found: " << name << "\n"; return 1;
        }
        auto updated = store.get(name);
        std::cout << "Updated: " << name << "  →  v" << (updated ? updated->version : "?") << "\n";
        return 0;
    }

    // ---- remove ------------------------------------------------------------
    int doRemove(data::DataStore& store, const std::vector<std::string>& args) {
        if (args.size() < 2) { std::cerr << "icmg data remove: requires <name>\n"; return 1; }
        store.remove(args[1]);
        std::cout << "Removed: " << args[1] << "\n";
        return 0;
    }

    // ---- search ------------------------------------------------------------
    int doSearch(data::DataStore& store, const std::vector<std::string>& args) {
        if (args.size() < 2) { std::cerr << "icmg data search: requires <query>\n"; return 1; }
        bool json_out = hasFlag(args, "--json");
        int limit = 10;
        std::string lim_str = flagValue(args, "--limit");
        if (!lim_str.empty()) try { limit = std::stoi(lim_str); } catch (...) {}

        // Build query from non-flag positional args after "search"
        std::string query;
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "--limit" || args[i] == "--json") {
                if (args[i] == "--limit") ++i; continue;
            }
            if (!query.empty()) query += " ";
            query += args[i];
        }

        auto results = store.search(query, limit);

        if (json_out) {
            std::cout << "[\n";
            for (size_t i = 0; i < results.size(); ++i) {
                auto& d = results[i];
                std::cout << "  {\"name\":\"" << escJ(d.name)
                          << "\",\"type\":\"" << escJ(d.data_type) << "\"}";
                if (i + 1 < results.size()) std::cout << ",";
                std::cout << "\n";
            }
            std::cout << "]\n";
        } else {
            if (results.empty()) { std::cout << "(no results)\n"; return 0; }
            for (auto& d : results)
                std::cout << "[" << d.data_type << "] " << d.name
                          << "  v" << d.version << "\n"
                          << "  " << d.content.substr(0, 80)
                          << (d.content.size() > 80 ? "..." : "") << "\n";
        }
        return 0;
    }

    // ---- history -----------------------------------------------------------
    int doHistory(data::DataStore& store, const std::vector<std::string>& args) {
        if (args.size() < 2) { std::cerr << "icmg data history: requires <name>\n"; return 1; }
        auto versions = store.history(args[1]);
        if (versions.empty()) { std::cout << "(no version history)\n"; return 0; }
        std::cout << "Version history for: " << args[1] << "\n"
                  << std::string(60, '-') << "\n";
        for (auto& v : versions) {
            std::cout << "v" << v.version;
            if (!v.change_note.empty()) std::cout << "  [" << v.change_note << "]";
            std::cout << "\n  " << v.content.substr(0, 80)
                      << (v.content.size() > 80 ? "..." : "") << "\n\n";
        }
        return 0;
    }

    // ---- revert ------------------------------------------------------------
    int doRevert(data::DataStore& store, const std::vector<std::string>& args) {
        if (args.size() < 2) { std::cerr << "icmg data revert: requires <name> --to <version>\n"; return 1; }
        std::string name    = args[1];
        std::string to_ver  = flagValue(args, "--to");
        if (to_ver.empty()) { std::cerr << "icmg data revert: requires --to <version>\n"; return 1; }
        if (!store.revert(name, to_ver)) {
            std::cerr << "Revert failed: not found or version '" << to_ver << "' not in history\n";
            return 1;
        }
        std::cout << "Reverted " << name << " to v" << to_ver << "\n";
        return 0;
    }

    static std::string escJ(const std::string& s) {
        std::string out;
        for (char c : s) {
            if      (c == '"')  out += "\\\"";
            else if (c == '\\') out += "\\\\";
            else if (c == '\n') out += "\\n";
            else                out += c;
        }
        return out;
    }
};

ICMG_REGISTER_COMMAND("data", DataCommand);

} // namespace icmg::cli
