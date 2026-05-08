#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../imem/memory_store.hpp"
#include <iostream>
#include <iomanip>
#include <chrono>
#include <algorithm>
#include <string>
#include <map>
#include <vector>

namespace icmg::cli {

static std::string mTimeAgo(int64_t epoch) {
    if (epoch <= 0) return "never";
    int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    int64_t diff = now - epoch;
    if (diff < 60)    return std::to_string(diff) + "s ago";
    if (diff < 3600)  return std::to_string(diff/60) + "m ago";
    if (diff < 86400) return std::to_string(diff/3600) + "h ago";
    return std::to_string(diff/86400) + "d ago";
}

static std::string truncStr(const std::string& s, size_t n) {
    if (s.size() <= n) return s;
    return s.substr(0, n - 1) + "…";
}

static std::string mImportanceLabel(int imp) {
    switch (imp) {
        case 0: return "low";
        case 1: return "med";
        case 2: return "high";
        case 3: return "critical";
        default: return "?";
    }
}

// =============================================================================
// Subcommands
// =============================================================================

class MemoryListCommand : public BaseCommand {
public:
    std::string name()        const override { return "memory-list"; }
    std::string description() const override { return "List memory nodes"; }

    int run(const std::vector<std::string>& args) override {
        int limit = 50;
        std::string topic_filter;
        bool json_out = false;
        for (size_t i = 0; i < args.size(); ++i) {
            if (args[i] == "--limit" && i + 1 < args.size()) {
                try { limit = std::stoi(args[++i]); } catch (...) {}
            } else if (args[i] == "--topic" && i + 1 < args.size()) {
                topic_filter = args[++i];
            } else if (args[i] == "--json") {
                json_out = true;
            }
        }

        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        imem::MemoryStore store(db);
        auto nodes = store.all();

        // Filter by topic prefix
        if (!topic_filter.empty()) {
            nodes.erase(std::remove_if(nodes.begin(), nodes.end(),
                [&](const imem::MemoryNode& n){
                    return n.topic.rfind(topic_filter, 0) != 0;
                }), nodes.end());
        }

        // Newest first
        std::sort(nodes.begin(), nodes.end(),
            [](const imem::MemoryNode& a, const imem::MemoryNode& b){
                return a.last_used > b.last_used;
            });
        if ((int)nodes.size() > limit) nodes.resize(limit);

        if (json_out) {
            std::cout << "[";
            for (size_t i = 0; i < nodes.size(); ++i) {
                if (i) std::cout << ",";
                const auto& n = nodes[i];
                std::cout << "{\"id\":" << n.id
                          << ",\"topic\":\""; for (char c : n.topic) {
                              if (c == '"') std::cout << "\\\""; else if (c == '\\') std::cout << "\\\\"; else std::cout << c;
                          } std::cout << "\""
                          << ",\"importance\":" << n.importance
                          << ",\"frequency\":" << n.frequency
                          << ",\"last_used\":" << n.last_used << "}";
            }
            std::cout << "]\n";
            return 0;
        }

        if (nodes.empty()) {
            std::cout << "No memory nodes found"
                      << (topic_filter.empty() ? "" : " for topic prefix '" + topic_filter + "'")
                      << ".\n";
            return 0;
        }

        std::cout << "Memory nodes (" << nodes.size() << ", "
                  << (topic_filter.empty() ? "all topics" : "topic: " + topic_filter)
                  << "):\n\n";
        for (auto& n : nodes) {
            std::cout << "  #" << std::left << std::setw(5) << n.id
                      << "[" << mImportanceLabel(n.importance) << "] "
                      << "freq=" << std::setw(3) << n.frequency
                      << " " << mTimeAgo(n.last_used) << "\n"
                      << "    " << truncStr(n.topic, 70) << "\n"
                      << "    \"" << truncStr(n.content, 80) << "\"\n";
        }
        return 0;
    }
};

class MemoryShowCommand : public BaseCommand {
public:
    std::string name()        const override { return "memory-show"; }
    std::string description() const override { return "Show full memory node by id"; }

    int run(const std::vector<std::string>& args) override {
        if (args.empty()) {
            std::cerr << "icmg memory show: requires <id>\n";
            return 1;
        }
        int64_t id;
        try { id = std::stoll(args[0]); } catch (...) {
            std::cerr << "icmg memory show: invalid id\n";
            return 1;
        }
        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        imem::MemoryStore store(db);
        auto n = store.get(id);
        if (n.id == 0) {
            std::cerr << "icmg memory show: not found (#" << id << ")\n";
            return 1;
        }
        std::cout << "ID         : " << n.id << "\n"
                  << "Topic      : " << n.topic << "\n"
                  << "Importance : " << mImportanceLabel(n.importance)
                  << " (" << n.importance << ")\n"
                  << "Frequency  : " << n.frequency << "\n"
                  << "Last used  : " << mTimeAgo(n.last_used) << "\n"
                  << "Created    : " << mTimeAgo(n.created_at) << "\n"
                  << "Keywords   : " << n.keywords << "\n"
                  << "Deleted    : " << (n.deleted_at > 0 ? "yes" : "no") << "\n"
                  << "\nContent:\n" << n.content << "\n";
        return 0;
    }
};

class MemoryStatsCommand : public BaseCommand {
public:
    std::string name()        const override { return "memory-stats"; }
    std::string description() const override { return "Show memory store statistics"; }

