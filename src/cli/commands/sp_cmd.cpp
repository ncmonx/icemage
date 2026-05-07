#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../sp/sp_store.hpp"
#include "../../sp/sql_parser.hpp"
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <set>
#include <regex>

namespace icmg::cli {

class SpCommand : public BaseCommand {
public:
    std::string name()        const override { return "sp"; }
    std::string description() const override { return "Stored procedure management"; }

    void usage() const override {
        std::cout <<
            "Usage: icmg sp <subcommand> [options]\n\n"
            "Subcommands:\n"
            "  add <name> <file.sql> [--db mssql|mysql|postgresql|oracle]\n"
            "             [--database <db>] [--context <desc>] [--scope <path>]\n"
            "  list [--db X] [--database X] [--param-type X] [--json]\n"
            "  show <name> [--json]\n"
            "  search <query>\n"
            "  deps <name>                  Show dependency tree\n"
            "  uses-table <table>           SPs that use this table\n"
            "  called-by <sp_name>          SPs that call this SP\n"
            "  update <name> <file.sql> [--note <text>]\n"
            "  history <name>\n"
            "  diff <name> <v1> <v2>        Diff two versions\n"
            "  lint <name>|--all            Check for issues\n"
            "  template <name>              Generate call template\n"
            "  impact-table <table> [--depth N]\n"
            "  remove <name>\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || args[0] == "--help" || args[0] == "-h") {
            usage(); return 0;
        }

        auto& cfg = core::Config::instance();
        std::string db_path = cfg.projectDbPath(".");
        core::Db db(db_path);
        sp::SpStore store(db);

        const std::string& sub = args[0];
        if (sub == "add")          return doAdd(store, args);
        if (sub == "list")         return doList(store, db, args);
        if (sub == "show")         return doShow(store, args);
        if (sub == "search")       return doSearch(store, args);
        if (sub == "deps")         return doDeps(store, args);
        if (sub == "uses-table")   return doUsesTable(store, args);
        if (sub == "called-by")    return doCalledBy(store, args);
        if (sub == "update")       return doUpdate(store, args);
        if (sub == "history")      return doHistory(store, args);
        if (sub == "diff")         return doDiff(store, args);
        if (sub == "lint")         return doLint(store, args);
        if (sub == "template")     return doTemplate(store, args);
        if (sub == "impact-table") return doImpactTable(store, args);
        if (sub == "remove")       return doRemove(store, args);
        if (sub == "link")         return doLink(db, args);

        std::cerr << "icmg sp: unknown subcommand '" << sub << "'\n";
        usage(); return 1;
    }

private:
    // ---- add ---------------------------------------------------------------
    int doAdd(sp::SpStore& store, const std::vector<std::string>& args) {
        std::string db_type  = flagValue(args, "--db");
        std::string database = flagValue(args, "--database");
        std::string ctx_over = flagValue(args, "--context");
        std::string scope    = flagValue(args, "--scope");

        std::vector<std::string> pos;
        for (size_t i = 1; i < args.size(); ++i) {
            const auto& a = args[i];
            if (a == "--db" || a == "--database" || a == "--context" || a == "--scope")
                { ++i; continue; }
            if (!a.empty() && a[0] == '-') continue;
            pos.push_back(a);
        }

        if (pos.size() < 2) {
            std::cerr << "icmg sp add: requires <name> <file.sql>\n"; return 1;
        }

        std::string name    = pos[0];
        std::string sqlfile = pos[1];

        std::string sql = readFile(sqlfile);
        if (sql.empty() && !std::filesystem::exists(sqlfile)) {
            std::cerr << "icmg sp add: cannot read file: " << sqlfile << "\n"; return 1;
        }

        sp::SqlParser parser;
        auto parsed = parser.parse(sql, db_type);

        sp::StoredProcedure proc;
        proc.name          = name;
        proc.db_type       = parsed.db_type.empty() ? "mssql" : parsed.db_type;
        proc.database_name = database;
        proc.content       = sql;
        proc.context       = ctx_over.empty() ? parsed.context : ctx_over;
        proc.parameters    = parsed.parameters;
        proc.tables_used   = parsed.tables;
        proc.sp_dependencies = parsed.sp_calls;
        proc.scope_path    = scope;

        try {
            int64_t id = store.add(proc);
            std::cout << "Added SP '" << name << "' (id=" << id << ")\n";
            if (!parsed.tables.empty()) {
                std::cout << "  Tables: ";
                for (size_t i = 0; i < parsed.tables.size(); ++i) {
                    if (i) std::cout << ", ";
                    std::cout << parsed.tables[i];
                }
                std::cout << "\n";
            }
            if (!parsed.sp_calls.empty()) {
                std::cout << "  Calls: ";
                for (size_t i = 0; i < parsed.sp_calls.size(); ++i) {
                    if (i) std::cout << ", ";
                    std::cout << parsed.sp_calls[i];
                }
                std::cout << "\n";
            }
        } catch (const std::exception& e) {
            std::cerr << e.what() << "\n"; return 1;
        }
        return 0;
    }

