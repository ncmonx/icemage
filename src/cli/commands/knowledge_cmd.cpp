// icmg knowledge — unified browser + CRUD for context_nodes (rules/context/skills).
//
// Subcommands:
//   list  [--type context|skill|hot|cold|all] [--inactive] [--json]
//   get   <node_key>
//   add   --title TITLE --content TEXT [--tier hot|cold|skill] [--tags JSON]
//         [--source FILE]
//   edit  <node_key> [--title T] [--content C] [--tier T] [--tags J] [--active yes|no]
//   delete <node_key> [--confirm]
//   --html  Open knowledge dashboard in browser via `icmg serve`.

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../core/context_node_store.hpp"
#include "../../core/exec_utils.hpp"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <algorithm>

using namespace icmg::core;

namespace icmg::cli {

static std::string slugify(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        if (std::isalnum(c)) out += static_cast<char>(std::tolower(c));
        else if (!out.empty() && out.back() != '-') out += '-';
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    return out;
}

// ---- subcommands -----------------------------------------------------------

static int doList(ContextNodeStore& store, const std::vector<std::string>& args) {
    std::string type_filter;
    bool show_inactive = false;
    bool json_out = false;

    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--type" && i+1 < args.size()) type_filter = args[++i];
        else if (args[i] == "--inactive") show_inactive = true;
        else if (args[i] == "--json")     json_out      = true;
    }

    // Map type aliases to tier values
    std::string tier;
    if (type_filter == "skill")   tier = "skill";
    else if (type_filter == "hot")  tier = "hot";
    else if (type_filter == "cold") tier = "cold";
    else if (type_filter == "context") tier = ""; // cold + hot
    // "all" or empty → no tier filter

    auto nodes = store.list(tier, !show_inactive);

    if (json_out) {
        std::cout << "[\n";
        for (size_t i = 0; i < nodes.size(); ++i) {
            auto& n = nodes[i];
            std::cout << "  {\"key\":\"" << n.node_key
                      << "\",\"title\":\"" << n.title
                      << "\",\"tier\":\"" << n.tier
                      << "\",\"active\":" << (n.active ? "true" : "false")
                      << ",\"source\":\"" << n.source_file << "\"}"
                      << (i+1 < nodes.size() ? "," : "") << "\n";
        }
        std::cout << "]\n";
        return 0;
    }

    // Group by tier for readable output
    std::vector<std::string> tiers = {"hot", "cold", "skill"};
    for (auto& t : tiers) {
        std::vector<ContextNode> group;
        for (auto& n : nodes) if (n.tier == t) group.push_back(n);
        if (group.empty()) continue;

        std::cout << "\n[" << t << "]\n";
        std::cout << std::string(60, '-') << "\n";
        for (auto& n : group) {
            std::cout << std::left
                      << std::setw(35) << n.node_key.substr(0, 34)
                      << std::setw(5)  << (n.active ? "" : "[off]")
                      << n.title << "\n";
        }
    }
    std::cout << "\ntotal: " << nodes.size() << " node(s)\n";
    return 0;
}

static int doGet(ContextNodeStore& store, const std::vector<std::string>& args) {
    if (args.empty()) { std::cerr << "knowledge get: missing node_key\n"; return 1; }
    auto node = store.get(args[0]);
    if (!node) { std::cerr << "knowledge get: not found: " << args[0] << "\n"; return 1; }

    std::cout << "key    : " << node->node_key    << "\n";
    std::cout << "title  : " << node->title       << "\n";
    std::cout << "tier   : " << node->tier        << "\n";
    std::cout << "active : " << (node->active ? "yes" : "no") << "\n";
    std::cout << "source : " << node->source_file << "\n";
    std::cout << "tags   : " << node->tags        << "\n";
    std::cout << "\n" << node->content << "\n";
    return 0;
}

static int doAdd(ContextNodeStore& store, const std::vector<std::string>& args) {
    std::string title, content, tier = "cold", tags = "[]", source;

    for (size_t i = 0; i < args.size(); ++i) {
        if      (args[i] == "--title"   && i+1 < args.size()) title   = args[++i];
        else if (args[i] == "--content" && i+1 < args.size()) content = args[++i];
        else if (args[i] == "--tier"    && i+1 < args.size()) tier    = args[++i];
        else if (args[i] == "--tags"    && i+1 < args.size()) tags    = args[++i];
        else if (args[i] == "--source"  && i+1 < args.size()) source  = args[++i];
    }

    if (title.empty())   { std::cerr << "knowledge add: --title required\n"; return 1; }
    if (content.empty()) { std::cerr << "knowledge add: --content required\n"; return 1; }
    if (tier != "hot" && tier != "cold" && tier != "skill") {
        std::cerr << "knowledge add: --tier must be hot|cold|skill\n"; return 1;
    }

    ContextNode node;
    node.node_key    = slugify(title);
    node.title       = title;
    node.content     = content;
    node.tier        = tier;
    node.tags        = tags;
    node.source_file = source;
    node.active      = true;

    store.upsert(node);
    std::cout << "added: " << node.node_key << " [" << tier << "]\n";
    return 0;
}

