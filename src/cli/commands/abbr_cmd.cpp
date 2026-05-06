#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../abbreviation/abbr_store.hpp"
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <filesystem>

namespace icmg::cli {

class AbbrCommand : public BaseCommand {
public:
    std::string name()        const override { return "abbr"; }
    std::string description() const override { return "Abbreviation management"; }

    void usage() const override {
        std::cout <<
            "Usage: icmg abbr <subcommand> [options]\n\n"
            "Subcommands:\n"
            "  learn <short> <full> [--domain <d>] [--scope <path>] [--update]\n"
            "  expand <short_or_sentence>\n"
            "  list [--domain <d>] [--json]\n"
            "  search <query>\n"
            "  remove <short> [--domain <d>]\n"
            "  import <file.csv>         (format: short,full,domain)\n"
            "  export [--domain <d>]\n"
            "  stats\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || args[0] == "--help" || args[0] == "-h") {
            usage(); return 0;
        }

        auto& cfg   = core::Config::instance();
        std::string db_path = cfg.projectDbPath(".");
        core::Db db(db_path);
        abbreviation::AbbrStore store(db);

        const std::string& sub = args[0];
        if (sub == "learn")  return doLearn(store, args);
        if (sub == "expand") return doExpand(store, args);
        if (sub == "list")   return doList(store, args);
        if (sub == "search") return doSearch(store, args);
        if (sub == "remove") return doRemove(store, args);
        if (sub == "import") return doImport(store, db, args);
        if (sub == "export") return doExport(store, args);
        if (sub == "stats")  return doStats(db);

        std::cerr << "icmg abbr: unknown subcommand '" << sub << "'\n";
        usage(); return 1;
    }

private:
    // ---- learn ------------------------------------------------------------
    int doLearn(abbreviation::AbbrStore& store, const std::vector<std::string>& args) {
        std::string domain    = flagValue(args, "--domain");
        std::string scope     = flagValue(args, "--scope");
        bool        update    = hasFlag(args, "--update");

        std::vector<std::string> pos;
        for (size_t i = 1; i < args.size(); ++i) {
            const auto& a = args[i];
            if (a == "--domain" || a == "--scope") { ++i; continue; }
            if (a == "--update") continue;
            if (!a.empty() && a[0] == '-') continue;
            pos.push_back(a);
        }

        if (pos.size() < 2) {
            std::cerr << "icmg abbr learn: requires <short> <full>\n"; return 1;
        }

        // A3: scope validation warning
        if (!scope.empty() && !std::filesystem::exists(scope)) {
            std::cerr << "warning: scope path does not exist: " << scope << "\n";
        }

        abbreviation::Abbreviation a;
        a.short_form = pos[0];
        a.full_form  = pos[1];
        a.domain     = domain;
        a.scope_path = scope;

        try {
            int64_t id = store.learn(a, update);
            std::cout << "Learned: " << a.short_form << " → " << a.full_form;
            if (!domain.empty()) std::cout << "  [" << domain << "]";
            std::cout << "  (id=" << id << ")\n";
        } catch (const abbreviation::AbbrConflictError& e) {
            std::cerr << "Error: " << e.what() << "\n"; return 1;
        } catch (const std::exception& e) {
            std::cerr << e.what() << "\n"; return 1;
        }
        return 0;
    }

    // ---- expand ------------------------------------------------------------
    int doExpand(abbreviation::AbbrStore& store, const std::vector<std::string>& args) {
        if (args.size() < 2) { std::cerr << "icmg abbr expand: requires <text>\n"; return 1; }

        std::string text = args[1];
        std::string cwd;
        try { cwd = std::filesystem::current_path().string(); } catch (...) {}
        for (char& c : cwd) if (c == '\\') c = '/';

        // Bump frequency for each abbreviation found
        auto expanded = store.expand(text, cwd);
        std::cout << expanded << "\n";
        return 0;
    }

    // ---- list --------------------------------------------------------------
    int doList(abbreviation::AbbrStore& store, const std::vector<std::string>& args) {
        std::string domain   = flagValue(args, "--domain");
        bool        json_out = hasFlag(args, "--json");

        auto items = store.list(domain);

        if (json_out) {
            std::cout << "[\n";
            for (size_t i = 0; i < items.size(); ++i) {
                auto& a = items[i];
                std::cout << "  {\"id\":" << a.id
                          << ",\"short\":\"" << escJ(a.short_form) << "\""
                          << ",\"full\":\"" << escJ(a.full_form) << "\""
                          << ",\"domain\":\"" << escJ(a.domain) << "\""
                          << ",\"frequency\":" << a.frequency << "}";
                if (i + 1 < items.size()) std::cout << ",";
                std::cout << "\n";
            }
            std::cout << "]\n";
        } else {
            if (items.empty()) { std::cout << "(no abbreviations)\n"; return 0; }
            std::cout << std::left
                      << std::setw(12) << "Short"
                      << std::setw(30) << "Full"
                      << std::setw(15) << "Domain"
                      << "Used\n"
                      << std::string(62, '-') << "\n";
            for (auto& a : items) {
                std::cout << std::setw(12) << a.short_form
                          << std::setw(30) << a.full_form
                          << std::setw(15) << (a.domain.empty() ? "—" : a.domain)
                          << a.frequency << "x\n";
            }
        }
        return 0;
    }