    // ---- list --------------------------------------------------------------
    int doList(sp::SpStore& store, core::Db& db,
               const std::vector<std::string>& args) {
        std::string db_type     = flagValue(args, "--db");
        std::string database    = flagValue(args, "--database");
        std::string param_type  = flagValue(args, "--param-type");
        bool json_out           = hasFlag(args, "--json");

        auto items = store.list(db_type, database);

        // A5: filter by param type
        if (!param_type.empty()) {
            std::string pt = param_type;
            for (char& c : pt) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            items.erase(std::remove_if(items.begin(), items.end(),
                [&](const sp::StoredProcedure& s) {
                    for (auto& p : s.parameters) {
                        std::string t = p.type;
                        for (char& c : t) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                        if (t.find(pt) != std::string::npos) return false;
                    }
                    return true;
                }), items.end());
        }

        if (json_out) {
            std::cout << "[\n";
            for (size_t i = 0; i < items.size(); ++i) {
                auto& s = items[i];
                std::cout << "  {\"id\":" << s.id
                          << ",\"name\":\"" << escJ(s.name) << "\""
                          << ",\"db_type\":\"" << escJ(s.db_type) << "\""
                          << ",\"database\":\"" << escJ(s.database_name) << "\""
                          << ",\"version\":" << s.version << "}";
                if (i + 1 < items.size()) std::cout << ",";
                std::cout << "\n";
            }
            std::cout << "]\n";
        } else {
            if (items.empty()) { std::cout << "(no stored procedures)\n"; return 0; }
            std::cout << std::left
                      << std::setw(4) << "v"
                      << std::setw(30) << "Name"
                      << std::setw(12) << "DB Type"
                      << "Database\n"
                      << std::string(70, '-') << "\n";
            for (auto& s : items) {
                std::cout << std::setw(4) << ("v" + std::to_string(s.version))
                          << std::setw(30) << s.name
                          << std::setw(12) << s.db_type
                          << s.database_name << "\n";
            }
        }
        return 0;
    }

