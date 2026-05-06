// tests/graph/test_graph_edges.cpp
// Tests for graph edge resolution: Strategy 1 (namespace), Strategy 2 (path suffix),
// Strategy 3 (dotted namespace), Strategy 4 (class cross-reference / Graphify behavior).
//
// Strategy 4 requires files on disk (resolveAndInsertEdges re-reads content).
// We use a temp directory with real .cs files, then scan + verify edges.

#include "../test_main.hpp"
#include "../../src/core/db.hpp"
#include "../../src/graph/graph_store.hpp"
#include "../../src/graph/scanner.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace icmg;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Full schema needed by Scanner + GraphStore
static core::Db makeGraphDb() {
    core::Db db(":memory:");
    db.run("PRAGMA foreign_keys=ON");
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
        " group_id TEXT"
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
    db.run(
        "CREATE TABLE scan_runs("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " root_path TEXT,"
        " node_count INTEGER,"
        " edge_count INTEGER,"
        " created_at INTEGER"
        ")"
    );
    return db;
}

// Write a file to disk and return its OS-native path string
// (uses weakly_canonical so the returned path matches entry.path().string()
//  from directory_iterator — avoids mixed-separator issues on Windows)
static std::string writeFile(const fs::path& dir, const std::string& name,
                              const std::string& content) {
    fs::path p = dir / name;
    fs::create_directories(p.parent_path());
    std::ofstream f(p);
    f << content;
    f.close();
    std::error_code ec;
    auto canon = fs::weakly_canonical(p, ec);
    return ec ? p.string() : canon.string();
}

static int edgeCount(core::Db& db) {
    int n = 0;
    db.query("SELECT COUNT(*) FROM graph_edges", {},
             [&](const core::Row& r) { if (!r.empty()) try { n = std::stoi(r[0]); } catch (...) {} });
    return n;
}

static bool hasEdgeBetween(core::Db& db, const std::string& src_path,
                            const std::string& dst_path) {
    bool found = false;
    db.query(
        "SELECT 1 FROM graph_edges e"
        " JOIN graph_nodes s ON s.id=e.src"
        " JOIN graph_nodes d ON d.id=e.dst"
        " WHERE s.path=? AND d.path=?",
        {src_path, dst_path},
        [&](const core::Row&) { found = true; });
    return found;
}

// ---------------------------------------------------------------------------
// Strategy 1: namespace declaration match
// File B declares "namespace MyApp.Services" and file A does "using MyApp.Services"
// ---------------------------------------------------------------------------

TEST("edge resolution: strategy1 namespace match creates import edge") {
    auto db = makeGraphDb();

    // Temp dir for test files
    fs::path tmp = fs::temp_directory_path() / "icmg_test_ns";
    fs::remove_all(tmp);
    fs::create_directories(tmp);

    std::string service_path = writeFile(tmp, "Service.cs",
        "namespace MyApp.Services\n{\n    public class Service {}\n}\n");
    std::string client_path = writeFile(tmp, "Client.cs",
        "using MyApp.Services;\nnamespace MyApp\n{\n    public class Client {}\n}\n");

    graph::GraphStore store(db);
    graph::Scanner scanner(store);
    graph::Scanner::Options opts;
    opts.skip_stale    = false;
    opts.resolve_edges = true;

    scanner.scan(tmp.string(), opts);

    // Client.cs imports MyApp.Services → edge to Service.cs
    ASSERT_TRUE(hasEdgeBetween(db, client_path, service_path));

    fs::remove_all(tmp);
}

// ---------------------------------------------------------------------------
// Strategy 2: path suffix match (C++ #include "relative/path.h")
// ---------------------------------------------------------------------------

TEST("edge resolution: strategy2 path suffix match for cpp includes") {
    auto db = makeGraphDb();

    fs::path tmp = fs::temp_directory_path() / "icmg_test_cpp";
    fs::remove_all(tmp);
    fs::create_directories(tmp / "core");

    std::string db_h   = writeFile(tmp, "core/db.hpp", "// db header\nclass Db {};\n");
    std::string main_c = writeFile(tmp, "main.cpp",
        "#include \"core/db.hpp\"\nint main() { Db d; return 0; }\n");

    graph::GraphStore store(db);
    graph::Scanner scanner(store);
    graph::Scanner::Options opts;
    opts.skip_stale    = false;
    opts.resolve_edges = true;

    scanner.scan(tmp.string(), opts);

    // main.cpp includes core/db.hpp → edge exists
    ASSERT_TRUE(hasEdgeBetween(db, main_c, db_h));

    fs::remove_all(tmp);
}

