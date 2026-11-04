// TDD (2026-09-23): bench-recall --replay -- IDE-A from the landscape scan
// (docs/plans/2026-09-23-ai-agi-landscape-scan.md; DolphinBench arXiv
// 2609.24971: judge memory by TASK outcome + COST, not just accuracy).
// Replays REAL past recall queries and scores the brain's current answer:
// hit-rate (does it still find what it found?) and token cost (what does an
// answer cost to inject?). Pure aggregation logic -- no DB/IO here.
#include "../test_main.hpp"
#include "../../src/imem/bench_replay.hpp"

using namespace icmg::imem;

static ReplayRow rrow(const std::string& q, int past, int now, int64_t chars) {
    ReplayRow r; r.query = q; r.past_hits = past; r.now_hits = now;
    r.result_chars = chars;
    return r;
}

// 1. Hit = the query still returns at least one result now.
TEST("replay: hit rate counts queries that still resolve") {
    std::vector<ReplayRow> rows = {
        rrow("daemon lifecycle", 3, 2, 400),
        rrow("vulkan hang", 1, 0, 0),        // regressed -> miss
        rrow("release checklist", 2, 5, 900),
    };
    auto s = computeReplayStats(rows);
    ASSERT_EQ(s.total, 3);
    ASSERT_EQ(s.hits, 2);
    ASSERT_TRUE(s.hit_rate > 0.66 && s.hit_rate < 0.67);
}

// 2. Regressions are singled out: had results before, none now.
TEST("replay: regressed queries listed") {
    std::vector<ReplayRow> rows = {
        rrow("a query that worked", 4, 0, 0),
        rrow("still fine", 1, 1, 100),
    };
    auto s = computeReplayStats(rows);
    ASSERT_EQ((int)s.regressed.size(), 1);
    ASSERT_EQ(s.regressed[0], std::string("a query that worked"));
}

// 3. Token cost: ~chars/4, averaged over queries WITH results.
TEST("replay: token estimate averages over answered queries") {
    std::vector<ReplayRow> rows = {
        rrow("q1", 1, 1, 800),    // ~200 tok
        rrow("q2", 1, 2, 1600),   // ~400 tok
        rrow("q3", 1, 0, 0),      // no answer -> excluded from avg
    };
    auto s = computeReplayStats(rows);
    ASSERT_EQ(s.avg_answer_tokens, 300);
    ASSERT_EQ(s.total_answer_tokens, 600);
}

// 4. Empty input -> zeroed stats, no crash, hit_rate 0.
TEST("replay: empty input safe") {
    auto s = computeReplayStats({});
    ASSERT_EQ(s.total, 0);
    ASSERT_EQ(s.hits, 0);
    ASSERT_TRUE(s.hit_rate == 0.0);
    ASSERT_EQ(s.avg_answer_tokens, 0);
}

// ---------------------------------------------------------------------------
// 2026-10-29: standard AI-memory benchmarks (LongMemEval-S, LoCoMo) -- Tier 1
// retrieval-only. Both datasets ship gold evidence ids (LongMemEval
// answer_session_ids / has_answer turns, LoCoMo evidence dia_id), so recall
// quality is measured deterministically, no LLM reader/judge.
// ---------------------------------------------------------------------------
#include "../../src/imem/bench_dataset.hpp"
#include "../../src/imem/bench_metrics.hpp"
#include "../../src/imem/bench_dataset_run.hpp"
#include "../../src/cli/bench_pipeline_flags.hpp"

static bool hasDoc(const BenchCorpus& c, const std::string& id) {
    for (const auto& d : c.docs) if (d.id == id) return true;
    return false;
}

// 5. Top-level array splitter: braces/brackets inside strings + escapes do not
//    break object boundaries (streams a 270MB file without one giant DOM).
TEST("bench-dataset: top-level object splitter respects strings") {
    std::string text = R"([ {"a":"x}{","b":[1,{"c":2}]}, {"d":"q\"}"} ,{}])";
    std::vector<std::string> objs;
    int n = forEachTopLevelObject(text, [&](const std::string& o) { objs.push_back(o); });
    ASSERT_EQ(n, 3);
    ASSERT_EQ(objs[0], std::string(R"({"a":"x}{","b":[1,{"c":2}]})"));
    ASSERT_EQ(objs[1], std::string(R"({"d":"q\"}"})"));
    ASSERT_EQ(objs[2], std::string("{}"));
}

// 6. Date parsing for both dataset formats -> UTC epoch.
TEST("bench-dataset: parses LongMemEval and LoCoMo date formats") {
    // 2023-05-20 02:21 UTC = 1684549260
    ASSERT_EQ(parseBenchDate("2023/05/20 (Sat) 02:21"), (int64_t)1684549260);
    // 2023-05-08 13:56 UTC = 1683554160
    ASSERT_EQ(parseBenchDate("1:56 pm on 8 May, 2023"), (int64_t)1683554160);
    // 12 am -> hour 0 ; 12 pm -> hour 12
    ASSERT_EQ(parseBenchDate("12:00 am on 1 January, 2024"), (int64_t)1704067200);
    ASSERT_EQ(parseBenchDate("12:30 pm on 1 Jan, 2024"), (int64_t)1704112200);
    ASSERT_EQ(parseBenchDate("not a date"), (int64_t)0);
}