    // ---- show --------------------------------------------------------------
    int doShow(sp::SpStore& store, const std::vector<std::string>& args) {
        if (args.size() < 2) { std::cerr << "icmg sp show: requires <name>\n"; return 1; }
        bool json_out = hasFlag(args, "--json");
        auto s = store.get(args[1]);
        if (!s) { std::cerr << "SP not found: " << args[1] << "\n"; return 1; }

        if (json_out) {
            std::cout << "{\"id\":" << s->id
                      << ",\"name\":\"" << escJ(s->name) << "\""
                      << ",\"db_type\":\"" << escJ(s->db_type) << "\""
                      << ",\"database\":\"" << escJ(s->database_name) << "\""
                      << ",\"version\":" << s->version
                      << ",\"context\":\"" << escJ(s->context) << "\""
                      << ",\"tables\":" << arrToJson(s->tables_used)
                      << ",\"calls\":" << arrToJson(s->sp_dependencies)
                      << "}\n";
        } else {
            std::cout << "[" << s->db_type << "] " << s->name
                      << "  v" << s->version;
            if (!s->database_name.empty()) std::cout << "  (db: " << s->database_name << ")";
            std::cout << "\n";
            if (!s->context.empty())
                std::cout << "Context: \"" << s->context << "\"\n";

            if (!s->parameters.empty()) {
                std::cout << "\nParameters:\n";
                for (auto& p : s->parameters) {
                    std::cout << "  " << std::setw(16) << p.name
                              << std::setw(12) << p.type
                              << p.direction;
                    if (!p.default_val.empty()) std::cout << "  (default: " << p.default_val << ")";
                    std::cout << "\n";
                }
            }

            if (!s->tables_used.empty()) {
                std::cout << "\nTables used: ";
                for (size_t i = 0; i < s->tables_used.size(); ++i) {
                    if (i) std::cout << ", ";
                    std::cout << s->tables_used[i];
                }
                std::cout << "\n";
            }

            if (!s->sp_dependencies.empty()) {
                std::cout << "Calls: ";
                for (size_t i = 0; i < s->sp_dependencies.size(); ++i) {
                    if (i) std::cout << ", ";
                    std::cout << s->sp_dependencies[i];
                }
                std::cout << "\n";
            }

            std::cout << "\n--- SQL ---\n" << s->content << "\n";
        }
        return 0;
    }

    // ---- search ------------------------------------------------------------
    int doSearch(sp::SpStore& store, const std::vector<std::string>& args) {
        if (args.size() < 2) { std::cerr << "icmg sp search: requires <query>\n"; return 1; }
        auto items = store.search(args[1]);
        if (items.empty()) { std::cout << "(no results)\n"; return 0; }
        std::cout << std::left
                  << std::setw(30) << "Name"
                  << std::setw(12) << "DB Type"
                  << "Context\n"
                  << std::string(70, '-') << "\n";
        for (auto& s : items) {
            std::string ctx = s.context.size() > 30 ? s.context.substr(0, 27) + "..." : s.context;
            std::cout << std::setw(30) << s.name << std::setw(12) << s.db_type << ctx << "\n";
        }
        return 0;
    }

    // ---- deps --------------------------------------------------------------
    int doDeps(sp::SpStore& store, const std::vector<std::string>& args) {
        if (args.size() < 2) { std::cerr << "icmg sp deps: requires <name>\n"; return 1; }
        auto s = store.get(args[1]);
        if (!s) { std::cerr << "SP not found: " << args[1] << "\n"; return 1; }

        std::cout << s->name << "\n";
        printDeps(store, s->name, s->sp_dependencies, 1, {s->name});
        return 0;
    }

    void printDeps(sp::SpStore& store, const std::string& /*parent*/,
                   const std::vector<std::string>& deps, int depth,
                   std::vector<std::string> visited) {
        for (auto& dep : deps) {
            std::string indent(depth * 2, ' ');
            std::cout << indent << "└─ " << dep;
            if (std::find(visited.begin(), visited.end(), dep) != visited.end()) {
                std::cout << " (circular)\n"; continue;
            }
            std::cout << "\n";
            auto child = store.get(dep);
            if (child && !child->sp_dependencies.empty()) {
                auto v2 = visited;
                v2.push_back(dep);
                printDeps(store, dep, child->sp_dependencies, depth + 1, v2);
            }
        }
    }

    // ---- uses-table --------------------------------------------------------
    int doUsesTable(sp::SpStore& store, const std::vector<std::string>& args) {
        if (args.size() < 2) { std::cerr << "icmg sp uses-table: requires <table>\n"; return 1; }
        auto items = store.usesTable(args[1]);
        if (items.empty()) { std::cout << "(no SPs use table: " << args[1] << ")\n"; return 0; }
        for (auto& s : items) std::cout << s.name << "\n";
        return 0;
    }

    // ---- called-by ---------------------------------------------------------
    int doCalledBy(sp::SpStore& store, const std::vector<std::string>& args) {
        if (args.size() < 2) { std::cerr << "icmg sp called-by: requires <sp_name>\n"; return 1; }
        auto items = store.calledBy(args[1]);
        if (items.empty()) { std::cout << "(no SPs call: " << args[1] << ")\n"; return 0; }
        for (auto& s : items) std::cout << s.name << "\n";
        return 0;
    }

