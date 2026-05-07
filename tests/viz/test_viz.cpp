#include "../test_main.hpp"
#include "../../src/core/db.hpp"
#include "../../src/viz/graph_serializer.hpp"
#include "../../src/viz/dot_exporter.hpp"
#include "../../src/viz/gexf_exporter.hpp"
#include "../../src/viz/graphml_exporter.hpp"
#include "../../src/viz/html_template.hpp"

using namespace icmg;

// ---------------------------------------------------------------------------
// Helper: in-memory DB with graph schema
// ---------------------------------------------------------------------------
static core::Db makeDb() {
    core::Db db(":memory:");
    db.run(
        "CREATE TABLE graph_nodes("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " path TEXT NOT NULL UNIQUE,"
        " lang TEXT,"
        " context TEXT,"
        " symbols TEXT,"
        " size_bytes INTEGER,"
        " file_hash TEXT,"
        " access_count INTEGER NOT NULL DEFAULT 0,"
        " updated_at INTEGER NOT NULL DEFAULT (strftime('%s','now')),"
        " zone TEXT NOT NULL DEFAULT 'default'"
        ")"
    );
    db.run(
        "CREATE TABLE graph_edges("
        " src INTEGER NOT NULL REFERENCES graph_nodes(id) ON DELETE CASCADE,"
        " dst INTEGER NOT NULL REFERENCES graph_nodes(id) ON DELETE CASCADE,"
        " edge_type TEXT NOT NULL,"
        " weight REAL NOT NULL DEFAULT 1.0,"
        " PRIMARY KEY(src,dst,edge_type)"
        ")"
    );
    return db;
}

static void addNode(core::Db& db, const std::string& path, const std::string& lang,
                     int64_t size = 1000) {
    db.run("INSERT OR IGNORE INTO graph_nodes(path,lang,context,symbols,size_bytes,file_hash,updated_at)"
           " VALUES(?,?,?,?,?,?,0)",
           {path, lang, "context of " + path, "{}", std::to_string(size), ""});
}

static void addEdge(core::Db& db, const std::string& src, const std::string& dst,
                     const std::string& etype = "imports") {
    int64_t sid = 0, did = 0;
    db.query("SELECT id FROM graph_nodes WHERE path=?", {src},
             [&](const core::Row& r) { if (!r.empty()) try { sid = std::stoll(r[0]); } catch (...) {} });
    db.query("SELECT id FROM graph_nodes WHERE path=?", {dst},
             [&](const core::Row& r) { if (!r.empty()) try { did = std::stoll(r[0]); } catch (...) {} });
    if (sid && did)
        db.run("INSERT OR IGNORE INTO graph_edges(src,dst,edge_type,weight) VALUES(?,?,?,1.0)",
               {std::to_string(sid), std::to_string(did), etype});
}

// ---------------------------------------------------------------------------
// GraphSerializer tests
// ---------------------------------------------------------------------------

TEST("serializer: empty graph returns empty nodes+edges") {
    auto db = makeDb();
    viz::GraphSerializer ser(db);
    auto data = ser.serialize();
    ASSERT_EQ(data.nodes.size(), 0u);
    ASSERT_EQ(data.edges.size(), 0u);
}

TEST("serializer: nodes loaded with correct fields") {
    auto db = makeDb();
    addNode(db, "src/main.cpp", "cpp", 2048);
    addNode(db, "src/db.hpp",   "cpp", 512);

    viz::GraphSerializer ser(db);
    auto data = ser.serialize();
    ASSERT_EQ(data.nodes.size(), 2u);

    bool foundMain = false;
    for (auto& n : data.nodes) {
        if (n.path == "src/main.cpp") {
            foundMain = true;
            ASSERT_EQ(n.label, std::string("main.cpp"));
            ASSERT_EQ(n.lang,  std::string("cpp"));
            ASSERT_EQ(n.size_bytes, (int64_t)2048);
        }
    }
    ASSERT_TRUE(foundMain);
}

TEST("serializer: edges loaded + degree computed") {
    auto db = makeDb();
    addNode(db, "src/a.cpp", "cpp");
    addNode(db, "src/b.hpp", "cpp");
    addEdge(db, "src/a.cpp", "src/b.hpp", "imports");

    viz::GraphSerializer ser(db);
    auto data = ser.serialize();
    ASSERT_EQ(data.edges.size(), 1u);

    // a.cpp has degree 1 (out), b.hpp has degree 1 (in)
    for (auto& n : data.nodes) {
        ASSERT_EQ(n.degree, 1);
    }
}

TEST("serializer: lang filter works") {
    auto db = makeDb();
    addNode(db, "src/a.cpp", "cpp");
    addNode(db, "src/b.py",  "python");
    addNode(db, "src/c.go",  "go");

    viz::GraphSerializer ser(db);
    auto data = ser.serialize({"cpp", "go"});
    ASSERT_EQ(data.nodes.size(), 2u);
    for (auto& n : data.nodes) {
        ASSERT_TRUE(n.lang == "cpp" || n.lang == "go");
    }
}