// 7. LongMemEval instance -> one corpus, session-level docs + evidence.
static const char* kLme = R"({
  "question_id":"q1","question_type":"single-session-user",
  "question":"What breed is my dog?","answer":"corgi",
  "question_date":"2023/06/01 (Thu) 10:00",
  "haystack_session_ids":["s_a","s_b"],
  "haystack_dates":["2023/05/20 (Sat) 02:21","2023/05/21 (Sun) 09:00"],
  "haystack_sessions":[
    [{"role":"user","content":"I adopted a corgi named Biscuit","has_answer":true},
     {"role":"assistant","content":"Congrats!"}],
    [{"role":"user","content":"Help me plan a trip to Kyoto"},
     {"role":"assistant","content":"Sure"}]],
  "answer_session_ids":["s_a"]})";

TEST("bench-dataset: LongMemEval session granularity") {
    auto c = parseLongMemEval(kLme, BenchGranularity::Session);
    ASSERT_EQ((int)c.docs.size(), 2);
    ASSERT_TRUE(hasDoc(c, "s_a"));
    ASSERT_CONTAINS(c.docs[0].text, std::string("corgi"));
    ASSERT_EQ(c.docs[0].date, (int64_t)1684549260);
    ASSERT_EQ((int)c.questions.size(), 1);
    const auto& q = c.questions[0];
    ASSERT_EQ(q.category, std::string("single-session-user"));
    ASSERT_FALSE(q.abstention);
    ASSERT_EQ((int)q.evidence.size(), 1);
    ASSERT_EQ(q.evidence[0], std::string("s_a"));
    ASSERT_TRUE(q.as_of >= c.docs[1].date);
}

TEST("bench-dataset: LongMemEval turn granularity uses has_answer") {
    auto c = parseLongMemEval(kLme, BenchGranularity::Turn);
    ASSERT_EQ((int)c.docs.size(), 4);
    ASSERT_TRUE(hasDoc(c, "s_a#0"));
    ASSERT_EQ((int)c.questions[0].evidence.size(), 1);
    ASSERT_EQ(c.questions[0].evidence[0], std::string("s_a#0"));
}

// 2026-11-03: ICM's README LongMemEval number indexes each SESSION built from
// the user's turns only. user-session reproduces that unit for a fair compare.
TEST("bench-dataset: LongMemEval user-session keeps only user turns") {
    auto c = parseLongMemEval(kLme, BenchGranularity::UserSession);
    ASSERT_EQ((int)c.docs.size(), 2);
    ASSERT_TRUE(hasDoc(c, "s_a"));
    ASSERT_CONTAINS(c.docs[0].text, std::string("corgi"));
    ASSERT_TRUE(c.docs[0].text.find("Congrats") == std::string::npos);
    ASSERT_TRUE(c.docs[1].text.find("Sure") == std::string::npos);
    ASSERT_EQ(c.docs[0].date, (int64_t)1684549260);
    ASSERT_EQ((int)c.questions[0].evidence.size(), 1);
    ASSERT_EQ(c.questions[0].evidence[0], std::string("s_a"));
}

TEST("bench-dataset: granularity names parse") {
    BenchGranularity g{};
    ASSERT_TRUE(parseBenchGranularity("session", g) && g == BenchGranularity::Session);
    ASSERT_TRUE(parseBenchGranularity("turn", g) && g == BenchGranularity::Turn);
    ASSERT_TRUE(parseBenchGranularity("user-session", g) && g == BenchGranularity::UserSession);
    ASSERT_FALSE(parseBenchGranularity("bogus", g));
}

TEST("bench-dataset: LongMemEval _abs ids are abstention") {
    std::string j = kLme;
    j.replace(j.find("\"q1\""), 4, "\"q1_abs\"");
    auto c = parseLongMemEval(j, BenchGranularity::Session);
    ASSERT_TRUE(c.questions[0].abstention);
}

// 8. LoCoMo sample -> one corpus, many questions; D<s>:<t> evidence maps to
//    turn ids or to session ids depending on granularity.
static const char* kLoco = R"({
  "sample_id":"conv-1",
  "conversation":{
    "speaker_a":"Caroline","speaker_b":"Melanie",
    "session_1_date_time":"1:56 pm on 8 May, 2023",
    "session_1":[{"speaker":"Caroline","dia_id":"D1:1","text":"I went to a LGBTQ support group"},
                 {"speaker":"Melanie","dia_id":"D1:2","text":"That sounds great",
                  "blip_caption":"a photo of a rainbow flag"}],
    "session_2_date_time":"9:00 am on 20 May, 2023",
    "session_2":[{"speaker":"Melanie","dia_id":"D2:1","text":"I painted a sunrise"}]},
  "qa":[{"question":"When did Caroline go to the support group?","answer":"7 May 2023",
         "evidence":["D1:1"],"category":2},
        {"question":"What did Melanie paint?","answer":"sunrise",
         "evidence":["D2:1; D1:2"],"category":4},
        {"question":"What did Caroline paint?","adversarial_answer":"sunrise",
         "evidence":["D2:1"],"category":5}]})";

