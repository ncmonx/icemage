// Phase 27 T6: `icmg config` — read/write ~/.icmg/config.json without manual edit.
//
// Reuses existing Config singleton getString/set/save helpers. Atomic write
// to a temp file + rename so corrupt state on crash impossible.

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/exec_utils.hpp"
#include "../../core/config.hpp"
#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <shellapi.h>
#endif
#include <nlohmann/json.hpp>
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <cstdlib>
#include <set>

namespace fs = std::filesystem;

namespace icmg::cli {

class ConfigCommand : public BaseCommand {
public:
    std::string name()        const override { return "config"; }
    std::string description() const override { return "Read/write ~/.icmg/config.json"; }

    void usage() const override {
        std::cout <<
            "Usage: icmg config <action> [args]\n\n"
            "Actions:\n"
            "  list                       Print all key/value pairs\n"
            "  get <key>                  Print one value\n"
            "  set <key> <value>          Update or insert\n"
            "  unset <key>                Remove key\n"
            "  edit                       Open $EDITOR on config file\n"
            "  path                       Print config file path\n"
            "  zone <name> <get|set|unset|list> [args]   Per-zone overrides\n"
            "  zones                      List zones with overrides\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || args[0] == "--help") { usage(); return 0; }
        std::string action = args[0];

        auto& cfg = core::Config::instance();
        cfg.load();
        fs::path path = configPath();

        if (action == "path") { std::cout << path.string() << "\n"; return 0; }
        if (action == "list") return doList(path);
        if (action == "get")  {
            if (args.size() < 2) { std::cerr << "config get: <key> required\n"; return 1; }
            std::string v = cfg.getString(args[1], "");
            std::cout << v << "\n";
            return v.empty() ? 1 : 0;
        }
        if (action == "set") {
            if (args.size() < 3) { std::cerr << "config set: <key> <value> required\n"; return 1; }
            cfg.set(args[1], args[2]);
            cfg.save();
            std::cout << "set " << args[1] << " = " << args[2] << "\n";
            return 0;
        }
        if (action == "unset") {
            if (args.size() < 2) { std::cerr << "config unset: <key> required\n"; return 1; }
            return doUnset(path, args[1]);
        }
        if (action == "edit") return doEdit(path);
        if (action == "zone") return doZone(args);
        if (action == "zones") return doZonesList(path);

        std::cerr << "config: unknown action: " << action << "\n";
        usage();
        return 1;
    }

    int doZone(const std::vector<std::string>& args) {
        // Phase 31 T4: per-zone overrides via dotted prefix `zone.<name>.<key>`.
        // Resolver order (caller-side): zone.<X>.<key> -> <key> -> default.
        if (args.size() < 3) {
            std::cerr << "config zone: usage: config zone <name> <set|get|unset|list> [args]\n";
            return 1;
        }
        std::string zone = args[1];
        std::string sub  = args[2];
        auto& cfg = core::Config::instance();

        if (sub == "set") {
            if (args.size() < 5) { std::cerr << "config zone X set <key> <value>\n"; return 1; }
            std::string full = "zone." + zone + "." + args[3];
            cfg.set(full, args[4]);
            cfg.save();
            std::cout << "set " << full << " = " << args[4] << "\n";
            return 0;
        }
        if (sub == "get") {
            if (args.size() < 4) { std::cerr << "config zone X get <key>\n"; return 1; }
            std::string full = "zone." + zone + "." + args[3];
            std::string v = cfg.getString(full, "");
            std::cout << v << "\n";
            return v.empty() ? 1 : 0;
        }
        if (sub == "unset") {
            if (args.size() < 4) { std::cerr << "config zone X unset <key>\n"; return 1; }
            return doUnset(configPath(), "zone." + zone + "." + args[3]);
        }
        if (sub == "list") {
            // Print all keys with prefix `zone.<zone>.`.
            std::ifstream f(configPath());
            if (!f) { std::cout << "(no config)\n"; return 0; }
            std::ostringstream s; s << f.rdbuf();
            try {
                auto j = nlohmann::json::parse(s.str());
                std::string prefix = "zone." + zone + ".";
                int n = 0;
                for (auto& [k, v] : j.items()) {
                    if (k.rfind(prefix, 0) == 0) {
                        std::cout << k.substr(prefix.size()) << " = ";
                        if (v.is_string()) std::cout << v.get<std::string>();
                        else               std::cout << v.dump();
                        std::cout << "\n";
                        ++n;
                    }
                }
                if (n == 0) std::cout << "(no overrides for zone '" << zone << "')\n";
            } catch (...) {}
            return 0;
        }
        std::cerr << "config zone: unknown subaction: " << sub << "\n";
        return 1;
    }

