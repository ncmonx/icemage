// tests/graph/test_edge_confidence.cpp
//
// TDD tests for GraphEdge.confidence field + GraphStore read/write.
//
// Tests:
//   (1) upsertEdge writes confidence=EXTRACTED (default)
//   (2) upsertEdge writes confidence=INFERRED explicitly
//   (3) upsertEdge writes confidence=AMBIGUOUS explicitly
//   (4) edgesFrom returns correct confidence
//   (5) edgesTo returns correct confidence

#include "../test_main.hpp"
#include "../../src/core/db.hpp"
#include "../../src/graph/graph_store.hpp"
#include "../../src/graph/graph_node.hpp"
#include <filesystem>

namespace fs = std::filesystem;
using icmg::graph::GraphStore;
using icmg::graph::GraphNode;
using icmg::graph::GraphEdge;

struct ConfDbGuard {
    fs::path tmp;
    ConfDbGuard() {
        tmp = fs::temp_directory_path() / "test_edge_conf.db";
        fs::remove(tmp);
        icmg::core::ensureProjectDb(tmp.string());
#ifdef _WIN32
        _putenv_s("ICMG_PROJECT_DB", tmp.string().c_str());
#else
        setenv("ICMG_PROJECT_DB", tmp.string().c_str(), 1);
#endif
    }
    ~ConfDbGuard() {
#ifdef _WIN32
        _putenv_s("ICMG_PROJECT_DB", "");
#else
        unsetenv("ICMG_PROJECT_DB");
#endif
        fs::remove(tmp);
    }
};

static std::pair<int64_t,int64_t> seedNodes(GraphStore& gs) {
    GraphNode na; na.path = "src.cpp"; na.kind = "file"; na.file_hash = "h1";
    GraphNode nb; nb.path = "dst.cpp"; nb.kind = "file"; nb.file_hash = "h2";
    int64_t ia = gs.upsertNode(na);
    int64_t ib = gs.upsertNode(nb);
    return {ia, ib};
}

TEST("edge-confidence: default is EXTRACTED") {
    ConfDbGuard g;
    icmg::core::Db db(g.tmp.string());
    GraphStore gs(db);
    auto [ia, ib] = seedNodes(gs);

    GraphEdge e; e.src = ia; e.dst = ib; e.edge_type = "imports";
    // confidence not set -> default EXTRACTED
    gs.upsertEdge(e);

    auto edges = gs.edgesFrom(ia);
    ASSERT_TRUE(!edges.empty());
    ASSERT_EQ(edges[0].confidence, std::string("EXTRACTED"));
}

TEST("edge-confidence: write INFERRED") {
    ConfDbGuard g;
    icmg::core::Db db(g.tmp.string());
    GraphStore gs(db);
    auto [ia, ib] = seedNodes(gs);

    GraphEdge e; e.src = ia; e.dst = ib; e.edge_type = "calls";
    e.confidence = "INFERRED";
    gs.upsertEdge(e);

    auto edges = gs.edgesFrom(ia);
    ASSERT_TRUE(!edges.empty());
    ASSERT_EQ(edges[0].confidence, std::string("INFERRED"));
}

TEST("edge-confidence: write AMBIGUOUS") {
    ConfDbGuard g;
    icmg::core::Db db(g.tmp.string());
    GraphStore gs(db);
    auto [ia, ib] = seedNodes(gs);

    GraphEdge e; e.src = ia; e.dst = ib; e.edge_type = "calls";
    e.confidence = "AMBIGUOUS";
    gs.upsertEdge(e);

    auto edges = gs.edgesFrom(ia);
    ASSERT_TRUE(!edges.empty());
    ASSERT_EQ(edges[0].confidence, std::string("AMBIGUOUS"));
}

TEST("edge-confidence: edgesTo returns confidence") {
    ConfDbGuard g;
    icmg::core::Db db(g.tmp.string());
    GraphStore gs(db);
    auto [ia, ib] = seedNodes(gs);

    GraphEdge e; e.src = ia; e.dst = ib; e.edge_type = "imports";
    e.confidence = "INFERRED";
    gs.upsertEdge(e);

    auto edges = gs.edgesTo(ib);
    ASSERT_TRUE(!edges.empty());
    ASSERT_EQ(edges[0].confidence, std::string("INFERRED"));
}