TEST("bench-dataset: LoCoMo turn granularity") {
    auto c = parseLoCoMo(kLoco, BenchGranularity::Turn);
    ASSERT_EQ((int)c.docs.size(), 3);
    ASSERT_TRUE(hasDoc(c, "D1:2"));
    ASSERT_CONTAINS(c.docs[1].text, std::string("rainbow flag"));  // image caption kept
    ASSERT_EQ((int)c.questions.size(), 3);
    ASSERT_EQ(c.questions[0].category, std::string("locomo-cat2"));
    ASSERT_EQ((int)c.questions[1].evidence.size(), 2);  // "D2:1; D1:2" split
    ASSERT_TRUE(c.questions[2].abstention);              // cat 5 adversarial
}

TEST("bench-dataset: LoCoMo session granularity dedups evidence") {
    auto c = parseLoCoMo(kLoco, BenchGranularity::Session);
    ASSERT_EQ((int)c.docs.size(), 2);
    ASSERT_TRUE(hasDoc(c, "S1"));
    ASSERT_EQ(c.docs[0].date, (int64_t)1683554160);
    ASSERT_EQ((int)c.questions[1].evidence.size(), 2);  // S2, S1
    ASSERT_EQ((int)c.questions[0].evidence.size(), 1);
    ASSERT_EQ(c.questions[0].evidence[0], std::string("S1"));
}

// 9. Metrics: recall_any / recall_all / binary NDCG at k.
TEST("bench-metrics: recall any/all and ndcg") {
    std::vector<std::string> ranked = {"x", "a", "y", "b"};
    std::vector<std::string> gold   = {"a", "b"};
    ASSERT_TRUE(recallAnyAtK(ranked, gold, 1) == 0.0);
    ASSERT_TRUE(recallAnyAtK(ranked, gold, 2) == 1.0);
    ASSERT_TRUE(recallAllAtK(ranked, gold, 2) == 0.0);
    ASSERT_TRUE(recallAllAtK(ranked, gold, 4) == 1.0);
    // dcg = 1/log2(3) + 1/log2(5) ; idcg = 1 + 1/log2(3)
    double nd = ndcgAtK(ranked, gold, 4);
    ASSERT_TRUE(nd > 0.6508 && nd < 0.6510);
    ASSERT_TRUE(ndcgAtK({"a", "b"}, gold, 2) == 1.0);
    ASSERT_TRUE(ndcgAtK({}, gold, 5) == 0.0);
}

// 10. Aggregation: per-category + overall means; abstention and evidence-less
//     questions are excluded (LongMemEval paper convention) but counted.
TEST("bench-metrics: aggregate skips abstention, groups by category") {
    std::vector<BenchQResult> rs;
    BenchQResult a; a.category = "temporal"; a.recall_any = {{5, 1.0}}; a.recall_all = {{5, 1.0}}; a.ndcg = {{5, 1.0}};
    BenchQResult b; b.category = "temporal"; b.recall_any = {{5, 0.0}}; b.recall_all = {{5, 0.0}}; b.ndcg = {{5, 0.0}};
    BenchQResult c; c.category = "multi";    c.recall_any = {{5, 1.0}}; c.recall_all = {{5, 0.0}}; c.ndcg = {{5, 0.5}};
    BenchQResult d; d.category = "multi";    d.skipped = true;
    rs = {a, b, c, d};
    auto agg = aggregateBench(rs, {5});
    ASSERT_EQ(agg.scored, 3);
    ASSERT_EQ(agg.skipped, 1);
    ASSERT_TRUE(agg.overall.recall_any.at(5) > 0.666 && agg.overall.recall_any.at(5) < 0.667);
    ASSERT_EQ(agg.by_category.at("temporal").n, 2);
    ASSERT_TRUE(agg.by_category.at("temporal").recall_any.at(5) == 0.5);
    ASSERT_TRUE(agg.by_category.at("multi").ndcg.at(5) == 0.5);
}

// 11. End-to-end on an isolated in-memory brain: the evidence doc (only one
//     mentioning the distinctive term) must rank first; docs dated AFTER the
//     question are invisible (as-of recall).
TEST("bench-run: isolated corpus ranks evidence first, honours as_of") {
    BenchCorpus c;
    c.docs = {{"s1", "We talked about sourdough starters and hydration", 1000},
              {"s2", "My corgi Biscuit loves the beach", 2000},
              {"s3", "Weekend plans: hiking and a movie", 3000},
              {"s4", "Biscuit the corgi learned a new trick", 9000}};  // after question
    BenchQuestion q; q.id = "q"; q.category = "single";
    q.question = "what is the name of my corgi"; q.evidence = {"s2"}; q.as_of = 5000;
    c.questions = {q};
    auto res = runBenchCorpus(c, {1, 3});
    ASSERT_EQ((int)res.size(), 1);
    ASSERT_FALSE(res[0].skipped);
    ASSERT_EQ(res[0].ranked.size() > 0, true);
    ASSERT_EQ(res[0].ranked[0], std::string("s2"));
    for (const auto& id : res[0].ranked) ASSERT_TRUE(id != "s4");
    ASSERT_TRUE(res[0].recall_any.at(1) == 1.0);
}