    // ---- update ------------------------------------------------------------
    int doUpdate(sp::SpStore& store, const std::vector<std::string>& args) {
        std::string note = flagValue(args, "--note");
        std::vector<std::string> pos;
        for (size_t i = 1; i < args.size(); ++i) {
            const auto& a = args[i];
            if (a == "--note") { ++i; continue; }
            if (!a.empty() && a[0] == '-') continue;
            pos.push_back(a);
        }
        if (pos.size() < 2) { std::cerr << "icmg sp update: requires <name> <file.sql>\n"; return 1; }
        std::string sql = readFile(pos[1]);
        if (sql.empty()) { std::cerr << "Cannot read: " << pos[1] << "\n"; return 1; }
        if (!store.update(pos[0], sql, note)) {
            std::cerr << "SP not found: " << pos[0] << "\n"; return 1;
        }
        std::cout << "Updated SP '" << pos[0] << "'\n";
        return 0;
    }

    // ---- history -----------------------------------------------------------
    int doHistory(sp::SpStore& store, const std::vector<std::string>& args) {
        if (args.size() < 2) { std::cerr << "icmg sp history: requires <name>\n"; return 1; }
        auto vers = store.history(args[1]);
        auto cur  = store.get(args[1]);
        if (!cur) { std::cerr << "SP not found: " << args[1] << "\n"; return 1; }

        std::cout << "History for " << args[1] << " (current: v" << cur->version << ")\n";
        std::cout << std::string(50, '-') << "\n";
        for (auto& v : vers) {
            std::cout << "  v" << v.version;
            if (!v.change_note.empty()) std::cout << "  " << v.change_note;
            std::cout << "\n";
        }
        return 0;
    }

    // ---- diff (A1) ---------------------------------------------------------
    int doDiff(sp::SpStore& store, const std::vector<std::string>& args) {
        if (args.size() < 4) {
            std::cerr << "icmg sp diff: requires <name> <v1> <v2>\n"; return 1;
        }
        std::string name = args[1];
        int v1 = 0, v2 = 0;
        try { v1 = std::stoi(args[2]); v2 = std::stoi(args[3]); } catch (...) {
            std::cerr << "icmg sp diff: v1 and v2 must be integers\n"; return 1;
        }

        auto cur = store.get(name);
        if (!cur) { std::cerr << "SP not found: " << name << "\n"; return 1; }

        auto getContent = [&](int ver) -> std::string {
            if (ver == cur->version) return cur->content;
            auto hist = store.history(name);
            for (auto& h : hist) if (h.version == ver) return h.content;
            return "";
        };

        std::string c1 = getContent(v1);
        std::string c2 = getContent(v2);
        if (c1.empty()) { std::cerr << "Version " << v1 << " not found\n"; return 1; }
        if (c2.empty()) { std::cerr << "Version " << v2 << " not found\n"; return 1; }

        // Simple line-by-line diff output
        std::cout << "--- " << name << " v" << v1 << "\n";
        std::cout << "+++ " << name << " v" << v2 << "\n";
        unifiedDiff(c1, c2);
        return 0;
    }

    // ---- lint (A2) ---------------------------------------------------------
    int doLint(sp::SpStore& store, const std::vector<std::string>& args) {
        bool all = hasFlag(args, "--all");
        std::vector<sp::StoredProcedure> items;
        if (all) {
            items = store.list();
        } else {
            if (args.size() < 2) { std::cerr << "icmg sp lint: requires <name> or --all\n"; return 1; }
            auto s = store.get(args[1]);
            if (!s) { std::cerr << "SP not found: " << args[1] << "\n"; return 1; }
            items.push_back(*s);
        }

        int errors = 0, warnings = 0;
        for (auto& s : items) {
            bool any = false;
            // Check sp_calls exist in store
            for (auto& dep : s.sp_dependencies) {
                auto dep_sp = store.get(dep);
                if (!dep_sp) {
                    std::cout << "[ERROR] " << s.name << ": calls '" << dep
                              << "' which is not in store\n";
                    ++errors; any = true;
                }
            }
            // Recursive self-call
            for (auto& dep : s.sp_dependencies) {
                if (dep == s.name) {
                    std::cout << "[WARN]  " << s.name << ": recursive self-call\n";
                    ++warnings; any = true;
                }
            }
            if (!any) std::cout << "[OK]    " << s.name << "\n";
        }
        if (errors > 0 || warnings > 0)
            std::cout << "\n" << errors << " error(s), " << warnings << " warning(s)\n";
        else
            std::cout << "All OK\n";
        return (errors > 0) ? 1 : 0;
    }

