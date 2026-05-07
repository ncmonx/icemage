#pragma once
#include "../graph/graph_store.hpp"
#include "../core/db.hpp"
#include <string>
#include <vector>
#include <unordered_map>

namespace icmg::viz {

class GraphSerializer {
public:
    struct VizNode {
        int64_t     id = 0;
        std::string path;
        std::string label;       // basename only
        std::string lang;
        std::string context;
        std::string symbols;
        int64_t     size_bytes = 0;
        int         degree = 0;  // total in+out edges
        std::string community;   // connected-component id (string)
        std::string zone;        // Phase 17: subsystem partitioning
    };

    struct VizEdge {
        int64_t     src = 0;
        int64_t     dst = 0;
        std::string edge_type;
        double      weight = 1.0;
    };

    struct VizData {
        std::vector<VizNode>  nodes;
        std::vector<VizEdge>  edges;
        // community_id → CSS color string
        std::unordered_map<std::string, std::string> community_colors;
    };

    explicit GraphSerializer(core::Db& db);

    // Load nodes + edges, compute communities, return VizData.
    // If lang_filter non-empty: only include nodes whose lang is in the set.
    // If community_filter non-empty: only include that community.
    VizData serialize(const std::vector<std::string>& lang_filter = {},
                      const std::string& community_filter = "",
                      const std::string& zone_filter = "") const;

    // Render VizData to JSON string for embedding in HTML.
    std::string toJson(const VizData& data) const;

private:
    core::Db& db_;

    // BFS connected-component labelling
    void computeCommunities(VizData& data) const;

    static std::string langColor(const std::string& lang);
    static std::string communityPalette(int idx);
    static std::string basename(const std::string& path);
    static std::string escJson(const std::string& s);
};

} // namespace icmg::viz