// 2026-11-02: 41/470 LongMemEval_s questions have evidence sessions dated
// AFTER question_date (dataset quirk); the as-of view hides them = sure miss.
// --no-as-of runs every question over the full corpus (what ICM/MemPalace do).
TEST("bench-run: noAsOf shows docs dated after the question") {
    BenchCorpus c;
    c.docs = {{"s1", "We talked about sourdough starters and hydration", 1000},
              {"s4", "Biscuit the corgi learned a new trick", 9000}};  // after question
    BenchQuestion q; q.id = "q"; q.category = "single";
    q.question = "what trick did my corgi Biscuit learn"; q.evidence = {"s4"}; q.as_of = 5000;
    c.questions = {q};
    auto hidden = runBenchCorpus(c, {1});
    ASSERT_TRUE(hidden[0].recall_any.at(1) == 0.0);
    BenchRunOptions opt; opt.noAsOf = true;
    auto shown = runBenchCorpus(c, {1}, opt);
    ASSERT_EQ(shown[0].ranked[0], std::string("s4"));
    ASSERT_TRUE(shown[0].recall_any.at(1) == 1.0);
}

// 2026-11-03: all 30 LongMemEval_s _abs questions still carry evidence
// sessions and ICM / MemPalace score all 500. scoreAbstention matches that.
TEST("bench-run: scoreAbstention scores _abs questions that have evidence") {
    BenchCorpus c;
    c.docs = {{"s1", "Biscuit the corgi learned a new trick", 1000},
              {"s2", "We talked about sourdough starters", 1000}};
    BenchQuestion q; q.id = "q_abs"; q.category = "single"; q.abstention = true;
    q.question = "what trick did my corgi Biscuit learn"; q.evidence = {"s1"}; q.as_of = 5000;
    BenchQuestion none = q; none.id = "q2_abs"; none.evidence.clear();
    c.questions = {q, none};
    auto def = runBenchCorpus(c, {1});
    ASSERT_TRUE(def[0].skipped);
    BenchRunOptions opt; opt.scoreAbstention = true;
    auto all = runBenchCorpus(c, {1}, opt);
    ASSERT_FALSE(all[0].skipped);
    ASSERT_TRUE(all[0].recall_any.at(1) == 1.0);
    ASSERT_TRUE(all[1].skipped);   // still nothing to score without evidence
    BenchRunOptions p;
    ASSERT_EQ(icmg::cli::parseBenchPipelineFlags({"--score-abstention"}, false, p), std::string());
    ASSERT_TRUE(p.scoreAbstention);
    ASSERT_FALSE(p.pipeline());
}

TEST("bench-run: --no-as-of flag parses") {
    BenchRunOptions opt;
    ASSERT_EQ(icmg::cli::parseBenchPipelineFlags({"--no-as-of"}, false, opt), std::string());
    ASSERT_TRUE(opt.noAsOf);
    ASSERT_FALSE(opt.pipeline());   // retrieval mode unchanged, only visibility
}

TEST("bench-run: daemon/atomize isolation is scoped and restored") {
    const char* orig = std::getenv("ICMG_NO_DAEMON");
    std::string saved = orig ? orig : "";
#ifdef _WIN32
    _putenv_s("ICMG_NO_DAEMON", "sentinel");
#else
    setenv("ICMG_NO_DAEMON", "sentinel", 1);
#endif
    BenchCorpus c;
    c.docs = {{"s1", "corgi named Biscuit", 1000}};
    BenchQuestion q; q.id = "q"; q.category = "c"; q.question = "corgi";
    q.evidence = {"s1"}; q.as_of = 2000;
    c.questions = {q};
    (void)runBenchCorpus(c, {1});
    const char* v = std::getenv("ICMG_NO_DAEMON");
    ASSERT_TRUE(v != nullptr);
    ASSERT_EQ(std::string(v), std::string("sentinel"));
#ifdef _WIN32
    _putenv_s("ICMG_NO_DAEMON", saved.c_str());
#else
    if (saved.empty()) unsetenv("ICMG_NO_DAEMON"); else setenv("ICMG_NO_DAEMON", saved.c_str(), 1);
#endif
}

