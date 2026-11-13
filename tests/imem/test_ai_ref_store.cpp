// v2.30.0 Model-AI ref flow: problem/solution store that lives in the Model-AI
// DB (exe-dir icmg-persona.db). Spec: docs/plans/2026-11-13-model-ai-ref-flow.md
#include "../test_main.hpp"
#include "../../src/imem/ai_ref_store.hpp"
#include "../../src/core/db.hpp"

#include <string>

using icmg::core::Db;
using icmg::imem::AiRef;
using icmg::imem::AiRefStore;

TEST("ai_ref_store: add and search") {
    Db db(":memory:");
    AiRefStore st(db);
    auto id = st.add({"msvc LNK1235 corrupt COFF symbol table on icemage_core.lib",
                      "delete the stale .lib and relink | verified: build.ps1",
                      "icemage", "", "build.ps1"});
    ASSERT_TRUE(id > 0);
    st.add({"python venv activation fails in powershell",
            "Set-ExecutionPolicy RemoteSigned", "tools", "", ""});
    auto hits = st.search("LNK1235 COFF symbol table", 5);
    ASSERT_TRUE(!hits.empty());
    ASSERT_EQ(hits[0].id, id);
    ASSERT_TRUE(hits[0].score > 0.0);
    ASSERT_EQ(hits[0].project, std::string("icemage"));
    ASSERT_EQ(st.count(), 2);
}

TEST("ai_ref_store: ranks best match first") {
    Db db(":memory:");
    AiRefStore st(db);
    st.add({"sqlite database is locked during write", "use WAL + busy_timeout", "a", "", ""});
    auto want = st.add({"sqlite fts5 syntax error on hyphen query",
                        "quote the query as an fts5 phrase", "b", "", ""});
    auto hits = st.search("fts5 syntax error hyphen", 5);
    ASSERT_TRUE(!hits.empty());
    ASSERT_EQ(hits[0].id, want);
}

TEST("ai_ref_store: operator chars do not throw") {
    Db db(":memory:");
    AiRefStore st(db);
    st.add({"crash on \"quoted\" path with - dash * star", "escape it", "p", "", ""});
    auto hits = st.search("\"quoted\" - dash * (star) AND OR NOT", 5);
    ASSERT_TRUE(!hits.empty());
    ASSERT_TRUE(st.search("", 5).empty());
}

TEST("ai_ref_store: dedup near duplicate problem") {
    Db db(":memory:");
    AiRefStore st(db);
    auto a = st.add({"hook reinstall empties settings.local.json on windows",
                     "atomic write", "icemage", "", ""});
    ASSERT_TRUE(st.isDuplicate("hook reinstall empties settings.local.json on Windows!"));
    ASSERT_FALSE(st.isDuplicate("update apply loses icmg.exe on windows"));
    auto b = st.add({"Hook reinstall empties settings.local.json on windows",
                     "newer", "icemage", "", ""});
    ASSERT_EQ(b, a);                     // dedup returns the existing id
    ASSERT_EQ(st.count(), 1);
}

TEST("ai_ref_store: mark used and get") {
    Db db(":memory:");
    AiRefStore st(db);
    auto id = st.add({"p1 problem text", "s1", "proj", "", ""});
    st.markUsed(id);
    st.markUsed(id);
    AiRef r;
    ASSERT_TRUE(st.get(id, r));
    ASSERT_EQ(r.used_count, 2);
    ASSERT_TRUE(r.last_used > 0);
    ASSERT_FALSE(st.get(id + 99, r));
}

TEST("ai_ref_store: rejects empty problem") {
    Db db(":memory:");
    AiRefStore st(db);
    ASSERT_EQ(st.add({"   ", "s", "p", "", ""}), (long long)0);
    ASSERT_EQ(st.count(), 0);
}

TEST("ai_ref_store: remove deletes row and its FTS entry") {
    Db db(":memory:");
    AiRefStore st(db);
    auto id = st.add({"stale smoke ref about a sqlite busy worker hang",
                      "busy_timeout 5000", "p", "", ""});
    ASSERT_TRUE(id > 0);
    ASSERT_TRUE(st.remove(id));
    ASSERT_EQ(st.count(), 0);
    ASSERT_TRUE(st.search("sqlite busy worker hang", 5).empty());
    ASSERT_FALSE(st.remove(id));   // already gone
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
