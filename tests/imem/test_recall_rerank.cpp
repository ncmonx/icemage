// 2026-11-01: opt-in cross-encoder rerank for live recall (CLI / MCP / hook).
#include "../test_main.hpp"
#include "../../src/imem/recall_rerank.hpp"

using icmg::imem::MemoryNode;
using icmg::imem::reorderHeadByScore;
using icmg::imem::rerankDocText;
using icmg::imem::parseRerankN;

static std::vector<MemoryNode> mk(std::initializer_list<int64_t> ids) {
    std::vector<MemoryNode> v;
    for (auto id : ids) { MemoryNode n; n.id = id; n.topic = "t" + std::to_string(id); v.push_back(n); }
    return v;
}

TEST("recall-rerank: head reordered by score, tail untouched") {
    auto v = reorderHeadByScore(mk({1, 2, 3, 4}), {0.1f, 0.9f, 0.5f}, 3);
    ASSERT_EQ(v[0].id, (int64_t)2);
    ASSERT_EQ(v[1].id, (int64_t)3);
    ASSERT_EQ(v[2].id, (int64_t)1);
    ASSERT_EQ(v[3].id, (int64_t)4);
}

TEST("recall-rerank: mismatched or empty scores keep BM25 order") {
    auto v = reorderHeadByScore(mk({1, 2, 3}), {}, 3);
    ASSERT_EQ(v[0].id, (int64_t)1);
    auto w = reorderHeadByScore(mk({1, 2, 3}), {0.9f}, 3);
    ASSERT_EQ(w[0].id, (int64_t)1);
    ASSERT_EQ(w[2].id, (int64_t)3);
}

TEST("recall-rerank: doc text = topic + content, length capped") {
    MemoryNode n; n.topic = "fix-x"; n.content = std::string(5000, 'a');
    std::string d = rerankDocText(n);
    ASSERT_TRUE(d.rfind("fix-x: ", 0) == 0);
    ASSERT_TRUE(d.size() <= 2100);
}

TEST("recall-rerank: parseRerankN clamps and rejects junk") {
    ASSERT_EQ(parseRerankN(""), 0);
    ASSERT_EQ(parseRerankN("abc"), 0);
    ASSERT_EQ(parseRerankN("-3"), 0);
    ASSERT_EQ(parseRerankN("20"), 20);
    ASSERT_EQ(parseRerankN("999"), 100);
}

TEST("recall-rerank: fetch limit over-fetches only when on") {
    ASSERT_EQ(icmg::imem::rerankFetchLimit(5, 0), 5);
    ASSERT_EQ(icmg::imem::rerankFetchLimit(5, 20), 20);
    ASSERT_EQ(icmg::imem::rerankFetchLimit(30, 20), 30);
}

TEST("recall-rerank: n=0 is a no-op cut to limit (never loads a model)") {
    auto v = icmg::imem::applyRecallRerank("q", mk({1, 2, 3, 4}), 0, 2);
    ASSERT_EQ(v.size(), (size_t)2);
    ASSERT_EQ(v[0].id, (int64_t)1);
    ASSERT_EQ(v[1].id, (int64_t)2);
}

TEST("recall-rerank: long memory -> capped passages, short memory -> one") {
    MemoryNode s; s.topic = "fix"; s.content = "short note about the cat";
    auto ps = icmg::imem::rerankPassages("cat", s);
    ASSERT_EQ(ps.size(), (size_t)1);
    ASSERT_TRUE(ps[0].rfind("fix: ", 0) == 0);
    MemoryNode l; l.topic = "log";
    for (int i = 0; i < 2000; ++i) l.content += (i == 1500 ? "zebra " : "word ");
    auto pl = icmg::imem::rerankPassages("where is the zebra", l);
    ASSERT_EQ(pl.size(), (size_t)icmg::imem::kRecallRerankPassages);
    bool found = false;
    for (auto& p : pl) found = found || p.find("zebra") != std::string::npos;
    ASSERT_TRUE(found);   // the evidence window deep in the doc is kept
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