// 2026-11-03: long runs (batch7 = 6348s) printed nothing until the final
// JSON, so nobody could tell how far along they were or when they would end.
TEST("bench-run: progress line shows done/total, percent and ETA") {
    auto s = formatBenchProgress(50, 500, 120.0);
    ASSERT_TRUE(s.find("50/500") != std::string::npos);
    ASSERT_TRUE(s.find("10%") != std::string::npos);
    ASSERT_TRUE(s.find("120s elapsed") != std::string::npos);
    ASSERT_TRUE(s.find("~1080s left") != std::string::npos);
    ASSERT_TRUE(formatBenchProgress(0, 500, 3.0).find("left") == std::string::npos);
    ASSERT_TRUE(formatBenchProgress(500, 500, 900.0).find("100%") != std::string::npos);
}

TEST("bench-run: k list parsing and report formatting") {
    auto ks = parseBenchKs("10,1,5,5,x");
    ASSERT_EQ((int)ks.size(), 3);
    ASSERT_EQ(ks[0], 1);
    ASSERT_EQ(ks[2], 10);
    ASSERT_EQ((int)parseBenchKs("").size(), 3);
    BenchFileReport rep; rep.dataset = "locomo"; rep.corpora = 1;
    BenchQResult r; r.category = "locomo-cat1"; r.recall_any = {{1, 1.0}};
    rep.results = {r};
    std::string txt = formatBenchReport(rep, {1}, false);
    ASSERT_CONTAINS(txt, std::string("OVERALL"));
    ASSERT_CONTAINS(txt, std::string("locomo-cat1"));
    std::string js = formatBenchReport(rep, {1}, true);
    ASSERT_CONTAINS(js, std::string("\"recall_any@1\": 1.0"));
}

TEST("bench-run: semantic mode keeps as_of isolation and ranks evidence") {
    BenchCorpus c;
    c.docs = {{"s1", "We talked about sourdough starters and hydration", 1000},
              {"s2", "My corgi Biscuit loves the beach", 2000},
              {"s4", "Biscuit the corgi learned a new trick", 9000}};
    BenchQuestion q; q.id = "q"; q.category = "single";
    q.question = "what is the name of my corgi"; q.evidence = {"s2"}; q.as_of = 5000;
    c.questions = {q};
    BenchRunOptions opt; opt.semantic = true; opt.alpha = 0.5;
    auto res = runBenchCorpus(c, {1, 3}, opt);
    ASSERT_EQ((int)res.size(), 1);
    ASSERT_FALSE(res[0].ranked.empty());
    ASSERT_EQ(res[0].ranked[0], std::string("s2"));
    for (const auto& id : res[0].ranked) ASSERT_TRUE(id != "s4");
}

// 2026-10-31: experimental retrieval stages (RRF, chunking, temporal, MMR).
#include "../../src/imem/bench_retrieval.hpp"
#include "../../src/imem/bench_pipeline.hpp"
#include "../../src/cli/bench_pipeline_flags.hpp"

TEST("bench-retrieval: rrf rewards agreement across lists") {
    auto f = rrfFuse({{"a", "b", "c"}, {"b", "c", "a"}});
    ASSERT_EQ(f[0].first, std::string("b"));      // 1/62 + 1/61 beats 1/61 + 1/63
    ASSERT_EQ((int)f.size(), 3);
    ASSERT_TRUE(f[0].second > f[1].second);
}

TEST("bench-retrieval: rrf keeps ids seen in only one list") {
    auto f = rrfFuse({{"a"}, {"z"}});
    ASSERT_EQ((int)f.size(), 2);
    ASSERT_EQ(f[0].first, std::string("a"));      // tie -> first seen
}

TEST("bench-retrieval: chunkWords windows with overlap") {
    auto c = chunkWords("w1 w2 w3 w4 w5 w6 w7", 3, 1);
    ASSERT_EQ((int)c.size(), 3);                  // w1-3, w3-5, w5-7
    ASSERT_EQ(c[0], std::string("w1 w2 w3"));
    ASSERT_EQ(c[1], std::string("w3 w4 w5"));
    ASSERT_EQ(c[2], std::string("w5 w6 w7"));
    ASSERT_EQ((int)chunkWords("", 3, 1).size(), 0);
    ASSERT_EQ((int)chunkWords("short text", 50, 10).size(), 1);
}

TEST("bench-retrieval: firstPersonView keeps only user lines") {
    std::string t = "user: I adopted a corgi\nassistant: Nice!\nuser: His name is Biscuit\n";
    ASSERT_EQ(firstPersonView(t), std::string("I adopted a corgi\nHis name is Biscuit\n"));
}

TEST("bench-retrieval: temporal hint resolves relative phrases") {
    const int64_t day = 86400, asOf = 1700000000;
    auto w = parseTemporalHint("What did I buy last week?", asOf);
    ASSERT_TRUE(w.found);
    ASSERT_TRUE(w.lo <= asOf - 7 * day && w.hi >= asOf - 7 * day);
    auto d = parseTemporalHint("Where did I go 3 days ago", asOf);
    ASSERT_TRUE(d.found);
    ASSERT_TRUE(d.lo <= asOf - 3 * day && d.hi >= asOf - 3 * day && d.hi < asOf);
    ASSERT_FALSE(parseTemporalHint("what is my dog's name", asOf).found);
}