    // ---- search ------------------------------------------------------------
    int doSearch(abbreviation::AbbrStore& store, const std::vector<std::string>& args) {
        if (args.size() < 2) { std::cerr << "icmg abbr search: requires <query>\n"; return 1; }
        auto items = store.search(args[1]);
        if (items.empty()) { std::cout << "(no results)\n"; return 0; }
        std::cout << std::left
                  << std::setw(12) << "Short"
                  << std::setw(30) << "Full"
                  << "Domain\n"
                  << std::string(50, '-') << "\n";
        for (auto& a : items) {
            std::cout << std::setw(12) << a.short_form
                      << std::setw(30) << a.full_form
                      << (a.domain.empty() ? "—" : a.domain) << "\n";
        }
        return 0;
    }

    // ---- remove ------------------------------------------------------------
    int doRemove(abbreviation::AbbrStore& store, const std::vector<std::string>& args) {
        if (args.size() < 2) { std::cerr << "icmg abbr remove: requires <short>\n"; return 1; }
        std::string domain = flagValue(args, "--domain");
        bool ok = store.remove(args[1], domain);
        if (!ok) { std::cerr << "Not found: " << args[1] << "\n"; return 1; }
        std::cout << "Removed: " << args[1] << "\n";
        return 0;
    }

    // ---- import ------------------------------------------------------------
    int doImport(abbreviation::AbbrStore& store, core::Db& /*db*/,
                 const std::vector<std::string>& args) {
        if (args.size() < 2) { std::cerr << "icmg abbr import: requires <file.csv>\n"; return 1; }

        std::ifstream f(args[1]);
        if (!f) { std::cerr << "Cannot open: " << args[1] << "\n"; return 1; }

        int ok = 0, skip = 0;
        std::string line;
        while (std::getline(f, line)) {
            if (line.empty() || line[0] == '#') continue;
            // Format: short,full,domain (domain optional)
            std::vector<std::string> cols;
            std::istringstream ss(line);
            std::string col;
            while (std::getline(ss, col, ',')) cols.push_back(col);
            if (cols.size() < 2) { ++skip; continue; }

            abbreviation::Abbreviation a;
            a.short_form = cols[0];
            a.full_form  = cols[1];
            if (cols.size() >= 3) a.domain = cols[2];

            try { store.learn(a, /*update=*/false); ++ok; }
            catch (...) { ++skip; }
        }
        std::cout << "Imported: " << ok << "  skipped: " << skip << "\n";
        return 0;
    }

    // ---- export ------------------------------------------------------------
    int doExport(abbreviation::AbbrStore& store, const std::vector<std::string>& args) {
        std::string domain = flagValue(args, "--domain");
        auto items = store.list(domain);
        // CSV to stdout
        std::cout << "short,full,domain\n";
        for (auto& a : items) {
            std::cout << escCsv(a.short_form) << ","
                      << escCsv(a.full_form)  << ","
                      << escCsv(a.domain)     << "\n";
        }
        return 0;
    }

    // ---- stats -------------------------------------------------------------
    int doStats(core::Db& db) {
        int total = 0;
        db.query("SELECT COUNT(*) FROM abbreviations", {},
                 [&](const core::Row& r) { if (!r.empty()) try { total = std::stoi(r[0]); } catch (...) {} });
        std::cout << "Total abbreviations: " << total << "\n\n";

        // Top 10 by frequency
        std::cout << "Most used:\n";
        std::cout << std::left << std::setw(12) << "Short" << std::setw(30) << "Full"
                  << "Used\n" << std::string(50, '-') << "\n";
        db.query(
            "SELECT short_form,full_form,frequency FROM abbreviations "
            "ORDER BY frequency DESC LIMIT 10",
            {},
            [](const core::Row& r) {
                if (r.size() < 3) return;
                std::cout << std::setw(12) << r[0] << std::setw(30) << r[1]
                          << r[2] << "x\n";
            });
        return 0;
    }

    // ---- helpers -----------------------------------------------------------
    static std::string escJ(const std::string& s) {
        std::string out;
        for (char c : s) {
            if      (c == '"')  out += "\\\"";
            else if (c == '\\') out += "\\\\";
            else                out += c;
        }
        return out;
    }

    static std::string escCsv(const std::string& s) {
        if (s.find(',') == std::string::npos && s.find('"') == std::string::npos)
            return s;
        std::string out = "\"";
        for (char c : s) {
            if (c == '"') out += "\"\"";
            else          out += c;
        }
        out += "\"";
        return out;
    }
};

ICMG_REGISTER_COMMAND("abbr", AbbrCommand);

} // namespace icmg::cli