static int doEdit(ContextNodeStore& store, const std::vector<std::string>& args) {
    if (args.empty()) { std::cerr << "knowledge edit: missing node_key\n"; return 1; }
    std::string key = args[0];

    auto existing = store.get(key);
    if (!existing) { std::cerr << "knowledge edit: not found: " << key << "\n"; return 1; }

    ContextNode node = *existing;
    for (size_t i = 1; i < args.size(); ++i) {
        if      (args[i] == "--title"   && i+1 < args.size()) node.title       = args[++i];
        else if (args[i] == "--content" && i+1 < args.size()) node.content     = args[++i];
        else if (args[i] == "--tier"    && i+1 < args.size()) node.tier        = args[++i];
        else if (args[i] == "--tags"    && i+1 < args.size()) node.tags        = args[++i];
        else if (args[i] == "--source"  && i+1 < args.size()) node.source_file = args[++i];
        else if (args[i] == "--active"  && i+1 < args.size()) {
            node.active = (args[++i] != "no" && args[i] != "false" && args[i] != "0");
        }
    }

    store.upsert(node);
    std::cout << "updated: " << key << "\n";
    return 0;
}

static int doDelete(ContextNodeStore& store, const std::vector<std::string>& args) {
    if (args.empty()) { std::cerr << "knowledge delete: missing node_key\n"; return 1; }
    std::string key = args[0];
    bool confirm = false;
    for (auto& a : args) if (a == "--confirm") confirm = true;

    if (!store.get(key)) { std::cerr << "knowledge delete: not found: " << key << "\n"; return 1; }

    if (!confirm) {
        std::cerr << "knowledge delete: add --confirm to delete '" << key << "'\n";
        return 1;
    }

    store.remove(key);
    std::cout << "deleted: " << key << "\n";
    return 0;
}

// ---- command ---------------------------------------------------------------

class KnowledgeCommand : public BaseCommand {
public:
    std::string name()        const override { return "knowledge"; }
    std::string description() const override { return "Browse + manage context_nodes (rules/context/skills)"; }

    void usage() const override {
        std::cout <<
            "Usage: icmg knowledge <subcommand> [options]\n\n"
            "Subcommands:\n"
            "  list  [--type context|skill|hot|cold|all] [--inactive] [--json]\n"
            "  get   <node_key>\n"
            "  add   --title TITLE --content TEXT [--tier hot|cold|skill]\n"
            "        [--tags JSON_ARRAY] [--source FILE]\n"
            "  edit  <node_key> [--title T] [--content C] [--tier T]\n"
            "        [--tags J] [--active yes|no]\n"
            "  delete <node_key> [--confirm]\n"
            "  --html  Open knowledge browser in `icmg serve` dashboard\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || args[0] == "--help" || args[0] == "-h") {
            usage(); return 0;
        }

        // --html: launch serve and open browser
        if (args[0] == "--html") {
            auto r = core::safeExecShell("icmg serve 2>&1 &", false, 1000);
            std::cout << "opening http://127.0.0.1:8080/knowledge\n";
            core::safeExecShell(
#ifdef _WIN32
                "start http://127.0.0.1:8080/knowledge",
#elif defined(__APPLE__)
                "open http://127.0.0.1:8080/knowledge",
#else
                "xdg-open http://127.0.0.1:8080/knowledge 2>/dev/null &",
#endif
                false, 2000);
            return 0;
        }

        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        ContextNodeStore store(db);

        const std::string& sub = args[0];
        std::vector<std::string> rest(args.begin() + 1, args.end());

        if (sub == "list")   return doList(store, rest);
        if (sub == "get")    return doGet(store, rest);
        if (sub == "add")    return doAdd(store, rest);
        if (sub == "edit")   return doEdit(store, rest);
        if (sub == "delete") return doDelete(store, rest);

        std::cerr << "knowledge: unknown subcommand '" << sub << "'. Try --help.\n";
        return 1;
    }
};

ICMG_REGISTER_COMMAND("knowledge", KnowledgeCommand);

} // namespace icmg::cli