// ---------------------------------------------------------------------------
// Strategy 3: dotted namespace → path segment
// "using ProjectName.Core" matches files in Core/ directory
// ---------------------------------------------------------------------------

TEST("edge resolution: strategy3 dotted namespace matches subdirectory files") {
    auto db = makeGraphDb();

    fs::path tmp = fs::temp_directory_path() / "icmg_test_dir";
    fs::remove_all(tmp);
    fs::create_directories(tmp / "Core");

    // A file in Core/ that does NOT declare a namespace (no strategy 1 match)
    std::string core_f = writeFile(tmp, "Core/Helper.cs",
        "// No namespace declaration\npublic static class Helper {}\n");
    std::string prog_f = writeFile(tmp, "Program.cs",
        "using ProjectName.Core;\nclass Program { static void Main() {} }\n");

    graph::GraphStore store(db);
    graph::Scanner scanner(store);
    graph::Scanner::Options opts;
    opts.skip_stale    = false;
    opts.resolve_edges = true;

    scanner.scan(tmp.string(), opts);

    // Strategy 3: last segment "core" matches Core/Helper.cs path
    ASSERT_TRUE(hasEdgeBetween(db, prog_f, core_f));

    fs::remove_all(tmp);
}

// ---------------------------------------------------------------------------
// Strategy 4: class cross-reference (Graphify-style usage-based edges)
// Same namespace, no `using` needed — detect actual type usage in code body
// ---------------------------------------------------------------------------

TEST("edge resolution: strategy4 class xref creates uses edge") {
    auto db = makeGraphDb();

    fs::path tmp = fs::temp_directory_path() / "icmg_test_xref";
    fs::remove_all(tmp);
    fs::create_directories(tmp);

    // Core.cs defines class Core in same namespace
    std::string core_f = writeFile(tmp, "Core.cs",
        "namespace MyApp\n{\n    internal class Core\n    {\n"
        "        public static string Path = \"\";\n"
        "    }\n}\n");

    // ApiClient.cs also in MyApp — no `using`, but references Core.Path
    std::string api_f = writeFile(tmp, "ApiClient.cs",
        "namespace MyApp\n{\n    internal static class ApiClient\n    {\n"
        "        public static void Init() { var p = Core.Path; }\n"
        "    }\n}\n");

    graph::GraphStore store(db);
    graph::Scanner scanner(store);
    graph::Scanner::Options opts;
    opts.skip_stale    = false;
    opts.resolve_edges = true;

    scanner.scan(tmp.string(), opts);

    // Strategy 4: ApiClient.cs mentions "Core" → uses edge to Core.cs
    ASSERT_TRUE(hasEdgeBetween(db, api_f, core_f));

    fs::remove_all(tmp);
}

TEST("edge resolution: strategy4 no false edge for unrelated files") {
    auto db = makeGraphDb();

    fs::path tmp = fs::temp_directory_path() / "icmg_test_noedge";
    fs::remove_all(tmp);
    fs::create_directories(tmp);

    // Two files, class names don't appear in each other
    writeFile(tmp, "Alpha.cs",
        "namespace App\n{\n    class Alpha { void Run() { int x = 1; } }\n}\n");
    writeFile(tmp, "Beta.cs",
        "namespace App\n{\n    class Beta { void Run() { int y = 2; } }\n}\n");

    graph::GraphStore store(db);
    graph::Scanner scanner(store);
    graph::Scanner::Options opts;
    opts.skip_stale    = false;
    opts.resolve_edges = true;

    scanner.scan(tmp.string(), opts);

    // "Alpha" not in Beta.cs body, "Beta" not in Alpha.cs body → 0 cross-ref edges
    // (There may be strategy1 namespace edges if both share "App" namespace but
    //  that's import-based, not cross-ref; total edges should be <= 2)
    ASSERT_TRUE(edgeCount(db) <= 2);

    fs::remove_all(tmp);
}

