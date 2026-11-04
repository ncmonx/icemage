// 2026-10-31: see bench_pipeline_flags.hpp.
#include "bench_pipeline_flags.hpp"

#include <algorithm>

namespace icmg::cli {

namespace {

// --pipeline v2 = best LoCoMo10 ablation (2026-11-01, n=1536): blend a0.3 +
// enrich/temporal/group + rerank 50. rrf, neighbours, chunk, user-view and mmr
// were flat or harmful there, so the preset leaves them off.
constexpr double kV2Alpha     = 0.3;
constexpr double kV2Temporal  = 0.3;
constexpr double kV2Group     = 0.3;
constexpr int    kV2Rerank    = 50;
constexpr int    kV2RerankChunk = 150;  // max-passage rerank: LME .791 -> .863 NDCG@10
// Default rerank (2026-11-03, LME_s n=500: NDCG@10 .881 -> .927 vs bm25).
constexpr int    kDefaultRerank      = 20;
constexpr int    kDefaultRerankChunk = 150;
// Default semantic blend (2026-11-03, LME_s n=500 + rr: all@5 .876 -> .926).
constexpr double kDefaultAlpha       = 0.3;
constexpr int    kRerankMax    = 100;   // cross-encoder cost is linear in N per question
constexpr int    kRerankChunkMax = 400; // words; pair window is 256 tokens anyway

bool has(const std::vector<std::string>& a, const char* f) {
    return std::find(a.begin(), a.end(), f) != a.end();
}

// Value after flag, or "" (and *present=false) when absent.
std::string value(const std::vector<std::string>& a, const char* f, bool* present) {
    for (size_t i = 0; i + 1 < a.size(); ++i)
        if (a[i] == f) { *present = true; return a[i + 1]; }
    *present = false;
    return "";
}

bool num(const std::vector<std::string>& a, const char* f, double lo, double hi,
         double& out, std::string& err) {
    bool p;
    std::string v = value(a, f, &p);
    if (!p) return true;
    try {
        size_t used = 0;
        double d = std::stod(v, &used);
        if (used != v.size() || d < lo || d > hi) throw 0;
        out = d;
        return true;
    } catch (...) {
        err = std::string(f) + " must be a number in [" + std::to_string(lo) + "," +
              std::to_string(hi) + "]";
        return false;
    }
}

}  // namespace

std::string parseBenchPipelineFlags(const std::vector<std::string>& args, bool turn,
                                    imem::BenchRunOptions& opt) {
    bool p;
    std::string preset = value(args, "--pipeline", &p);
    if (p) {
        if (preset != "v2") return "--pipeline: unknown preset '" + preset + "' (expected v2)";
        opt.semantic = true;
        opt.fusion = "blend";
        if (!has(args, "--alpha")) opt.alpha = kV2Alpha;   // explicit --alpha wins
        opt.enrich = true;
        opt.temporal = kV2Temporal;
        opt.groupBeta = kV2Group;
        opt.rerankTop = kV2Rerank;
        opt.rerankChunk = kV2RerankChunk;
    }
    (void)turn;
    std::string fusion = value(args, "--fusion", &p);
    if (p) {
        if (fusion != "blend" && fusion != "rrf") return "--fusion must be blend|rrf";
        opt.fusion = fusion;
    }
    if (has(args, "--enrich")) opt.enrich = true;
    if (has(args, "--user-view")) opt.userView = true;
    if (has(args, "--no-as-of")) opt.noAsOf = true;
    if (has(args, "--score-abstention")) opt.scoreAbstention = true;

    std::string err;
    double d = opt.neighbours;
    if (!num(args, "--neighbours", 0, 5, d, err)) return err;
    opt.neighbours = (int)d;
    d = opt.chunkWords;
    if (!num(args, "--chunk", 0, 400, d, err)) return err;
    opt.chunkWords = (int)d;
    if (!num(args, "--temporal", 0, 5, opt.temporal, err)) return err;
    if (!num(args, "--group", 0, 5, opt.groupBeta, err)) return err;
    if (!num(args, "--mmr", 0, 1, opt.mmrLambda, err)) return err;
    double rr = opt.rerankTop;
    if (!num(args, "--rerank", 0, kRerankMax, rr, err)) return err;
    opt.rerankTop = (int)rr;
    double rc = opt.rerankChunk;
    if (!num(args, "--rerank-chunk", 0, kRerankChunkMax, rc, err)) return err;
    opt.rerankChunk = (int)rc;
    return "";
}

const char* benchPipelineUsage() {
    return
        "  Experimental pipeline (bench-only, each stage ablatable):\n"
        "    --pipeline v2      tuned preset: --semantic --fusion blend --alpha 0.3 --enrich\n"
        "                       --temporal 0.3 --group 0.3 --rerank 50 --rerank-chunk 150\n"
        "                       (flags override)\n"
        "    --fusion blend|rrf BM25+dense fusion over a full dense scan (rrf = scale-free)\n"
        "    --enrich           prepend the session date to every doc\n"
        "    --neighbours N     turn docs carry +/-N same-session turns\n"
        "    --chunk N          multi-vector dense: N-word chunks, max-sim per doc\n"
        "    --user-view        extra vectors over the user's own lines (facts/preferences)\n"
        "    --temporal B       boost docs inside a time window parsed from the question\n"
        "    --group B          lift turns by their session's best score (two-tier)\n"
        "    --mmr L            MMR diversification of the top 50 (L<1; multi-hop)\n"
        "    --rerank N         cross-encoder rerank of the final top N (ms-marco-MiniLM-L-6-v2;\n"
        "                       ~/.icmg/embed/ms-marco-MiniLM-L-6-v2.onnx or ICMG_RERANK_MODEL;\n"
        "                       DEFAULT 20 + --rerank-chunk 150 when the model is present)\n"
        "    --no-rerank        plain bm25 (turn the default rerank off; same as --rerank 0)\n"
        "    --no-semantic      skip the dense stage (DEFAULT is --semantic --alpha 0.3 when the\n"
        "                       embed model is present)\n"
        "    --rerank-chunk W   rerank W-word passages (best 4 per doc by query overlap),\n"
        "                       doc score = max passage (long docs overflow the 256-token pair)\n"
        "    ICMG_BENCH_THREADS=N  parallel embedding workers (default hw-2, max 8)\n"
        "    --no-as-of         ignore question dates: every doc visible (LongMemEval_s has\n"
        "                       evidence dated after question_date; ICM/MemPalace runs see all)\n"
        "    --score-abstention also score _abs questions that carry evidence (ICM/MemPalace\n"
        "                       report LongMemEval_s over all 500; default skips them)\n";
}

void applyBenchDefaults(const std::vector<std::string>& args, const BenchModels& models,
                        imem::BenchRunOptions& opt) {
    if (has(args, "--no-semantic")) {
        opt.semantic = false;
    } else if (!opt.semantic && !has(args, "--semantic") && models.embed) {
        opt.semantic = true;
        bool a;
        value(args, "--alpha", &a);
        if (!a) opt.alpha = kDefaultAlpha;
    }
    if (has(args, "--no-rerank")) {
        opt.rerankTop = 0;
        opt.rerankChunk = 0;
        return;
    }
    bool p;
    value(args, "--rerank", &p);
    if (p || opt.rerankTop > 0 || !models.rerank) return;   // explicit / preset / no model
    opt.rerankTop = kDefaultRerank;
    if (!has(args, "--rerank-chunk")) opt.rerankChunk = kDefaultRerankChunk;
}

}  // namespace icmg::cli
