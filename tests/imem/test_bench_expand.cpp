// TDD (2026-11-11): query-time retrieval stages from the 2026-11-11 research
// (docs/plans/2026-11-11-token-memory-research.md):
//   RM3 pseudo-relevance feedback (--prf B), coverage-greedy head (--cover L)
#include "../test_main.hpp"
#include "../../src/imem/bench_expand.hpp"
#include "../../src/imem/bench_gate_trace.hpp"
#include "../../src/imem/bench_dataset_run.hpp"
#include "../../src/cli/bench_pipeline_flags.hpp"

#include <algorithm>
#include <map>

using namespace icmg::imem;

static bool hasTerm(const std::vector<PrfTerm>& t, const std::string& w) {
    return std::any_of(t.begin(), t.end(), [&](const PrfTerm& p) { return p.term == w; });
}

TEST("prf: rm3 terms weight by doc score and term share, skip query terms") {
    std::vector<std::vector<std::string>> docs = {
        {"corgi", "frisbee", "frisbee", "beach"},   // strong feedback doc
        {"corgi", "sourdough"}};                     // weak one
    auto t = rm3ExpansionTerms({"corgi"}, docs, {1.0, 0.1}, 10);
    ASSERT_FALSE(t.empty());
    ASSERT_EQ(t[0].term, std::string("frisbee"));   // 1.0 * 2/4 beats the rest
    ASSERT_FALSE(hasTerm(t, "corgi"));              // already in the query
    ASSERT_TRUE(hasTerm(t, "beach"));
    for (size_t i = 1; i < t.size(); ++i) ASSERT_TRUE(t[i - 1].weight >= t[i].weight);
}

TEST("prf: rm3 caps term count, drops short tokens, empty input safe") {
    std::vector<std::vector<std::string>> docs = {{"ab", "alpha", "beta", "gamma", "delta"}};
    auto t = rm3ExpansionTerms({}, docs, {1.0}, 2);
    ASSERT_EQ((int)t.size(), 2);
    ASSERT_FALSE(hasTerm(t, "ab"));
    ASSERT_TRUE(rm3ExpansionTerms({"x"}, {}, {}, 5).empty());
    ASSERT_TRUE(rm3ExpansionTerms({"x"}, docs, {}, 5).empty());   // size mismatch
}

TEST("prf: --prf parses, labels, is range-checked and turns the pipeline on") {
    BenchRunOptions opt;
    ASSERT_EQ(icmg::cli::parseBenchPipelineFlags({"--prf", "0.4"}, false, opt), std::string(""));
    ASSERT_TRUE(opt.prfBeta > 0.39 && opt.prfBeta < 0.41);
    ASSERT_TRUE(opt.pipeline());
    ASSERT_CONTAINS(describePipeline(opt), std::string("prf=0.40"));
    BenchRunOptions bad;
    ASSERT_FALSE(icmg::cli::parseBenchPipelineFlags({"--prf", "2"}, false, bad).empty());
}

TEST("prf: feedback pulls in an evidence doc with no query-word overlap") {
    BenchCorpus c;
    c.docs = {{"s1", "my corgi Biscuit loves frisbee at the beach", 1000},
              {"s2", "frisbee afternoon on the beach again, sunny", 2000},
              {"s3", "sourdough hydration and bread flour", 3000}};
    BenchQuestion q; q.id = "q"; q.category = "c";
    q.question = "corgi Biscuit"; q.evidence = {"s1", "s2"}; q.as_of = 5000;
    c.questions = {q};

    BenchRunOptions base; base.fusion = "blend";   // pipeline, bm25 only
    auto r0 = runBenchCorpus(c, {3}, base);
    ASSERT_FALSE(r0[0].ranked.empty());
    ASSERT_EQ(r0[0].ranked[0], std::string("s1"));
    ASSERT_TRUE(std::find(r0[0].ranked.begin(), r0[0].ranked.end(), "s2") == r0[0].ranked.end());

    BenchRunOptions prf = base; prf.prfBeta = 0.5;
    auto r1 = runBenchCorpus(c, {3}, prf);
    ASSERT_EQ(r1[0].ranked[0], std::string("s1"));   // original top stays on top
    ASSERT_TRUE(std::find(r1[0].ranked.begin(), r1[0].ranked.end(), "s2") != r1[0].ranked.end());
    ASSERT_TRUE(std::find(r1[0].ranked.begin(), r1[0].ranked.end(), "s3") == r1[0].ranked.end());
}

TEST("cover: greedy prefers a doc that covers the still-missing query term") {
    // q = {alice, paris}. d1 and d2 both say "alice" (near-duplicates, top
    // relevance); d3 is weaker but the only one mentioning "paris".
    std::vector<std::pair<std::string, double>> cand = {{"d1", 1.0}, {"d2", 0.95}, {"d3", 0.6}};
    std::map<std::string, std::vector<std::string>> toks = {
        {"d1", {"alice", "bought", "bread"}},
        {"d2", {"alice", "bought", "bread", "today"}},
        {"d3", {"paris", "trip", "flight"}}};
    auto out = coverSelect(cand, toks, {"alice", "paris"}, 1.0, 1.0, 3);
    ASSERT_EQ((int)out.size(), 3);
    ASSERT_EQ(out[0], std::string("d1"));
    ASSERT_EQ(out[1], std::string("d3"));   // jumps the duplicate d2
    ASSERT_EQ(out[2], std::string("d2"));
}