TEST("bench-retrieval: temporal hint resolves a month name to its latest past occurrence") {
    // asOf = 2023-11-14; "in May" -> May 2023
    auto w = parseTemporalHint("What concert did I see in May?", 1700000000);
    ASSERT_TRUE(w.found);
    ASSERT_TRUE(w.lo >= 1682899200 - 86400 && w.lo <= 1682899200 + 86400);  // ~2023-05-01
    ASSERT_TRUE(w.hi > w.lo + 27 * 86400);
}

TEST("bench-retrieval: groupPrior lifts siblings of the strongest group") {
    std::map<std::string, double> s = {{"s1#0", 1.0}, {"s1#1", 0.1}, {"s2#0", 0.5}};
    std::map<std::string, std::string> g = {{"s1#0", "s1"}, {"s1#1", "s1"}, {"s2#0", "s2"}};
    auto p = groupPrior(s, g, 0.5);
    ASSERT_TRUE(p["s1#1"] > s["s1#1"]);
    ASSERT_TRUE(p["s1#1"] - s["s1#1"] > p["s2#0"] - s["s2#0"]);
}

TEST("bench-pipeline: enrich prepends date, neighbours stay inside the session") {
    std::vector<BenchDoc> d = {{"s1#0", "user: hi", 1683504000},
                               {"s1#1", "user: my corgi is Biscuit", 1683504000},
                               {"s2#0", "user: other session", 1683590400}};
    auto e = enrichBenchDocs(d, true, 1);
    ASSERT_CONTAINS(e[0].text, std::string("[2023-05-08 (May 2023)]"));
    ASSERT_CONTAINS(e[0].text, std::string("my corgi is Biscuit"));   // next turn
    ASSERT_NOT_CONTAINS(e[1].text, std::string("other session"));     // other session
    ASSERT_CONTAINS(e[1].text, std::string("user: hi"));              // prev turn
}

TEST("bench-pipeline: bm25 pipeline honours as_of and ranks evidence") {
    BenchCorpus c;
    c.docs = {{"s1", "We talked about sourdough starters and hydration", 1000},
              {"s2", "My corgi Biscuit loves the beach", 2000},
              {"s4", "Biscuit the corgi learned a new trick", 9000}};
    BenchQuestion q; q.id = "q"; q.category = "c";
    q.question = "corgi Biscuit"; q.evidence = {"s2"}; q.as_of = 5000;
    c.questions = {q};
    BenchRunOptions opt; opt.fusion = "rrf"; opt.temporal = 0.3; opt.groupBeta = 0.2;
    auto res = runBenchCorpus(c, {1, 3}, opt);
    ASSERT_EQ((int)res.size(), 1);
    ASSERT_FALSE(res[0].ranked.empty());
    ASSERT_EQ(res[0].ranked[0], std::string("s2"));
    for (const auto& id : res[0].ranked) ASSERT_TRUE(id != "s4");
}

// 2026-11-03: batch13 showed every _abs question ranked empty under --rerank
// with --score-abstention: the pipeline skipped abstention unconditionally.
TEST("bench-pipeline: scoreAbstention ranks _abs questions in the pipeline too") {
    BenchCorpus c;
    c.docs = {{"s1", "We talked about sourdough starters and hydration", 1000},
              {"s2", "My corgi Biscuit loves the beach", 2000}};
    BenchQuestion q; q.id = "q_abs"; q.category = "c"; q.abstention = true;
    q.question = "corgi Biscuit"; q.evidence = {"s2"}; q.as_of = 5000;
    c.questions = {q};
    BenchRunOptions opt; opt.fusion = "rrf"; opt.temporal = 0.3; opt.scoreAbstention = true;
    auto res = runBenchCorpus(c, {1}, opt);
    ASSERT_EQ((int)res.size(), 1);
    ASSERT_FALSE(res[0].skipped);
    ASSERT_FALSE(res[0].ranked.empty());
    ASSERT_EQ(res[0].ranked[0], std::string("s2"));
}

TEST("bench-pipeline: dense rrf with chunks and user view ranks evidence") {
    BenchCorpus c;
    c.docs = {{"s1", "user: sourdough starters and hydration\nassistant: sure", 1000},
              {"s2", "user: My corgi Biscuit loves the beach\nassistant: cute", 2000}};
    BenchQuestion q; q.id = "q"; q.category = "c";
    q.question = "what is the name of my dog"; q.evidence = {"s2"}; q.as_of = 5000;
    c.questions = {q};
    BenchRunOptions opt; opt.semantic = true; opt.fusion = "rrf";
    opt.chunkWords = 4; opt.userView = true; opt.mmrLambda = 0.7;
    auto res = runBenchCorpus(c, {1}, opt);
    ASSERT_FALSE(res[0].ranked.empty());
    ASSERT_EQ(res[0].ranked[0], std::string("s2"));
    ASSERT_CONTAINS(describePipeline(opt), std::string("chunk=4"));
}

