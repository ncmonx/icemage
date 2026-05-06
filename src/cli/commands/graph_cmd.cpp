#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../graph/graph_store.hpp"
#include "../../graph/scanner.hpp"
#include "../../graph/daemon.hpp"
#include <iostream>
#include <iomanip>
#include <chrono>
#include <sstream>
#include <filesystem>

namespace icmg::cli {

static std::string timeAgo(int64_t epoch) {
    if (epoch <= 0) return "never";
    int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    int64_t diff = now - epoch;
    if (diff < 60)    return std::to_string(diff) + "s ago";
    if (diff < 3600)  return std::to_string(diff/60) + "m ago";
    if (diff < 86400) return std::to_string(diff/3600) + "h ago";
    return std::to_string(diff/86400) + "d ago";
}

static void escapeJson(std::ostream& o, const std::string& s) {
    for (char c : s) {
        if      (c == '"')  o << "\\\"";
        else if (c == '\\') o << "\\\\";
        else if (c == '\n') o << "\\n";
        else if (c == '\t') o << "\\t";
        else                o << c;
    }
}

static void printNodeJson(std::ostream& o, const graph::GraphNode& n) {
    o << "{\"id\":" << n.id
      << ",\"path\":\""; escapeJson(o, n.path); o << "\""
      << ",\"lang\":\"" << n.lang << "\""
      << ",\"size\":" << n.size_bytes
      << ",\"access_count\":" << n.access_count
      << ",\"updated\":\"" << timeAgo(n.updated_at) << "\""
      << ",\"context\":\""; escapeJson(o, n.context); o << "\""
      << "}";
}

// ---- graph scan ----
class GraphScanCommand : public BaseCommand {
public:
    std::string name()        const override { return "graph-scan"; }
    std::string description() const override { return "Scan directory into graph"; }

    int run(const std::vector<std::string>& args) override {
        std::string path = ".";
        for (auto& a : args) { if (!a.empty() && a[0] != '-') { path = a; break; } }

        graph::Scanner::Options opts;
        std::string depth_str = flagValue(args, "--depth", "20");
        try { opts.max_depth = std::stoi(depth_str); } catch (...) {}
        std::string lang_str = flagValue(args, "--lang");
        if (!lang_str.empty()) {
            std::istringstream ss(lang_str);
            std::string l;
            while (std::getline(ss, l, ',')) opts.include_langs.push_back(l);
        }
        bool json_out = hasFlag(args, "--json");

        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        graph::GraphStore store(db);
        graph::Scanner scanner(store);

        int count = scanner.scan(path, opts);
        if (json_out) {
            std::cout << "{\"scanned\":" << count
                      << ",\"nodes\":" << store.nodeCount()
                      << ",\"edges\":" << store.edgeCount() << "}\n";
        } else {
            std::cout << "Scanned " << count << " file(s)"
                      << " | nodes=" << store.nodeCount()
                      << " edges=" << store.edgeCount() << "\n";
        }
        return 0;
    }
};

// ---- graph context ----
class GraphContextCommand : public BaseCommand {
public:
    std::string name()        const override { return "graph-context"; }
    std::string description() const override { return "Show graph context for a file"; }

    int run(const std::vector<std::string>& args) override {
        if (args.empty()) { std::cerr << "icmg graph context: requires <file>\n"; return 1; }
        std::string file;
        for (auto& a : args) { if (!a.empty() && a[0] != '-') { file = a; break; } }
        bool json_out = hasFlag(args, "--json");

        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        graph::GraphStore store(db);

        auto node = store.getNode(file);
        if (!node) { std::cerr << "icmg graph context: not found (run `graph scan` first)\n"; return 1; }

        store.bumpAccess(node->id);
        auto edges_from = store.edgesFrom(node->id);
        auto edges_to   = store.edgesTo(node->id);

        if (json_out) {
            std::cout << "{";
            std::cout << "\"id\":" << node->id;
            std::cout << ",\"path\":\""; escapeJson(std::cout, node->path); std::cout << "\"";
            std::cout << ",\"lang\":\"" << node->lang << "\"";
            std::cout << ",\"size\":" << node->size_bytes;
            std::cout << ",\"context\":\""; escapeJson(std::cout, node->context); std::cout << "\"";
            std::cout << ",\"symbols\":" << node->symbols;
            std::cout << ",\"deps_count\":" << edges_from.size();
            std::cout << ",\"used_by_count\":" << edges_to.size();
            std::cout << "}\n";
        } else {
            double kb = node->size_bytes / 1024.0;
            std::cout << "File: " << node->path << "\n";
            std::cout << "Lang: " << node->lang
                      << "  |  Size: " << std::fixed << std::setprecision(1) << kb << " KB"
                      << "  |  Updated: " << timeAgo(node->updated_at) << "\n";
            if (!node->context.empty())
                std::cout << "Context: \"" << node->context.substr(0, 120) << "\"\n";
            std::cout << "Symbols: " << node->symbols << "\n";
            std::cout << "Depends on (" << edges_from.size() << " edges)\n";
            std::cout << "Used by   (" << edges_to.size()   << " edges)\n";
        }
        return 0;
    }
};

// ---- graph related ----
class GraphRelatedCommand : public BaseCommand {
public:
    std::string name()        const override { return "graph-related"; }
    std::string description() const override { return "Show related files"; }