    int run(const std::vector<std::string>&) override {
        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        imem::MemoryStore store(db);
        auto nodes = store.all();

        int by_imp[4] = {0,0,0,0};
        int total_freq = 0;
        std::map<std::string,int> by_topic_prefix;
        for (auto& n : nodes) {
            int imp = std::max(0, std::min(3, n.importance));
            ++by_imp[imp];
            total_freq += n.frequency;
            // First word of topic = prefix bucket
            auto sp = n.topic.find_first_of(" :");
            std::string pfx = (sp == std::string::npos) ? n.topic : n.topic.substr(0, sp);
            ++by_topic_prefix[pfx];
        }

        std::cout << "=== Memory store stats ===\n"
                  << "Total nodes      : " << nodes.size() << "\n"
                  << "  critical       : " << by_imp[3] << "\n"
                  << "  high           : " << by_imp[2] << "\n"
                  << "  medium         : " << by_imp[1] << "\n"
                  << "  low            : " << by_imp[0] << "\n"
                  << "Total frequency  : " << total_freq << "\n";

        if (!by_topic_prefix.empty()) {
            std::vector<std::pair<std::string,int>> sorted(
                by_topic_prefix.begin(), by_topic_prefix.end());
            std::sort(sorted.begin(), sorted.end(),
                [](auto& a, auto& b){ return a.second > b.second; });
            std::cout << "\nTopic buckets (top 10):\n";
            for (size_t i = 0; i < sorted.size() && i < 10; ++i) {
                std::cout << "  " << std::left << std::setw(20) << sorted[i].first
                          << sorted[i].second << "\n";
            }
        }
        return 0;
    }
};

class MemoryPurgeCommand : public BaseCommand {
public:
    std::string name()        const override { return "memory-purge"; }
    std::string description() const override { return "Hard-delete soft-deleted nodes older than N days"; }

    int run(const std::vector<std::string>& args) override {
        int days = 30;
        for (size_t i = 0; i < args.size(); ++i) {
            if (args[i] == "--days" && i + 1 < args.size()) {
                try { days = std::stoi(args[++i]); } catch (...) {}
            }
        }
        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        imem::MemoryStore store(db);
        int n = store.purge(days);
        std::cout << "Purged " << n << " node(s) deleted >" << days << " days ago.\n";
        return 0;
    }
};

class MemorySearchCommand : public BaseCommand {
public:
    std::string name()        const override { return "memory-search"; }
    std::string description() const override { return "Search memory (alias for recall)"; }

    int run(const std::vector<std::string>& args) override {
        if (args.empty()) {
            std::cerr << "icmg memory search: requires <query>\n";
            return 1;
        }
        std::string query;
        for (auto& a : args) {
            if (!query.empty()) query += " ";
            query += a;
        }
        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        imem::MemoryStore store(db);
        auto results = store.recall(query, 10, false);
        if (results.empty()) {
            std::cout << "No matches.\n";
            return 0;
        }
        for (auto& n : results) {
            std::cout << "[" << std::fixed << std::setprecision(1) << n.score
                      << "] #" << n.id << " " << truncStr(n.topic, 60) << "\n"
                      << "    \"" << truncStr(n.content, 80) << "\"\n";
        }
        return 0;
    }
};

class MemoryHistoryCommand : public BaseCommand {
public:
    std::string name()        const override { return "memory-history"; }
    std::string description() const override { return "Show recent recall queries"; }

    int run(const std::vector<std::string>& args) override {
        int limit = 20;
        for (size_t i = 0; i < args.size(); ++i) {
            if (args[i] == "--limit" && i + 1 < args.size()) {
                try { limit = std::stoi(args[++i]); } catch (...) {}
            }
        }
        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        imem::MemoryStore store(db);
        auto history = store.queryHistory(limit);
        if (history.empty()) {
            std::cout << "No query history.\n";
            return 0;
        }
        for (size_t i = 0; i < history.size(); ++i) {
            std::cout << "  " << (i + 1) << ". " << history[i] << "\n";
        }
        return 0;
    }
};

// =============================================================================
// memory decay — periodically reduce importance of stale nodes (Phase: memoir+decay)
// =============================================================================
class MemoryDecayCommand : public BaseCommand {
public:
    std::string name()        const override { return "memory-decay"; }
    std::string description() const override { return "Reduce importance of stale memory nodes"; }