    int doZonesList(const fs::path& path) {
        if (!fs::exists(path)) { std::cout << "(no zones configured)\n"; return 0; }
        std::ifstream f(path);
        std::ostringstream s; s << f.rdbuf();
        try {
            auto j = nlohmann::json::parse(s.str());
            std::set<std::string> zones;
            for (auto& [k, v] : j.items()) {
                if (k.rfind("zone.", 0) != 0) continue;
                std::string rest = k.substr(5);
                auto dot = rest.find('.');
                if (dot != std::string::npos) zones.insert(rest.substr(0, dot));
            }
            if (zones.empty()) { std::cout << "(no zones)\n"; return 0; }
            std::cout << "Zones with overrides:\n";
            for (auto& z : zones) std::cout << "  " << z << "\n";
        } catch (...) {}
        return 0;
    }

private:
    fs::path configPath() {
        const char* h = std::getenv("USERPROFILE");
        if (!h) h = std::getenv("HOME");
        if (!h) h = ".";
        return fs::path(h) / ".icmg" / "config.json";
    }

    int doList(const fs::path& path) {
        if (!fs::exists(path)) {
            std::cout << "(no config — create with `icmg config set <key> <value>`)\n";
            return 0;
        }
        std::ifstream f(path);
        std::ostringstream s; s << f.rdbuf();
        std::cout << s.str();
        if (!s.str().empty() && s.str().back() != '\n') std::cout << "\n";
        return 0;
    }

    int doUnset(const fs::path& path, const std::string& key) {
        // Naive: load, parse, remove key, atomic write.
        if (!fs::exists(path)) return 0;
        std::ifstream f(path);
        std::ostringstream s; s << f.rdbuf();
        std::string raw = s.str();
        // Find "<key>": "..." pattern crude — fallback to JSON parse.
        try {
            auto j = nlohmann::json::parse(raw);
            if (!j.is_object()) return 1;
            // Support dotted keys by traversal.
            std::vector<std::string> parts;
            std::string cur;
            for (char c : key) {
                if (c == '.') { parts.push_back(cur); cur.clear(); }
                else cur.push_back(c);
            }
            parts.push_back(cur);
            auto* node = &j;
            for (size_t i = 0; i + 1 < parts.size(); ++i) {
                if (!node->contains(parts[i])) return 0;
                node = &(*node)[parts[i]];
            }
            node->erase(parts.back());
            // Atomic write
            fs::path tmp = path; tmp += ".tmp";
            { std::ofstream out(tmp); out << j.dump(2) << "\n"; }
            fs::rename(tmp, path);
            std::cout << "unset " << key << "\n";
            return 0;
        } catch (const std::exception& e) {
            std::cerr << "config unset: parse error: " << e.what() << "\n";
            return 1;
        }
    }

    int doEdit(const fs::path& path) {
        if (!fs::exists(path)) {
            // Create empty.
            fs::create_directories(path.parent_path());
            std::ofstream out(path); out << "{}\n";
        }
        const char* ed = std::getenv("EDITOR");
#ifdef _WIN32
        if (!ed) {
            // ShellExecuteA opens notepad without spawning a cmd.exe relay.
            ShellExecuteA(nullptr, "open", path.string().c_str(),
                          nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        }
        std::string cmd = std::string(ed) + " \"" + path.string() + "\"";
#else
        std::string cmd = std::string(ed ? ed : "vi") + " '" + path.string() + "'";
#endif
        auto res = core::safeExecShell(cmd, true, 120000);
        if (!res.out.empty()) std::cout << res.out;
        return res.exit_code;
    }
};

ICMG_REGISTER_COMMAND("config", ConfigCommand);

} // namespace icmg::cli