    int run(const std::vector<std::string>& args) override {
        if (args.empty()) { std::cerr << "icmg graph related: requires <file>\n"; return 1; }
        std::string file;
        for (auto& a : args) { if (!a.empty() && a[0] != '-') { file = a; break; } }
        int limit = 10;
        try { limit = std::stoi(flagValue(args, "--limit", "10")); } catch (...) {}
        bool json_out = hasFlag(args, "--json");

        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        graph::GraphStore store(db);

        auto nodes = store.related(file, limit);
        if (json_out) {
            std::cout << "[";
            for (size_t i = 0; i < nodes.size(); ++i) {
                if (i) std::cout << ",";
                printNodeJson(std::cout, nodes[i]);
            }
            std::cout << "]\n";
        } else {
            if (nodes.empty()) { std::cout << "No related files.\n"; return 0; }
            for (auto& n : nodes)
                std::cout << n.lang << "  " << n.path << "\n";
        }
        return 0;
    }
};

// ---- graph list ----
class GraphListCommand : public BaseCommand {
public:
    std::string name()        const override { return "graph-list"; }
    std::string description() const override { return "List all graph nodes"; }

    int run(const std::vector<std::string>& args) override {
        std::string lang_filter = flagValue(args, "--lang");
        bool json_out = hasFlag(args, "--json");

        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        graph::GraphStore store(db);

        auto nodes = store.all();
        if (!lang_filter.empty()) {
            nodes.erase(std::remove_if(nodes.begin(), nodes.end(),
                [&](const graph::GraphNode& n) { return n.lang != lang_filter; }), nodes.end());
        }

        if (json_out) {
            std::cout << "[";
            for (size_t i = 0; i < nodes.size(); ++i) {
                if (i) std::cout << ",";
                printNodeJson(std::cout, nodes[i]);
            }
            std::cout << "]\n";
        } else {
            for (auto& n : nodes) {
                double kb = n.size_bytes / 1024.0;
                std::cout << std::left << std::setw(8) << n.lang
                          << std::setw(10) << (std::to_string((int)kb) + " KB")
                          << n.path << "\n";
            }
        }
        return 0;
    }
};

// ---- graph stats ----
class GraphStatsCommand : public BaseCommand {
public:
    std::string name()        const override { return "graph-stats"; }
    std::string description() const override { return "Graph statistics"; }

    int run(const std::vector<std::string>& args) override {
        bool json_out = hasFlag(args, "--json");
        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        graph::GraphStore store(db);

        int nodes = store.nodeCount();
        int edges = store.edgeCount();
        if (json_out) {
            std::cout << "{\"nodes\":" << nodes << ",\"edges\":" << edges << "}\n";
        } else {
            std::cout << "Nodes: " << nodes << "\nEdges: " << edges << "\n";
        }
        return 0;
    }
};

// ---- graph impact ----
class GraphImpactCommand : public BaseCommand {
public:
    std::string name()        const override { return "graph-impact"; }
    std::string description() const override { return "Show files impacted by changing a file"; }

    int run(const std::vector<std::string>& args) override {
        if (args.empty()) { std::cerr << "icmg graph impact: requires <file>\n"; return 1; }
        std::string file;
        for (auto& a : args) { if (!a.empty() && a[0] != '-') { file = a; break; } }
        int depth = 3;
        try { depth = std::stoi(flagValue(args, "--depth", "3")); } catch (...) {}
        bool json_out = hasFlag(args, "--json");

        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        graph::GraphStore store(db);

        auto affected = store.impact(file, depth);
        if (json_out) {
            std::cout << "[";
            for (size_t i = 0; i < affected.size(); ++i) {
                if (i) std::cout << ",";
                printNodeJson(std::cout, affected[i]);
            }
            std::cout << "]\n";
        } else {
            std::cout << "Impact of changing: " << file << "\n";
            std::cout << "  " << affected.size() << " file(s) affected:\n";
            for (auto& n : affected) std::cout << "    " << n.path << "\n";
        }
        return 0;
    }
};

// ---- graph orphans ----
class GraphOrphansCommand : public BaseCommand {
public:
    std::string name()        const override { return "graph-orphans"; }
    std::string description() const override { return "Find orphan files (no inbound edges)"; }

