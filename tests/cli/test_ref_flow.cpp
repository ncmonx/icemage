// v2.30.0 Model-AI ref flow (docs/plans/2026-11-13-model-ai-ref-flow.md):
// pure policy pieces -- project scope filter (B/C), the "Ada Ref / Solusi?"
// gate, the verified-only write-back decision and the ref content builder.
#include "../test_main.hpp"
#include "../../src/cli/ref_flow.hpp"

#include <string>
#include <vector>

using namespace icmg::cli::refflow;

namespace {
const std::string kHome = "C:/Users/Me";
const std::string kCwd  = "D:/Work/ProjA";
}

// ---- T1 isShareableProject -------------------------------------------------
TEST("ref_flow: shareable keeps normal projects") {
    ASSERT_TRUE(isShareableProject("D:/Work/ProjB", kHome, kCwd));
    ASSERT_TRUE(isShareableProject("C:\\Users\\Me\\code\\ProjC", kHome, kCwd));
}

TEST("ref_flow: shareable rejects temp and tmp") {
    ASSERT_FALSE(isShareableProject("C:/Users/Me/AppData/Local/Temp/icmg_x", kHome, kCwd));
    ASSERT_FALSE(isShareableProject("C:\\Temp\\smk277", kHome, kCwd));
    ASSERT_FALSE(isShareableProject("/tmp/proj", kHome, kCwd));
}

TEST("ref_flow: shareable rejects system home cwd build") {
    ASSERT_FALSE(isShareableProject("C:/Windows/System32", kHome, kCwd));
    ASSERT_FALSE(isShareableProject("c:\\users\\me\\", kHome, kCwd));      // home root, case/slash-insensitive
    ASSERT_FALSE(isShareableProject("d:/work/proja/", kHome, kCwd));       // cwd itself
    ASSERT_FALSE(isShareableProject("D:/x/icemage-code/build", kHome, kCwd));
    ASSERT_FALSE(isShareableProject("D:/x/app/build-msvc-full/sub", kHome, kCwd));
    ASSERT_FALSE(isShareableProject("", kHome, kCwd));
}

TEST("ref_flow: shareable build word inside name is fine") {
    ASSERT_TRUE(isShareableProject("D:/Work/buildtools-app", kHome, kCwd));
    ASSERT_TRUE(isShareableProject("D:/Work/my-build", kHome, kCwd));
}

// ---- T2 gate ---------------------------------------------------------------
TEST("ref_flow: gate needs med or high") {
    const double floor = 0.15;
    ASSERT_FALSE(refGateSatisfied(0.0, floor));          // nothing found -> go B/C
    ASSERT_FALSE(refGateSatisfied(0.16, floor));         // low tier -> go B/C
    ASSERT_TRUE(refGateSatisfied(0.15 * 1.5, floor));    // med -> stop
    ASSERT_TRUE(refGateSatisfied(5.0, floor));           // high -> stop
}

TEST("ref_flow: combined gate accepts a ref or a med local hit") {
    ASSERT_TRUE(refFlowGate(kRefMinCoverage, 0.0, 0.15));
    ASSERT_TRUE(refFlowGate(0.0, 0.3, 0.15));
    ASSERT_FALSE(refFlowGate(kRefMinCoverage - 0.01, 0.16, 0.15));
}

TEST("ref_flow: refCoverage measures how much of the ref problem the prompt names") {
    const std::string prob = "vite dev server port already in use EADDRINUSE";
    ASSERT_TRUE(refCoverage("EADDRINUSE port already in use on vite", prob) >= kRefMinCoverage);
    ASSERT_TRUE(refCoverage("kubernetes helm chart rollback", prob) == 0.0);
    // a single shared word is never enough
    ASSERT_TRUE(refCoverage("vite config question", prob) == 0.0);
    // stopwords do not count toward the match
    ASSERT_TRUE(refCoverage("the in on use", "the in on use") == 0.0);
}

// ---- T4 write-back policy --------------------------------------------------
TEST("ref_flow: writeback requires change and verification") {
    WritebackSignals s;
    s.changed = true; s.verified_ok = true;
    ASSERT_TRUE(decideWriteback(s) == WritebackDecision::Store);

    s.verified_ok = false;
    ASSERT_TRUE(decideWriteback(s) == WritebackDecision::SkipUnverified);

    s.verified_ok = false; s.ref_reused = true;            // observed reuse also counts
    ASSERT_TRUE(decideWriteback(s) == WritebackDecision::Store);

    s.changed = false;
    ASSERT_TRUE(decideWriteback(s) == WritebackDecision::SkipNoChange);
}

TEST("ref_flow: writeback failed verify never stores") {
    WritebackSignals s;
    s.changed = true; s.verified_ok = true; s.verify_failed_after = true;
    ASSERT_TRUE(decideWriteback(s) == WritebackDecision::SkipUnverified);
}

TEST("ref_flow: writeback respects kill switch dup and cap") {
    WritebackSignals s;
    s.changed = true; s.verified_ok = true;
    s.disabled = true;
    ASSERT_TRUE(decideWriteback(s) == WritebackDecision::SkipDisabled);
    s.disabled = false; s.duplicate = true;
    ASSERT_TRUE(decideWriteback(s) == WritebackDecision::SkipDuplicate);
    s.duplicate = false; s.stored_this_session = kRefWritebackMaxPerSession;
    ASSERT_TRUE(decideWriteback(s) == WritebackDecision::SkipCap);
}

TEST("ref_flow: writeback env kill switch parse") {
    ASSERT_FALSE(writebackDisabledByEnv(nullptr));
    ASSERT_FALSE(writebackDisabledByEnv("1"));
    ASSERT_TRUE(writebackDisabledByEnv("0"));
    ASSERT_TRUE(writebackDisabledByEnv("off"));
    ASSERT_TRUE(writebackDisabledByEnv("false"));
}

// ---- T4 content builder ----------------------------------------------------
TEST("ref_flow: content redacts private and caps") {
    std::string prompt = "fix login crash <private>token=abc123</private> on startup";
    RefContent c = buildRefContent(prompt, {"src/a.cpp", "src/b.cpp"},
                                   "ctest -R login", "fixed null deref");
    ASSERT_TRUE(c.problem.find("abc123") == std::string::npos);
    ASSERT_TRUE(c.problem.find("fix login crash") != std::string::npos);
    ASSERT_TRUE(c.solution.find("src/a.cpp") != std::string::npos);
    ASSERT_TRUE(c.solution.find("ctest -R login") != std::string::npos);
    ASSERT_TRUE(c.solution.find("fixed null deref") != std::string::npos);

    std::string big(5000, 'x');
    RefContent c2 = buildRefContent(big, {}, "", big);
    ASSERT_TRUE(c2.problem.size() <= kRefProblemCap);
    ASSERT_TRUE(c2.solution.size() <= kRefSolutionCap);
}

TEST("ref_flow: content caps file list") {
    std::vector<std::string> files;
    for (int i = 0; i < 40; ++i) files.push_back("src/f" + std::to_string(i) + ".cpp");
    RefContent c = buildRefContent("p", files, "", "ok");
    ASSERT_TRUE(c.solution.find("src/f0.cpp") != std::string::npos);
    ASSERT_TRUE(c.solution.find("src/f39.cpp") == std::string::npos);
    ASSERT_TRUE(c.solution.find("more") != std::string::npos);
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