TEST("bench-retrieval: group id and date label") {
    ASSERT_EQ(benchGroupOf("answer_4be1b6b4_2#7"), std::string("answer_4be1b6b4_2"));
    ASSERT_EQ(benchGroupOf("D3:5"), std::string("D3"));
    ASSERT_EQ(benchGroupOf("S2"), std::string("S2"));
    ASSERT_EQ(benchDateLabel(1683504000), std::string("2023-05-08 (May 2023)"));
    ASSERT_EQ(benchDateLabel(0), std::string(""));
}

TEST("bench-retrieval: mmr skips a near-duplicate for a diverse doc") {
    std::vector<std::pair<std::string, double>> cand = {{"a", 1.0}, {"a2", 0.99}, {"b", 0.9}};
    std::map<std::string, std::vector<float>> v = {
        {"a", {1, 0}}, {"a2", {1, 0}}, {"b", {0, 1}}};
    auto sel = mmrSelect(cand, v, 0.5, 2);
    ASSERT_EQ((int)sel.size(), 2);
    ASSERT_EQ(sel[0], std::string("a"));
    ASSERT_EQ(sel[1], std::string("b"));
    auto pure = mmrSelect(cand, v, 1.0, 2);
    ASSERT_EQ(pure[1], std::string("a2"));
}

TEST("bench-retrieval: rerankHead reorders only the top N by score") {
    std::vector<std::string> ranked = {"a", "b", "c", "d"};
    auto out = rerankHead(ranked, {0.1, 0.9, 0.5}, 3);
    ASSERT_EQ((int)out.size(), 4);
    ASSERT_EQ(out[0], std::string("b"));
    ASSERT_EQ(out[1], std::string("c"));
    ASSERT_EQ(out[2], std::string("a"));
    ASSERT_EQ(out[3], std::string("d"));   // tail untouched
    // Score count mismatch -> unchanged (scorer unavailable / failed).
    auto same = rerankHead(ranked, {}, 3);
    ASSERT_EQ(same[0], std::string("a"));
}

TEST("bench-pipeline: --rerank flag parses and labels the mode") {
    BenchRunOptions opt;
    ASSERT_EQ(icmg::cli::parseBenchPipelineFlags({"--rerank", "20"}, false, opt), std::string(""));
    ASSERT_EQ(opt.rerankTop, 20);
    ASSERT_TRUE(opt.pipeline());
    ASSERT_CONTAINS(describePipeline(opt), std::string("rerank=20"));
    BenchRunOptions bad;
    ASSERT_FALSE(icmg::cli::parseBenchPipelineFlags({"--rerank", "999"}, false, bad).empty());
}

// LoCoMo10 ablation 2026-11-01: blend a0.3 + enrich/temporal/group + rerank 50
// won; rrf, neighbours, chunk, user-view and mmr were flat or harmful.
TEST("bench-pipeline: --pipeline v2 preset = tuned blend + stages + rerank") {
    BenchRunOptions opt;
    ASSERT_EQ(icmg::cli::parseBenchPipelineFlags({"--pipeline", "v2"}, true, opt), std::string(""));
    ASSERT_TRUE(opt.semantic);
    ASSERT_EQ(opt.fusion, std::string("blend"));
    ASSERT_TRUE(opt.alpha > 0.29 && opt.alpha < 0.31);
    ASSERT_TRUE(opt.enrich);
    ASSERT_TRUE(opt.temporal > 0.29 && opt.temporal < 0.31);
    ASSERT_TRUE(opt.groupBeta > 0.29 && opt.groupBeta < 0.31);
    ASSERT_EQ(opt.rerankTop, 50);
    ASSERT_EQ(opt.rerankChunk, 150);
    ASSERT_EQ(opt.neighbours, 0);
    ASSERT_EQ(opt.chunkWords, 0);
    ASSERT_FALSE(opt.userView);
    ASSERT_TRUE(opt.mmrLambda >= 1.0);
    // explicit flags still override the preset
    BenchRunOptions o2;
    ASSERT_EQ(icmg::cli::parseBenchPipelineFlags({"--pipeline", "v2", "--rerank", "20", "--alpha", "0.5"},
                                                 false, o2), std::string(""));
    ASSERT_EQ(o2.rerankTop, 20);
    ASSERT_TRUE(o2.alpha > 0.49 && o2.alpha < 0.51);
}

// 2026-11-01: chunked (max-passage) rerank -- LongMemEval sessions overflow the
// 256-token cross-encoder window, so whole-doc rerank hurt there.
TEST("bench-retrieval: topOverlapChunks keeps the query-richest windows in order") {
    std::vector<std::string> ch = {"alpha beta", "the cat sat", "nothing here", "cat cat sat mat"};
    auto idx = topOverlapChunks("where did the cat sit on the mat", ch, 2);
    ASSERT_EQ(idx.size(), (size_t)2);
    ASSERT_EQ(idx[0], (size_t)1);   // original order kept among the winners
    ASSERT_EQ(idx[1], (size_t)3);
    auto all = topOverlapChunks("cat", ch, 10);
    ASSERT_EQ(all.size(), ch.size());
    ASSERT_TRUE(topOverlapChunks("q", {}, 3).empty());
}