    // ---- template (A3) -----------------------------------------------------
    int doTemplate(sp::SpStore& store, const std::vector<std::string>& args) {
        if (args.size() < 2) { std::cerr << "icmg sp template: requires <name>\n"; return 1; }
        auto s = store.get(args[1]);
        if (!s) { std::cerr << "SP not found: " << args[1] << "\n"; return 1; }

        if (s->db_type == "mssql") {
            std::cout << "-- MSSQL\nEXEC " << s->name << "\n";
        } else {
            std::cout << "-- " << s->db_type << "\nCALL " << s->name << "(\n";
        }
        for (size_t i = 0; i < s->parameters.size(); ++i) {
            auto& p = s->parameters[i];
            std::string placeholder = "'" + p.type + "_value'";
            if (p.default_val.empty() == false) placeholder = p.default_val;
            std::cout << "    " << std::setw(20) << p.name
                      << " = " << placeholder
                      << "   -- " << p.type << " " << p.direction;
            if (i + 1 < s->parameters.size()) std::cout << ",";
            std::cout << "\n";
        }
        if (s->db_type != "mssql") std::cout << ")\n";
        return 0;
    }

    // ---- impact-table (A4) -------------------------------------------------
    int doImpactTable(sp::SpStore& store, const std::vector<std::string>& args) {
        if (args.size() < 2) { std::cerr << "icmg sp impact-table: requires <table>\n"; return 1; }
        std::string depth_str = flagValue(args, "--depth");
        int max_depth = depth_str.empty() ? 2 : std::stoi(depth_str);
        std::string table = args[1];

        std::cout << "Table: " << table << "\n";

        // BFS
        std::vector<std::string> visited;
        std::vector<std::pair<std::string,int>> queue; // {sp_name, depth}
        auto direct = store.usesTable(table);
        std::cout << "Direct users (depth 1): " << direct.size() << " SP(s)\n";
        for (auto& s : direct) {
            std::cout << "  " << s.name << "\n";
            visited.push_back(s.name);
            if (max_depth > 1) queue.push_back({s.name, 1});
        }

        for (size_t qi = 0; qi < queue.size(); ++qi) {
            auto [sp_name, depth] = queue[qi];
            if (depth >= max_depth) continue;
            auto callers = store.calledBy(sp_name);
            for (auto& c : callers) {
                if (std::find(visited.begin(), visited.end(), c.name) == visited.end()) {
                    std::cout << "  " << c.name << " calls " << sp_name
                              << " (depth " << depth + 1 << ")\n";
                    visited.push_back(c.name);
                    queue.push_back({c.name, depth + 1});
                }
            }
        }
        std::cout << "Total impact: " << visited.size() << " SP(s)\n";
        return 0;
    }

    // ---- remove ------------------------------------------------------------
    int doRemove(sp::SpStore& store, const std::vector<std::string>& args) {
        if (args.size() < 2) { std::cerr << "icmg sp remove: requires <name>\n"; return 1; }
        if (!store.remove(args[1])) { std::cerr << "SP not found: " << args[1] << "\n"; return 1; }
        std::cout << "Removed SP: " << args[1] << "\n";
        return 0;
    }

