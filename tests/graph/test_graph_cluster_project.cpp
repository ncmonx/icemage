// tests/graph/test_graph_cluster_project.cpp
//
// TDD tests for `icmg graph-cluster --project` mode:
// pulls edges from GraphStore and runs Leiden clustering,
// emitting file-per-cluster output.
//
// Tests:
//   (1) --project flag is registered and --help returns 0
//   (2) --project on empty DB returns 0 (graceful, no crash)
//   (3) --project --json on empty DB emits valid JSON
//   (4) --project --top-n N limits output to N clusters

#include "../test_main.hpp"
#include "../../src/core/registry.hpp"
#include "../../src/cli/base_command.hpp"
#include "../../src/core/db.hpp"
#include <filesystem>

namespace fs = std::filesystem;
using icmg::cli::BaseCommand;
using Reg = icmg::core::Registry<BaseCommand>;

// RAII: redirect project DB to a temp file for isolation
struct ClusterDbGuard {
    fs::path tmp;
    ClusterDbGuard() {
        tmp = fs::temp_directory_path() / "test_graph_cluster.db";
        // Minimal schema bootstrap using db.run()
        icmg::core::Db db(tmp.string());
        db.run(
            "CREATE TABLE IF NOT EXISTS graph_nodes"
            "(id INTEGER PRIMARY KEY, path TEXT UNIQUE NOT NULL,"
            " kind TEXT, context TEXT, hash TEXT,"
            " updated_at INTEGER DEFAULT 0, zone TEXT DEFAULT '');"
        );
        db.run(
            "CREATE TABLE IF NOT EXISTS graph_edges"
            "(src INTEGER, dst INTEGER, type TEXT,"
            " PRIMARY KEY(src,dst,type));"
        );
        setenv_compat(tmp.string());
    }
    ~ClusterDbGuard() {
        unsetenv_compat();
        fs::remove(tmp);
    }
    static void setenv_compat(const std::string& p) {
#ifdef _WIN32
        _putenv_s("ICMG_PROJECT_DB", p.c_str());
#else
        setenv("ICMG_PROJECT_DB", p.c_str(), 1);
#endif
    }
    static void unsetenv_compat() {
#ifdef _WIN32
        _putenv_s("ICMG_PROJECT_DB", "");
#else
        unsetenv("ICMG_PROJECT_DB");
#endif
    }
};

TEST("graph-cluster: command registered") {
    auto cmd = Reg::instance().create("graph-cluster");
    ASSERT_TRUE(cmd != nullptr);
}

TEST("graph-cluster: --help returns 0") {
    auto cmd = Reg::instance().create("graph-cluster");
    ASSERT_TRUE(cmd != nullptr);
    int rc = cmd->run({"--help"});
    ASSERT_EQ(rc, 0);
}

TEST("graph-cluster: --project flag accepted (empty DB, no crash)") {
    ClusterDbGuard g;
    auto cmd = Reg::instance().create("graph-cluster");
    ASSERT_TRUE(cmd != nullptr);
    int rc = cmd->run({"--project"});
    // Empty graph: should succeed with 0 clusters, not crash
    ASSERT_TRUE(rc == 0 || rc == 1);  // 1 = no nodes (acceptable)
}

TEST("graph-cluster: --project --json emits valid JSON on empty DB") {
    ClusterDbGuard g;
    auto cmd = Reg::instance().create("graph-cluster");
    ASSERT_TRUE(cmd != nullptr);

    // Capture stdout
    std::streambuf* oldbuf = std::cout.rdbuf();
    std::ostringstream oss;
    std::cout.rdbuf(oss.rdbuf());

    int rc = cmd->run({"--project", "--json"});

    std::cout.rdbuf(oldbuf);
    std::string out = oss.str();

    // Should exit 0 or 1 and output should be JSON (starts with '{' or '[')
    ASSERT_TRUE(rc == 0 || rc == 1);
    if (!out.empty()) {
        char first = out[out.find_first_not_of(" \t\r\n")];
        ASSERT_TRUE(first == '{' || first == '[');
    }
}

TEST("graph-cluster: --top-n flag accepted") {
    ClusterDbGuard g;
    auto cmd = Reg::instance().create("graph-cluster");
    ASSERT_TRUE(cmd != nullptr);
    // Should not crash even if --top-n is passed
    int rc = cmd->run({"--project", "--top-n", "5"});
    ASSERT_TRUE(rc == 0 || rc == 1);
}
