// Phase 3 (graphify-parity): QueryEngine — turns a natural-language query into
// a bounded subgraph of the knowledge graph, then formats it as text context
// for an LLM (or prints it raw under --no-llm).
//
// Seed strategy: GraphStore::search(query) (FTS over path/context/symbols) gives
// seed nodes; BFS closure (depth N) expands the neighborhood; a hard maxNodes cap
// keeps the context bounded. No LLM dependency lives here — the command layer
// decides whether to feed formatSubGraph() to a model or print it directly.
#pragma once

#include <string>
#include <vector>

#include "../graph/graph_store.hpp"

namespace icmg::cli {

struct SubGraph {
    std::vector<graph::GraphNode> nodes;  // seed + closure, capped at maxNodes
    std::vector<int64_t>          seeds;  // seed node ids (BM25/FTS hits)
    bool                          truncated = false;  // hit maxNodes cap
};

class QueryEngine {
public:
    explicit QueryEngine(graph::GraphStore& g) : store_(g) {}

    // Seed via search(query), expand via BFS closure(depth), cap at maxNodes.
    SubGraph buildSubGraph(const std::string& query, int depth, int maxNodes);

    // Human/LLM-readable rendering of a subgraph (paths, kind, context, confidence).
    std::string formatSubGraph(const SubGraph& sg) const;

    // Describe a single node: its context + 1-hop neighbors (both directions).
    std::string explainNode(const std::string& path, int depth) const;

private:
    graph::GraphStore& store_;
};

}  // namespace icmg::cli