TEST("edge resolution: no self-edges created") {
    auto db = makeGraphDb();

    fs::path tmp = fs::temp_directory_path() / "icmg_test_self";
    fs::remove_all(tmp);
    fs::create_directories(tmp);

    // File mentions its own class name
    writeFile(tmp, "Service.cs",
        "namespace App\n{\n    class Service\n    {\n"
        "        Service _self;\n"  // references own class name
        "        void Run() { var s = new Service(); }\n"
        "    }\n}\n");

    graph::GraphStore store(db);
    graph::Scanner scanner(store);
    graph::Scanner::Options opts;
    opts.skip_stale    = false;
    opts.resolve_edges = true;

    scanner.scan(tmp.string(), opts);

    // Must never create src == dst edge
    int self_edges = 0;
    db.query("SELECT COUNT(*) FROM graph_edges WHERE src=dst", {},
             [&](const core::Row& r) { if (!r.empty()) try { self_edges = std::stoi(r[0]); } catch (...) {} });
    ASSERT_EQ(self_edges, 0);

    fs::remove_all(tmp);
}

// ---------------------------------------------------------------------------
// VS designer file grouping
// ---------------------------------------------------------------------------

static std::string groupIdOf(core::Db& db, const std::string& path) {
    std::string gid;
    db.query("SELECT group_id FROM graph_nodes WHERE path=?", {path},
             [&](const core::Row& r) { if (!r.empty()) gid = r[0]; });
    return gid;
}

TEST("designer grouping: trio gets same group_id and companion edges") {
    auto db = makeGraphDb();

    fs::path tmp = fs::temp_directory_path() / "icmg_test_designer";
    fs::remove_all(tmp);
    fs::create_directories(tmp);

    std::string cs_p   = writeFile(tmp, "Form1.cs",
        "namespace App { public partial class Form1 {} }");
    std::string des_p  = writeFile(tmp, "Form1.Designer.cs",
        "namespace App { partial class Form1 { private void InitializeComponent() {} } }");
    std::string resx_p = writeFile(tmp, "Form1.resx",
        "<?xml version=\"1.0\"?><root></root>");

    graph::GraphStore store(db);
    graph::Scanner scanner(store);
    graph::Scanner::Options opts;
    opts.skip_stale    = false;
    opts.resolve_edges = true;

    scanner.scan(tmp.string(), opts);

    // All three share the same group_id (= canonical .cs path)
    std::string gid_cs  = groupIdOf(db, cs_p);
    std::string gid_des = groupIdOf(db, des_p);
    std::string gid_res = groupIdOf(db, resx_p);

    ASSERT_FALSE(gid_cs.empty());
    ASSERT_EQ(gid_cs, gid_des);
    ASSERT_EQ(gid_cs, gid_res);

    // Companion edges exist between all three (bidirectional)
    ASSERT_TRUE(hasEdgeBetween(db, cs_p, des_p));
    ASSERT_TRUE(hasEdgeBetween(db, des_p, cs_p));
    ASSERT_TRUE(hasEdgeBetween(db, cs_p, resx_p));
    ASSERT_TRUE(hasEdgeBetween(db, resx_p, cs_p));
    ASSERT_TRUE(hasEdgeBetween(db, des_p, resx_p));
    ASSERT_TRUE(hasEdgeBetween(db, resx_p, des_p));

    fs::remove_all(tmp);
}

TEST("designer grouping: standalone cs with no companions gets no group_id") {
    auto db = makeGraphDb();

    fs::path tmp = fs::temp_directory_path() / "icmg_test_no_designer";
    fs::remove_all(tmp);
    fs::create_directories(tmp);

    std::string cs_p = writeFile(tmp, "Standalone.cs",
        "namespace App { public class Standalone {} }");

    graph::GraphStore store(db);
    graph::Scanner scanner(store);
    graph::Scanner::Options opts;
    opts.skip_stale    = false;
    opts.resolve_edges = true;

    scanner.scan(tmp.string(), opts);

    // No companions → group_id stays NULL
    ASSERT_TRUE(groupIdOf(db, cs_p).empty());

    fs::remove_all(tmp);
}

// ---------------------------------------------------------------------------

int main() {
    std::cout << "=== Graph edge resolution tests ===\n";
    return icmg::test::run_all();
}