    int run(const std::vector<std::string>& args) override {
        std::string exclude_str = flagValue(args, "--exclude-pattern");
        bool json_out = hasFlag(args, "--json");
        std::vector<std::string> excl;
        if (!exclude_str.empty()) {
            std::istringstream ss(exclude_str);
            std::string p;
            while (std::getline(ss, p, ',')) {
                while (!p.empty() && p.front() == ' ') p.erase(p.begin());
                excl.push_back(p);
            }
        }
        // Default: exclude known entry points
        excl.insert(excl.end(), {"main.", "index.", "mod.rs", "__init__"});

        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        graph::GraphStore store(db);

        auto nodes = store.orphans(excl);
        if (json_out) {
            std::cout << "[";
            for (size_t i = 0; i < nodes.size(); ++i) {
                if (i) std::cout << ",";
                printNodeJson(std::cout, nodes[i]);
            }
            std::cout << "]\n";
        } else {
            if (nodes.empty()) { std::cout << "No orphans found.\n"; return 0; }
            std::cout << "Orphan files (" << nodes.size() << "):\n";
            for (auto& n : nodes) std::cout << "  " << n.path << "\n";
        }
        return 0;
    }
};

// ---- graph cycles ----
class GraphCyclesCommand : public BaseCommand {
public:
    std::string name()        const override { return "graph-cycles"; }
    std::string description() const override { return "Detect circular dependencies"; }

    int run(const std::vector<std::string>& args) override {
        std::string lang_filter = flagValue(args, "--lang");
        bool json_out = hasFlag(args, "--json");

        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        graph::GraphStore store(db);

        auto found = store.cycles(lang_filter);
        if (json_out) {
            std::cout << "[";
            for (size_t i = 0; i < found.size(); ++i) {
                if (i) std::cout << ",";
                std::cout << "[";
                for (size_t j = 0; j < found[i].size(); ++j) {
                    if (j) std::cout << ",";
                    std::cout << "\""; escapeJson(std::cout, found[i][j]); std::cout << "\"";
                }
                std::cout << "]";
            }
            std::cout << "]\n";
        } else {
            if (found.empty()) { std::cout << "No cycles detected.\n"; return 0; }
            std::cout << found.size() << " cycle(s) detected:\n";
            for (auto& cycle : found) {
                std::cout << "  ";
                for (size_t i = 0; i < cycle.size(); ++i) {
                    if (i) std::cout << " → ";
                    std::cout << cycle[i];
                }
                std::cout << "\n";
            }
        }
        return 0;
    }
};

// ---- graph hot ----
class GraphHotCommand : public BaseCommand {
public:
    std::string name()        const override { return "graph-hot"; }
    std::string description() const override { return "Show most accessed files"; }

    int run(const std::vector<std::string>& args) override {
        int days  = 7;
        int limit = 20;
        try { days  = std::stoi(flagValue(args, "--days",  "7"));  } catch (...) {}
        try { limit = std::stoi(flagValue(args, "--limit", "20")); } catch (...) {}
        bool json_out = hasFlag(args, "--json");

        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        graph::GraphStore store(db);

        auto nodes = store.hot(days, limit);
        if (json_out) {
            std::cout << "[";
            for (size_t i = 0; i < nodes.size(); ++i) {
                if (i) std::cout << ",";
                printNodeJson(std::cout, nodes[i]);
            }
            std::cout << "]\n";
        } else {
            if (nodes.empty()) { std::cout << "No data.\n"; return 0; }
            for (auto& n : nodes)
                std::cout << "[" << n.access_count << "x] " << n.path << "\n";
        }
        return 0;
    }
};

// ---- graph search ----
class GraphSearchCommand : public BaseCommand {
public:
    std::string name()        const override { return "graph-search"; }
    std::string description() const override { return "Search graph nodes by query"; }

    int run(const std::vector<std::string>& args) override {
        std::string query;
        for (auto& a : args) { if (!a.empty() && a[0] != '-') { query = a; break; } }
        if (query.empty()) { std::cerr << "icmg graph search: requires <query>\n"; return 1; }
        int limit = 10;
        try { limit = std::stoi(flagValue(args, "--limit", "10")); } catch (...) {}
        bool json_out = hasFlag(args, "--json");

        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        graph::GraphStore store(db);

        auto nodes = store.search(query, limit);
        if (json_out) {
            std::cout << "[";
            for (size_t i = 0; i < nodes.size(); ++i) {
                if (i) std::cout << ",";
                printNodeJson(std::cout, nodes[i]);
            }
            std::cout << "]\n";
        } else {
            if (nodes.empty()) { std::cout << "No results.\n"; return 0; }
            for (auto& n : nodes)
                std::cout << n.lang << "  " << n.path << "  — " << n.context.substr(0,60) << "\n";
        }
        return 0;
    }
};

// ---- graph-watch ----
class GraphWatchCommand : public BaseCommand {
public:
    std::string name()        const override { return "graph-watch"; }
    std::string description() const override { return "Start file watcher daemon"; }