TEST("serializer: community detection assigns communities") {
    auto db = makeDb();
    // Two disconnected components
    addNode(db, "src/a.cpp", "cpp");
    addNode(db, "src/b.cpp", "cpp");
    addNode(db, "src/c.py",  "python");
    addNode(db, "src/d.py",  "python");
    addEdge(db, "src/a.cpp", "src/b.cpp");
    addEdge(db, "src/c.py",  "src/d.py");

    viz::GraphSerializer ser(db);
    auto data = ser.serialize();

    ASSERT_EQ(data.nodes.size(), 4u);

    // Two communities
    std::string comm_a, comm_c;
    for (auto& n : data.nodes) {
        if (n.path == "src/a.cpp") comm_a = n.community;
        if (n.path == "src/c.py")  comm_c = n.community;
    }
    ASSERT_TRUE(!comm_a.empty());
    ASSERT_TRUE(!comm_c.empty());
    ASSERT_TRUE(comm_a != comm_c);  // different components
}

TEST("serializer: toJson produces valid structure") {
    auto db = makeDb();
    addNode(db, "src/a.cpp", "cpp", 1000);
    addNode(db, "src/b.hpp", "cpp", 500);
    addEdge(db, "src/a.cpp", "src/b.hpp");

    viz::GraphSerializer ser(db);
    auto data = ser.serialize();
    std::string json = ser.toJson(data);

    ASSERT_TRUE(!json.empty());
    ASSERT_TRUE(json.find("\"nodes\"") != std::string::npos);
    ASSERT_TRUE(json.find("\"edges\"") != std::string::npos);
    ASSERT_TRUE(json.find("src/a.cpp") != std::string::npos);
}

// ---------------------------------------------------------------------------
// DotExporter tests
// ---------------------------------------------------------------------------

TEST("dot_exporter: generates valid DOT header") {
    auto db = makeDb();
    addNode(db, "src/main.cpp", "cpp");

    viz::DotExporter exp(db);
    std::string dot = exp.toDot();

    ASSERT_TRUE(dot.find("digraph icmg") != std::string::npos);
    ASSERT_TRUE(dot.find("src/main.cpp") != std::string::npos);
}

TEST("dot_exporter: edge appears in output") {
    auto db = makeDb();
    addNode(db, "src/a.cpp", "cpp");
    addNode(db, "src/b.hpp", "cpp");
    addEdge(db, "src/a.cpp", "src/b.hpp", "imports");

    viz::DotExporter exp(db);
    std::string dot = exp.toDot();

    ASSERT_TRUE(dot.find("->") != std::string::npos);
    ASSERT_TRUE(dot.find("imports") != std::string::npos);
}

TEST("dot_exporter: lang filter") {
    auto db = makeDb();
    addNode(db, "src/a.cpp", "cpp");
    addNode(db, "src/b.py",  "python");

    viz::DotExporter exp(db);
    std::string dot = exp.toDot({"python"});

    ASSERT_TRUE(dot.find("b.py") != std::string::npos);
    ASSERT_TRUE(dot.find("a.cpp") == std::string::npos);
}

// ---------------------------------------------------------------------------
// GEXF / GraphML tests
// ---------------------------------------------------------------------------

TEST("gexf_exporter: generates XML with nodes and edges") {
    auto db = makeDb();
    addNode(db, "src/a.cpp", "cpp");
    addNode(db, "src/b.hpp", "cpp");
    addEdge(db, "src/a.cpp", "src/b.hpp");

    viz::GexfExporter exp(db);
    std::string gexf = exp.toGexf();

    ASSERT_TRUE(gexf.find("<gexf") != std::string::npos);
    ASSERT_TRUE(gexf.find("<node") != std::string::npos);
    ASSERT_TRUE(gexf.find("<edge") != std::string::npos);
}

TEST("graphml_exporter: generates GraphML with nodes and edges") {
    auto db = makeDb();
    addNode(db, "src/a.cpp", "cpp");
    addNode(db, "src/b.hpp", "cpp");
    addEdge(db, "src/a.cpp", "src/b.hpp", "calls");

    viz::GraphmlExporter exp(db);
    std::string gml = exp.toGraphml();

    ASSERT_TRUE(gml.find("<graphml") != std::string::npos);
    ASSERT_TRUE(gml.find("<node") != std::string::npos);
    ASSERT_TRUE(gml.find("<edge") != std::string::npos);
    ASSERT_TRUE(gml.find("calls") != std::string::npos);
}

// ---------------------------------------------------------------------------
// HTML template test
// ---------------------------------------------------------------------------

TEST("html_template: contains expected structure") {
    std::string json = "{\"nodes\":[],\"edges\":[],\"communities\":{}}";
    std::string html = viz::buildHtml(json, "test-project");

    ASSERT_TRUE(html.find("<!DOCTYPE html>") != std::string::npos);
    ASSERT_TRUE(html.find("cytoscape") != std::string::npos);
    ASSERT_TRUE(html.find("test-project") != std::string::npos);
    ASSERT_TRUE(html.find("GRAPH_DATA") != std::string::npos);
    // JSON data embedded
    ASSERT_TRUE(html.find("\"nodes\":[]") != std::string::npos);
}

TEST("html_template: title with special chars escaped") {
    std::string json = "{\"nodes\":[],\"edges\":[],\"communities\":{}}";
    std::string html = viz::buildHtml(json, "project\"name");

    // The " should be escaped in the JS string literal
    ASSERT_TRUE(html.find("\\\"") != std::string::npos);
}

int main() {
    std::cout << "=== Visual Graph tests ===\n";
    return icmg::test::run_all();
}
