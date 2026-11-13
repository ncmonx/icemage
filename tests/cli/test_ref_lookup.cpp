// v2.30.0 Model-AI ref flow, lookup half: Model-AI ref section for the prompt
// hook and the B/C project filter over the registry.
#include "../test_main.hpp"
#include "../../src/cli/ref_lookup.hpp"
#include "../../src/core/db.hpp"
#include "../../src/imem/ai_ref_store.hpp"

#include <set>
#include <string>

using namespace icmg::cli::refflow;
using icmg::core::Db;

TEST("ref_lookup: section lists matching refs and marks them") {
    Db db(":memory:");
    icmg::imem::AiRefStore st(db);
    auto id = st.add({"vite dev server port already in use EADDRINUSE",
                      "kill stale node process | verified: npm test", "web", "", "npm test"});
    std::set<std::string> injected;
    RefSection s = modelAiRefSection(db, "EADDRINUSE port already in use on vite", 3,
                                     [&](const std::string& k) { return injected.count(k) > 0; },
                                     [&](const std::string& k) { injected.insert(k); });
    ASSERT_EQ(s.hits, 1);
    ASSERT_TRUE(s.best_score >= kRefMinCoverage);
    ASSERT_TRUE(s.text.find("ref:" + std::to_string(id)) != std::string::npos);
    ASSERT_TRUE(s.text.find("kill stale node process") != std::string::npos);
    ASSERT_TRUE(injected.count("ref:" + std::to_string(id)) == 1);

    icmg::imem::AiRef r;
    ASSERT_TRUE(st.get(id, r));
    ASSERT_EQ(r.used_count, 1);

    // second prompt in the same session: already injected -> skipped
    RefSection s2 = modelAiRefSection(db, "EADDRINUSE port already in use on vite", 3,
                                      [&](const std::string& k) { return injected.count(k) > 0; },
                                      [&](const std::string& k) { injected.insert(k); });
    ASSERT_EQ(s2.hits, 0);
    ASSERT_TRUE(s2.text.empty());
}

TEST("ref_lookup: no match gives empty section") {
    Db db(":memory:");
    icmg::imem::AiRefStore st(db);
    st.add({"vite dev server port already in use", "kill it", "web", "", ""});
    RefSection s = modelAiRefSection(db, "kubernetes helm chart rollback", 3, nullptr, nullptr);
    ASSERT_EQ(s.hits, 0);
    ASSERT_TRUE(s.text.empty());
}

TEST("ref_lookup: shareableProjects filters registry and fills db_path") {
    Db db(":memory:");
    db.run("CREATE TABLE projects(id INTEGER PRIMARY KEY, name TEXT, path TEXT, db_path TEXT)");
    db.run("INSERT INTO projects(name,path,db_path) VALUES"
           "('a','D:/Work/ProjA','D:/Work/ProjA/.icmg/data.db'),"
           "('b','D:/Work/ProjB',''),"
           "('t','C:/Temp/scratch','C:/Temp/scratch/.icmg/data.db'),"
           "('h','C:/Users/Me','C:/Users/Me/.icmg/data.db')");
    auto ps = shareableProjects(db, "C:/Users/Me", "D:/Work/ProjA");
    ASSERT_EQ(ps.size(), (size_t)1);
    ASSERT_EQ(ps[0].name, std::string("b"));
    ASSERT_TRUE(ps[0].db_path.find("data.db") != std::string::npos);
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