    int run(const std::vector<std::string>& args) override {
        std::string path = ".";
        for (auto& a : args) { if (!a.empty() && a[0] != '-') { path = a; break; } }

        namespace fs = std::filesystem;
        std::string root   = fs::absolute(path).string();
        auto& cfg = core::Config::instance();
        std::string dbPath  = cfg.projectDbPath(root);
        std::string pidPath = root + "/.icmg/watcher.pid";

        if (graph::Daemon::isRunning(pidPath)) {
            auto pid = graph::Daemon::readPid(pidPath);
            std::cout << "Watcher already running (PID " << pid << ")\n";
            return 0;
        }

        try {
            graph::Daemon::start(root, dbPath, pidPath);
            auto pid = graph::Daemon::readPid(pidPath);
            std::cout << "Watching: " << root << " (recursive)\n";
            std::cout << "PID: " << pid << " saved to " << pidPath << "\n";
            std::cout << "To stop: icmg graph-stop\n";
        } catch (const graph::DaemonError& e) {
            std::cerr << "icmg graph-watch: " << e.what() << "\n";
            return 1;
        }
        return 0;
    }
};

// ---- graph-stop ----
class GraphStopCommand : public BaseCommand {
public:
    std::string name()        const override { return "graph-stop"; }
    std::string description() const override { return "Stop file watcher daemon"; }

    int run(const std::vector<std::string>& args) override {
        std::string path = ".";
        for (auto& a : args) { if (!a.empty() && a[0] != '-') { path = a; break; } }

        namespace fs = std::filesystem;
        std::string root    = fs::absolute(path).string();
        std::string pidPath = root + "/.icmg/watcher.pid";

        if (!graph::Daemon::isRunning(pidPath)) {
            std::cout << "No watcher running.\n";
            return 0;
        }
        try {
            auto pid = graph::Daemon::readPid(pidPath);
            graph::Daemon::stop(pidPath);
            std::cout << "Stopped watcher (PID " << pid << ")\n";
        } catch (const graph::DaemonError& e) {
            std::cerr << "icmg graph-stop: " << e.what() << "\n";
            return 1;
        }
        return 0;
    }
};

// ---- graph-watch-status ----
class GraphWatchStatusCommand : public BaseCommand {
public:
    std::string name()        const override { return "graph-watch-status"; }
    std::string description() const override { return "Show watcher daemon status"; }

    int run(const std::vector<std::string>& args) override {
        std::string path = ".";
        for (auto& a : args) { if (!a.empty() && a[0] != '-') { path = a; break; } }
        bool json_out = hasFlag(args, "--json");

        namespace fs = std::filesystem;
        std::string root    = fs::absolute(path).string();
        std::string pidPath = root + "/.icmg/watcher.pid";

        bool running = graph::Daemon::isRunning(pidPath);
        auto pid     = graph::Daemon::readPid(pidPath);

        if (json_out) {
            std::cout << "{\"running\":" << (running ? "true" : "false")
                      << ",\"pid\":" << pid << "}\n";
        } else {
            std::cout << "Watcher: " << (running ? "RUNNING" : "STOPPED")
                      << " (PID " << (pid > 0 ? std::to_string(pid) : "none") << ")\n";
        }
        return 0;
    }
};

ICMG_REGISTER_COMMAND("graph-scan",         GraphScanCommand);
ICMG_REGISTER_COMMAND("graph-context",      GraphContextCommand);
ICMG_REGISTER_COMMAND("graph-related",      GraphRelatedCommand);
ICMG_REGISTER_COMMAND("graph-list",         GraphListCommand);
ICMG_REGISTER_COMMAND("graph-stats",        GraphStatsCommand);
ICMG_REGISTER_COMMAND("graph-impact",       GraphImpactCommand);
ICMG_REGISTER_COMMAND("graph-orphans",      GraphOrphansCommand);
ICMG_REGISTER_COMMAND("graph-cycles",       GraphCyclesCommand);
ICMG_REGISTER_COMMAND("graph-hot",          GraphHotCommand);
ICMG_REGISTER_COMMAND("graph-search",       GraphSearchCommand);
ICMG_REGISTER_COMMAND("graph-watch",        GraphWatchCommand);
ICMG_REGISTER_COMMAND("graph-stop",         GraphStopCommand);
ICMG_REGISTER_COMMAND("graph-watch-status", GraphWatchStatusCommand);

} // namespace icmg::cli
