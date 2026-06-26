// Phase 3 (graphify-parity): QueryEngine implementation.
#include "graph_query_engine.hpp"

#include <algorithm>
#include <set>
#include <sstream>

namespace icmg::cli {

namespace {
std::string snippet(const std::string& s, std::size_t n) {
    if (s.size() <= n) return s;
    return s.substr(0, n) + "...";
}
}  // namespace

SubGraph QueryEngine::buildSubGraph(const std::string& query, int depth, int maxNodes) {
    SubGraph sg;
    if (maxNodes <= 0) maxNodes = 50;
    if (depth < 0) depth = 0;

    // 1. Seed: FTS/keyword search over the graph.
    auto seeds = store_.search(query, 10);

    std::set<int64_t>    seen;
    auto push = [&](const graph::GraphNode& n) -> bool {
        if (n.id == 0 || seen.count(n.id)) return false;
        if (static_cast<int>(sg.nodes.size()) >= maxNodes) { sg.truncated = true; return false; }
        seen.insert(n.id);
        sg.nodes.push_back(n);
        return true;
    };

    for (auto& s : seeds) {
        sg.seeds.push_back(s.id);
        push(s);
    }

    // 2. Expand: BFS closure (both directions) around each seed, depth-limited.
    for (int64_t seedId : sg.seeds) {
        if (static_cast<int>(sg.nodes.size()) >= maxNodes) { sg.truncated = true; break; }
        for (bool reverse : {false, true}) {
            auto levels = store_.closureByLevel(seedId, {}, depth, reverse);
            for (auto& lvl : levels) {
                for (auto& n : lvl) {
                    if (!push(n) && sg.truncated) break;
                }
                if (sg.truncated) break;
            }
            if (sg.truncated) break;
        }
    }
    return sg;
}

std::string QueryEngine::formatSubGraph(const SubGraph& sg) const {
    std::set<int64_t> seedSet(sg.seeds.begin(), sg.seeds.end());
    std::ostringstream os;
    os << "Subgraph: " << sg.nodes.size() << " node(s)"
       << (sg.truncated ? " (truncated at cap)" : "") << "\n";
    for (auto& n : sg.nodes) {
        os << (seedSet.count(n.id) ? "* " : "  ")
           << n.path;
        if (!n.kind.empty() && n.kind != "file") os << " [" << n.kind << "]";
        if (!n.symbol_name.empty()) os << " :: " << n.symbol_name;
        if (!n.context.empty()) os << " — " << snippet(n.context, 80);
        os << "\n";
    }
    return os.str();
}

std::string QueryEngine::explainNode(const std::string& path, int depth) const {
    std::ostringstream os;
    auto node = store_.getNode(path);
    if (!node) {
        os << "No node found for: " << path << "\n";
        return os.str();
    }
    os << "Node: " << node->path << "\n";
    if (!node->lang.empty())    os << "  lang: " << node->lang << "\n";
    if (!node->kind.empty())    os << "  kind: " << node->kind << "\n";
    if (!node->context.empty()) os << "  context: " << snippet(node->context, 200) << "\n";

    auto outE = store_.edgesFrom(node->id);
    auto inE  = store_.edgesTo(node->id);
    os << "  depends on (" << outE.size() << "):\n";
    {
        auto levels = store_.closureByLevel(node->id, {}, depth > 0 ? depth : 1, false);
        int level = 1;
        for (auto& lvl : levels) {
            for (auto& n : lvl) os << "    ->[" << level << "] " << n.path << "\n";
            ++level;
        }
    }
    os << "  used by (" << inE.size() << "):\n";
    {
        auto levels = store_.closureByLevel(node->id, {}, depth > 0 ? depth : 1, true);
        int level = 1;
        for (auto& lvl : levels) {
            for (auto& n : lvl) os << "    <-[" << level << "] " << n.path << "\n";
            ++level;
        }
    }
    return os.str();
}

}  // namespace icmg::cli
