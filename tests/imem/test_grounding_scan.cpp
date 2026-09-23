// TDD (2026-09-23): grounding scan -- `icmg memory-health --grounding`.
// Research: docs/plans/2026-09-23-ai-agi-landscape-scan.md IDE-C (inspired by
// "Grounding Agent Memory: Environment-Probing Curation", arXiv 2609.11060):
// a memory that references a file the graph no longer knows is likely STALE
// (file renamed/deleted since the memory was written). Flag it -- never delete.
//
// Pure functions, no IO/DB/LLM: extractFileRefs(content) pulls path-like
// tokens; findUngroundedMemories(mems, known_basenames) flags memories whose
// referenced basenames are absent from the graph.
#include "../test_main.hpp"
#include "../../src/imem/grounding_scan.hpp"
#include <string>

using namespace icmg::imem;

static GroundedMem mem(int64_t id, const std::string& topic, const std::string& content) {
    GroundedMem m; m.id = id; m.topic = topic; m.content = content;
    return m;
}

// 1. Path-like tokens with a code extension are extracted; prose is not.
TEST("grounding: extracts file refs from content") {
    auto refs = extractFileRefs(
        "Fixed the bug in src/cli/dispatcher.cpp and core/db.hpp today");
    ASSERT_EQ((int)refs.size(), 2);
    ASSERT_EQ(refs[0], std::string("src/cli/dispatcher.cpp"));
    ASSERT_EQ(refs[1], std::string("core/db.hpp"));
}

// 2. Backticks/quotes/trailing punctuation are stripped; refs are deduped.
TEST("grounding: strips punctuation and dedups") {
    auto refs = extractFileRefs(
        "Edit `scripts/pack-win.ps1`, then scripts/pack-win.ps1. Done (main.cpp).");
    ASSERT_EQ((int)refs.size(), 2);
    ASSERT_EQ(refs[0], std::string("scripts/pack-win.ps1"));
    ASSERT_EQ(refs[1], std::string("main.cpp"));
}

// 3. URLs, globs, and non-code extensions are NOT file refs.
TEST("grounding: skips urls, globs, and non-code tokens") {
    auto refs = extractFileRefs(
        "See https://arxiv.org/abs/2609.11060 and paper 2609.24971; "
        "glob src/*.cpp should not count, nor v2.13.2 nor README.md");
    ASSERT_EQ((int)refs.size(), 0);
}

// 4. Windows-style separators are recognized and normalized to '/'.
TEST("grounding: handles backslash paths") {
    auto refs = extractFileRefs("crash at src\\core\\exec_utils.cpp line 40");
    ASSERT_EQ((int)refs.size(), 1);
    ASSERT_EQ(refs[0], std::string("src/core/exec_utils.cpp"));
}

// 5. Memory whose refs all exist in the graph -> grounded, no issue.
TEST("grounding: fully grounded memory not flagged") {
    std::vector<GroundedMem> mems = {
        mem(1, "decisions-cli", "moved logic into src/cli/dispatcher.cpp"),
    };
    std::set<std::string> known = {"dispatcher.cpp"};
    auto issues = findUngroundedMemories(mems, known, 25);
    ASSERT_EQ((int)issues.size(), 0);
}

// 6. Memory referencing a basename the graph lost -> flagged with that ref.
TEST("grounding: memory referencing vanished file flagged") {
    std::vector<GroundedMem> mems = {
        mem(7, "bug:crash", "root cause in old_widget.cpp, fixed via core/db.hpp"),
    };
    std::set<std::string> known = {"db.hpp"};
    auto issues = findUngroundedMemories(mems, known, 25);
    ASSERT_EQ((int)issues.size(), 1);
    ASSERT_EQ(issues[0].mem_id, (int64_t)7);
    ASSERT_EQ((int)issues[0].missing.size(), 1);
    ASSERT_EQ(issues[0].missing[0], std::string("old_widget.cpp"));
}

// 7. Basename matching is case-insensitive (Windows reality).
TEST("grounding: basename match is case-insensitive") {
    std::vector<GroundedMem> mems = {
        mem(2, "t", "see Src/Cli/Dispatcher.CPP for details"),
    };
    std::set<std::string> known = {"dispatcher.cpp"};   // stored lowercased
    auto issues = findUngroundedMemories(mems, known, 25);
    ASSERT_EQ((int)issues.size(), 0);
}

// 8. Memories with no file refs at all are never flagged.
TEST("grounding: memory without refs ignored") {
    std::vector<GroundedMem> mems = {
        mem(3, "plan:x", "we decided to ship the token-killer pack next week"),
    };
    std::set<std::string> known;
    auto issues = findUngroundedMemories(mems, known, 25);
    ASSERT_EQ((int)issues.size(), 0);
}

// 9. Ranking: more missing refs first, then newer (higher id) first; capped.
TEST("grounding: ranked by missing count then id, capped at max_out") {
    std::vector<GroundedMem> mems = {
        mem(10, "a", "gone_one.cpp"),
        mem(20, "b", "gone_a.cpp and gone_b.hpp broke together"),
        mem(30, "c", "gone_two.cpp"),
    };
    std::set<std::string> known;
    auto issues = findUngroundedMemories(mems, known, 25);
    ASSERT_EQ((int)issues.size(), 3);
    ASSERT_EQ(issues[0].mem_id, (int64_t)20);   // 2 missing refs
    ASSERT_EQ(issues[1].mem_id, (int64_t)30);   // 1 missing, newer
    ASSERT_EQ(issues[2].mem_id, (int64_t)10);
    auto capped = findUngroundedMemories(mems, known, 2);
    ASSERT_EQ((int)capped.size(), 2);
}

// 10. Relative import SPECIFIERS ("./x.js", "../y.js") are module imports the
// bundler resolves, not file citations -- auto-captured import lists would
// otherwise drown the report (live shakedown: 25/25 hits were "./debug.js").
TEST("grounding: relative import specifiers are not file refs") {
    auto refs = extractFileRefs(
        "imports ./debug.js and ../../utils/cron.js but cites src/tools/UI.ts");
    ASSERT_EQ((int)refs.size(), 1);
    ASSERT_EQ(refs[0], std::string("src/tools/UI.ts"));
}

// 12. C/C++ SYSTEM headers (unistd.h, sys/socket.h, cuda_fp16.h...) come from
// SDKs, never from the project graph -- citing one is not staleness evidence.
TEST("grounding: system headers are not file refs") {
    auto refs = extractFileRefs(
        "ported sys/socket.h + unistd.h + stdint.h usage into src/core/db.hpp");
    ASSERT_EQ((int)refs.size(), 1);
    ASSERT_EQ(refs[0], std::string("src/core/db.hpp"));
}

// 11. TS convention: import "x.js" resolves to source "x.ts"/"x.tsx" -- a .js
// ref whose .ts twin lives in the graph is grounded, not vanished.
TEST("grounding: js ref grounded by ts twin in graph") {
    std::vector<GroundedMem> mems = {
        mem(4, "t", "see src/utils/format.js and src/pages/app.jsx"),
    };
    std::set<std::string> known = {"format.ts", "app.tsx"};
    auto issues = findUngroundedMemories(mems, known, 25);
    ASSERT_EQ((int)issues.size(), 0);
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