    // ---- helpers -----------------------------------------------------------
    static std::string readFile(const std::string& path) {
        std::ifstream f(path);
        if (!f) return "";
        std::ostringstream ss;
        ss << f.rdbuf();
        return ss.str();
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

    static std::string arrToJson(const std::vector<std::string>& v) {
        std::string out = "[";
        for (size_t i = 0; i < v.size(); ++i) {
            if (i) out += ",";
            out += "\"" + escJ(v[i]) + "\"";
        }
        return out + "]";
    }

    // Simple unified diff — show changed lines with context
    static void unifiedDiff(const std::string& a, const std::string& b) {
        auto split = [](const std::string& s) {
            std::vector<std::string> lines;
            std::istringstream ss(s);
            std::string line;
            while (std::getline(ss, line)) lines.push_back(line);
            return lines;
        };
        auto la = split(a);
        auto lb = split(b);
        size_t i = 0, j = 0;
        while (i < la.size() || j < lb.size()) {
            if (i < la.size() && j < lb.size() && la[i] == lb[j]) {
                std::cout << "  " << la[i] << "\n"; ++i; ++j;
            } else if (i < la.size()) {
                std::cout << "- " << la[i] << "\n"; ++i;
            } else {
                std::cout << "+ " << lb[j] << "\n"; ++j;
            }
        }
    }
    // ---- link (Phase 21 Task 7) -------------------------------------------
    // Scan a file for stored-proc references and insert `calls` edges from
    // the file node to matching SP symbol nodes in the graph.
    int doLink(core::Db& db, const std::vector<std::string>& args) {
        std::string target;
        for (size_t i = 1; i < args.size(); ++i) {
            if (!args[i].empty() && args[i][0] != '-') { target = args[i]; break; }
        }
        if (target.empty()) {
            std::cerr << "icmg sp link: requires <file>\n"; return 1;
        }
        namespace fs = std::filesystem;
        if (!fs::exists(target)) {
            std::cerr << "icmg sp link: file not found: " << target << "\n"; return 1;
        }
        std::ifstream f(target, std::ios::binary);
        if (!f) { std::cerr << "icmg sp link: cannot open\n"; return 1; }
        std::ostringstream buf; buf << f.rdbuf();
        std::string content = buf.str();

        std::error_code ec;
        std::string canon = fs::weakly_canonical(target, ec).string();
        if (ec) canon = target;
#ifdef _WIN32
        if (canon.size() >= 2 && canon[1] == ':' && canon[0] >= 'a' && canon[0] <= 'z')
            canon[0] = (char)(canon[0] - 'a' + 'A');
#endif
        int64_t src_id = 0;
        db.query("SELECT id FROM graph_nodes WHERE path=?", {canon},
                 [&](const core::Row& r){ if (!r.empty()) try { src_id = std::stoll(r[0]); } catch(...){} });
        if (src_id == 0) {
            std::cerr << "icmg sp link: file not in graph (run `icmg graph scan` first)\n";
            return 1;
        }

        std::regex re_call(R"(\bEXEC(?:UTE)?\s+\[?(\w+)\]?)",
                           std::regex::ECMAScript | std::regex::icase);
        std::set<std::string> hits;
        for (auto it = std::sregex_iterator(content.begin(), content.end(), re_call);
             it != std::sregex_iterator(); ++it) {
            hits.insert((*it)[1].str());
        }
        if (hits.empty()) {
            std::cout << "No SP references found in " << target << "\n";
            return 0;
        }
        int linked = 0;
        for (auto& name : hits) {
            int64_t dst_id = 0;
            db.query(
                "SELECT id FROM graph_nodes WHERE kind='sp' AND symbol_name=? LIMIT 1",
                {name},
                [&](const core::Row& r){ if (!r.empty()) try { dst_id = std::stoll(r[0]); } catch(...){} });
            if (dst_id == 0 || dst_id == src_id) continue;
            db.run(
                "INSERT OR IGNORE INTO graph_edges(src,dst,edge_type,weight) VALUES(?,?,'calls',1.5)",
                {std::to_string(src_id), std::to_string(dst_id)});
            ++linked;
        }
        std::cout << "Linked " << linked << " SP reference(s).\n";
        return 0;
    }
};

ICMG_REGISTER_COMMAND("sp", SpCommand);

} // namespace icmg::cli