    void usage() const override {
        std::cout <<
            "Usage: icmg memory decay [options]\n\n"
            "Reduces importance of nodes not used in N days.\n\n"
            "Options:\n"
            "  --threshold-days N  Stale after N days (default 30)\n"
            "  --floor N           Minimum importance (default 0; pinned=3 won't decay)\n"
            "  --dry-run           Show counts; do not modify\n"
            "  --pinned-keep       Skip nodes with importance=3 (critical/pinned)\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (hasFlag(args, "--help")) { usage(); return 0; }
        int days  = 30;  try { days  = std::stoi(flagValue(args, "--threshold-days", "30")); } catch (...) {}
        int floor = 0;   try { floor = std::stoi(flagValue(args, "--floor", "0")); } catch (...) {}
        bool dry  = hasFlag(args, "--dry-run");
        bool pinned_keep = !hasFlag(args, "--no-pinned-keep");   // default keep

        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        int64_t cutoff = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count() - (int64_t)days * 86400;

        // Count candidates first.
        int candidates = 0, pinned = 0;
        std::string where = "deleted_at IS NULL AND last_used > 0 AND last_used < ? AND importance > ?";
        db.query("SELECT COUNT(*) FROM memory_nodes WHERE " + where,
                 {std::to_string(cutoff), std::to_string(floor)},
                 [&](const core::Row& r){ if (!r.empty()) candidates = std::stoi(r[0]); });
        if (pinned_keep) {
            db.query("SELECT COUNT(*) FROM memory_nodes WHERE " + where + " AND importance = 3",
                     {std::to_string(cutoff), std::to_string(floor)},
                     [&](const core::Row& r){ if (!r.empty()) pinned = std::stoi(r[0]); });
        }
        int affected = candidates - (pinned_keep ? pinned : 0);
        std::cout << "Decay candidates: " << candidates
                  << "  pinned-skipped: " << (pinned_keep ? pinned : 0)
                  << "  will decay: " << affected << "\n";
        if (dry) return 0;

        std::string upd = "UPDATE memory_nodes SET importance = importance - 1 "
                          "WHERE deleted_at IS NULL AND last_used > 0 AND last_used < ? AND importance > ?";
        if (pinned_keep) upd += " AND importance != 3";
        db.run(upd, {std::to_string(cutoff), std::to_string(floor)});
        std::cout << "Decayed " << affected << " nodes.\n";
        return 0;
    }
};

// =============================================================================
// Root dispatcher: `icmg memory <subcommand>`
// =============================================================================

class MemoryRootCommand : public BaseCommand {
public:
    std::string name()        const override { return "memory"; }
    std::string description() const override { return "Memory management (list, show, search, stats, purge)"; }

    void usage() const override {
        std::cout <<
            "Usage: icmg memory <subcommand> [options]\n\n"
            "Subcommands:\n"
            "  list [--topic <prefix>] [--limit N] [--json]   List memory nodes\n"
            "  show <id>                                       Show full node detail\n"
            "  search <query>                                  Search memory (= recall)\n"
            "  stats                                           Show store statistics\n"
            "  history [--limit N]                             Recent recall queries\n"
            "  forget <id>                                     Soft-delete node\n"
            "  restore <id>                                    Restore deleted node\n"
            "  purge [--days N]                                Hard-delete old soft-deleted (default 30d)\n"
            "  decay [--threshold-days N] [--dry-run]          Reduce importance of stale nodes\n"
            "\n"
            "See also: icmg store, icmg recall (top-level shortcuts).\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || args[0] == "--help" || args[0] == "-h") {
            usage();
            return 0;
        }
        std::string sub = args[0];
        std::vector<std::string> rest(args.begin() + 1, args.end());

        // Map shortcut → registered command name
        std::string registered;
        if      (sub == "list")    registered = "memory-list";
        else if (sub == "show")    registered = "memory-show";
        else if (sub == "search")  registered = "memory-search";
        else if (sub == "stats")   registered = "memory-stats";
        else if (sub == "history") registered = "memory-history";
        else if (sub == "purge")   registered = "memory-purge";
        else if (sub == "decay")   registered = "memory-decay";
        else if (sub == "health")  registered = "memory-health";
        else if (sub == "consolidate")     registered = "memory-consolidate";
        else if (sub == "extract-patterns")registered = "memory-extract-patterns";
        else if (sub == "forget")  registered = "forget";   // existing top-level
        else if (sub == "restore") registered = "restore";  // existing top-level
        else {
            std::cerr << "icmg memory: unknown subcommand: " << sub << "\n";
            usage();
            return 1;
        }

        auto& reg = icmg::core::Registry<icmg::cli::BaseCommand>::instance();
        if (!reg.has(registered)) {
            std::cerr << "icmg memory: handler missing: " << registered << "\n";
            return 1;
        }
        auto handler = reg.create(registered);
        return handler->run(rest);
    }
};

ICMG_REGISTER_COMMAND("memory",         MemoryRootCommand);
ICMG_REGISTER_COMMAND("memory-list",    MemoryListCommand);
ICMG_REGISTER_COMMAND("memory-show",    MemoryShowCommand);
ICMG_REGISTER_COMMAND("memory-stats",   MemoryStatsCommand);
ICMG_REGISTER_COMMAND("memory-search",  MemorySearchCommand);
ICMG_REGISTER_COMMAND("memory-history", MemoryHistoryCommand);
ICMG_REGISTER_COMMAND("memory-purge",   MemoryPurgeCommand);
ICMG_REGISTER_COMMAND("memory-decay",   MemoryDecayCommand);

} // namespace icmg::cli