TEST("bench-retrieval: maxPerOwner folds passage scores into doc scores") {
    auto s = maxPerOwner({0.1f, 0.9f, -2.0f, 0.4f}, {0, 0, 1, 2}, 4);
    ASSERT_EQ(s.size(), (size_t)4);
    ASSERT_TRUE(s[0] > 0.89f && s[0] < 0.91f);
    ASSERT_TRUE(s[1] < -1.99f && s[1] > -2.01f);
    ASSERT_TRUE(s[2] > 0.39f && s[2] < 0.41f);
    ASSERT_TRUE(s[3] < -1e29f);              // doc without passages sinks
    ASSERT_TRUE(maxPerOwner({0.1f}, {0, 1}, 2).empty());   // size mismatch
}

TEST("bench-pipeline: --rerank-chunk parses, labels, and is range-checked") {
    BenchRunOptions opt;
    ASSERT_EQ(icmg::cli::parseBenchPipelineFlags({"--rerank", "20", "--rerank-chunk", "150"}, false, opt),
              std::string(""));
    ASSERT_EQ(opt.rerankChunk, 150);
    ASSERT_CONTAINS(describePipeline(opt), std::string("rerank-chunk=150"));
    BenchRunOptions bad;
    ASSERT_FALSE(icmg::cli::parseBenchPipelineFlags({"--rerank-chunk", "5000"}, false, bad).empty());
}

// 2026-11-03: LME_s n=500 rr20c150 beat bm25 on every metric (any@1 .854->.936,
// NDCG@10 .881->.927), so bench-recall reranks by default when the model exists.
TEST("bench-pipeline: rerank 20 + chunk 150 is the default when the model exists") {
    using icmg::cli::applyBenchDefaults;
    const icmg::cli::BenchModels kRr{true, false}, kNone{false, false};
    BenchRunOptions on;
    applyBenchDefaults({}, kRr, on);
    ASSERT_EQ(on.rerankTop, 20);
    ASSERT_EQ(on.rerankChunk, 150);
    ASSERT_TRUE(on.pipeline());

    BenchRunOptions none;                       // no model -> plain bm25, unchanged
    applyBenchDefaults({}, kNone, none);
    ASSERT_EQ(none.rerankTop, 0);
    ASSERT_FALSE(none.pipeline());

    BenchRunOptions off;                        // explicit opt-out
    applyBenchDefaults({"--no-rerank"}, kRr, off);
    ASSERT_EQ(off.rerankTop, 0);
    ASSERT_EQ(off.rerankChunk, 0);

    BenchRunOptions mine; mine.rerankTop = 50;  // explicit --rerank wins
    applyBenchDefaults({"--rerank", "50"}, kRr, mine);
    ASSERT_EQ(mine.rerankTop, 50);
    ASSERT_EQ(mine.rerankChunk, 0);

    BenchRunOptions zero;                       // --rerank 0 also opts out
    applyBenchDefaults({"--rerank", "0"}, kRr, zero);
    ASSERT_EQ(zero.rerankTop, 0);
}

// 2026-11-03: LME_s n=500 semantic a0.3 + rr20c150 beat rr alone (any@5 .964->.982,
// all@5 .876->.926, NDCG@10 .927->.956), so semantic is the default too.
TEST("bench-pipeline: semantic alpha 0.3 is the default when the embed model exists") {
    using icmg::cli::applyBenchDefaults;
    const icmg::cli::BenchModels kBoth{true, true}, kRrOnly{true, false};

    BenchRunOptions on;
    applyBenchDefaults({}, kBoth, on);
    ASSERT_TRUE(on.semantic);
    ASSERT_TRUE(on.alpha > 0.3 - 1e-9 && on.alpha < 0.3 + 1e-9);
    ASSERT_EQ(on.rerankTop, 20);

    BenchRunOptions noEmbed;                    // no embed model -> semantic stays off
    applyBenchDefaults({}, kRrOnly, noEmbed);
    ASSERT_FALSE(noEmbed.semantic);
    ASSERT_TRUE(noEmbed.alpha > 0.5 - 1e-9 && noEmbed.alpha < 0.5 + 1e-9);

    BenchRunOptions off;                        // explicit opt-out
    applyBenchDefaults({"--no-semantic"}, kBoth, off);
    ASSERT_FALSE(off.semantic);
    ASSERT_EQ(off.rerankTop, 20);               // rerank default independent

    BenchRunOptions mine; mine.alpha = 0.7;     // explicit --alpha wins
    applyBenchDefaults({"--alpha", "0.7"}, kBoth, mine);
    ASSERT_TRUE(mine.semantic);
    ASSERT_TRUE(mine.alpha > 0.7 - 1e-9 && mine.alpha < 0.7 + 1e-9);

    BenchRunOptions sem; sem.semantic = true;   // explicit --semantic keeps its alpha
    applyBenchDefaults({"--semantic"}, kBoth, sem);
    ASSERT_TRUE(sem.alpha > 0.5 - 1e-9 && sem.alpha < 0.5 + 1e-9);
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