TEST("cover: lam=0 mu=0 keeps relevance order; budget limits the greedy head") {
    std::vector<std::pair<std::string, double>> cand = {{"a", 3}, {"b", 2}, {"c", 1}, {"d", 0.5}};
    std::map<std::string, std::vector<std::string>> toks = {
        {"a", {"x1"}}, {"b", {"x2"}}, {"c", {"qq"}}, {"d", {"zz"}}};
    auto same = coverSelect(cand, toks, {"qq"}, 0.0, 0.0, 4);
    ASSERT_TRUE(same == (std::vector<std::string>{"a", "b", "c", "d"}));
    auto b1 = coverSelect(cand, toks, {"qq", "zz"}, 5.0, 0.0, 2);   // greedy only 2 slots
    ASSERT_EQ(b1[0], std::string("c"));
    ASSERT_EQ(b1[1], std::string("d"));
    ASSERT_EQ((int)b1.size(), 4);                                   // tail kept in order
    ASSERT_EQ(b1[2], std::string("a"));
    ASSERT_TRUE(coverSelect({}, toks, {"q"}, 1, 1, 3).empty());
}

TEST("cover: --cover parses, labels and turns the pipeline on") {
    BenchRunOptions opt;
    ASSERT_EQ(icmg::cli::parseBenchPipelineFlags({"--cover", "0.5"}, false, opt), std::string(""));
    ASSERT_TRUE(opt.coverLambda > 0.49 && opt.coverLambda < 0.51);
    ASSERT_TRUE(opt.pipeline());
    ASSERT_CONTAINS(describePipeline(opt), std::string("cover=0.50"));
    BenchRunOptions bad;
    ASSERT_FALSE(icmg::cli::parseBenchPipelineFlags({"--cover", "-1"}, false, bad).empty());
}

TEST("gate: skip rerank only on a clear margin AND full query coverage by top-1") {
    const std::vector<std::string> q = {"corgi", "frisbee"};
    ASSERT_TRUE(rerankGateSkip(1.0, 0.4, {"corgi", "frisbee", "beach"}, q, 0.5));   // margin .6
    ASSERT_FALSE(rerankGateSkip(1.0, 0.7, {"corgi", "frisbee"}, q, 0.5));           // margin .3
    ASSERT_FALSE(rerankGateSkip(1.0, 0.1, {"corgi"}, q, 0.5));                      // misses frisbee
    ASSERT_FALSE(rerankGateSkip(0.0, 0.0, {"corgi", "frisbee"}, q, 0.5));           // no signal
    ASSERT_FALSE(rerankGateSkip(1.0, 0.1, {"corgi"}, {}, 0.5));                     // empty query
    ASSERT_FALSE(rerankGateSkip(1.0, 0.1, {"corgi", "frisbee"}, q, 0.0));           // gate off
}

TEST("gate: --rerank-gate parses and labels") {
    BenchRunOptions opt;
    ASSERT_EQ(icmg::cli::parseBenchPipelineFlags({"--rerank", "20", "--rerank-gate", "0.3"}, false, opt),
              std::string(""));
    ASSERT_TRUE(opt.rerankGate > 0.29 && opt.rerankGate < 0.31);
    ASSERT_CONTAINS(describePipeline(opt), std::string("gate=0.30"));
    BenchRunOptions bad;
    ASSERT_FALSE(icmg::cli::parseBenchPipelineFlags({"--rerank-gate", "1.5"}, false, bad).empty());
}

TEST("gate: queryCoverage is the share of distinct query terms in the doc") {
    const std::vector<std::string> q = {"corgi", "frisbee", "corgi", "beach", "park"};
    ASSERT_TRUE(queryCoverage({"corgi", "frisbee", "x"}, q) > 0.49 &&
                queryCoverage({"corgi", "frisbee", "x"}, q) < 0.51);   // 2 of 4 distinct
    ASSERT_TRUE(queryCoverage({"corgi", "frisbee", "beach", "park"}, q) > 0.99);
    ASSERT_TRUE(queryCoverage({"x"}, q) < 0.01);
    ASSERT_TRUE(queryCoverage({"corgi"}, {}) < 0.01);                  // empty query -> 0
}

TEST("gate trace: one JSON line per question with signals, pre/post ranks and gold") {
    GateTrace t;
    t.qid = "q1";
    t.category = "multi-session";
    t.bmS1 = 12.5; t.bmS2 = 7.0;
    t.fusedS1 = 0.9; t.fusedS2 = 0.6;
    t.bmCov = 1.0; t.fusedCov = 0.5;
    t.pre = {"a", "b"};
    t.post = {"b", "a"};
    t.gold = {"b"};
    const std::string s = gateTraceJson(t);
    ASSERT_TRUE(s.find('\n') == std::string::npos);                    // JSONL-safe
    ASSERT_CONTAINS(s, std::string("\"qid\":\"q1\""));
    ASSERT_CONTAINS(s, std::string("\"bm_s1\":12.5"));
    ASSERT_CONTAINS(s, std::string("\"pre\":[\"a\",\"b\"]"));
    ASSERT_CONTAINS(s, std::string("\"post\":[\"b\",\"a\"]"));
    ASSERT_CONTAINS(s, std::string("\"gold\":[\"b\"]"));
    ASSERT_CONTAINS(s, std::string("\"fused_cov\":0.5"));
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
