// v2.30.0 Model-AI ref flow, write-back half: scan the Claude Code transcript
// tail of the finished turn (problem, edited files, verify commands, outcome)
// and store a ref only for verified work.
#include "../test_main.hpp"
#include "../../src/cli/ref_writeback.hpp"
#include "../../src/core/db.hpp"
#include "../../src/imem/ai_ref_store.hpp"

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using namespace icmg::cli::refflow;

namespace {
std::string userText(const std::string& t) {
    return R"({"type":"user","message":{"role":"user","content":")" + t + R"("}})";
}
std::string toolUse(const std::string& id, const std::string& name, const std::string& input) {
    return R"({"type":"assistant","message":{"role":"assistant","content":[{"type":"tool_use","id":")" +
           id + R"(","name":")" + name + R"(","input":)" + input + "}]}}";
}
std::string toolResult(const std::string& id, bool err) {
    return R"({"type":"user","message":{"role":"user","content":[{"type":"tool_result","tool_use_id":")" +
           id + R"(","is_error":)" + (err ? "true" : "false") + R"(,"content":"x"}]}})";
}
std::string assistantText(const std::string& t) {
    return R"({"type":"assistant","message":{"role":"assistant","content":[{"type":"text","text":")" +
           t + R"("}]}})";
}
std::string join(std::initializer_list<std::string> lines) {
    std::string s;
    for (auto& l : lines) s += l + "\n";
    return s;
}
}  // namespace

TEST("ref_writeback: isVerifyCommand matches test/build runners only") {
    ASSERT_TRUE(isVerifyCommand("ctest --test-dir build"));
    ASSERT_TRUE(isVerifyCommand("cd x && npm test"));
    ASSERT_TRUE(isVerifyCommand("pwsh -File build.ps1 -Target both"));
    ASSERT_TRUE(isVerifyCommand("python -m pytest -q"));
    ASSERT_FALSE(isVerifyCommand("git status"));
    ASSERT_FALSE(isVerifyCommand("ls -la"));
}

TEST("ref_writeback: scan picks last turn edits, verify and outcome") {
    std::string t = join({
        userText("old task about something else entirely"),
        toolUse("a0", "Edit", R"({"file_path":"old.cpp"})"),
        userText("fix crash when settings json has a BOM on windows"),
        toolUse("a1", "Edit", R"({"file_path":"src/a.cpp","old_string":"x","new_string":"y"})"),
        toolResult("a1", false),
        toolUse("a2", "Bash", R"({"command":"ctest --test-dir build"})"),
        toolResult("a2", false),
        assistantText("Fixed: strip BOM before parse.\\nmore detail"),
    });
    TurnScan s = scanTranscriptTurn(t);
    ASSERT_EQ(s.problem, std::string("fix crash when settings json has a BOM on windows"));
    ASSERT_EQ(s.edited_files.size(), (size_t)1);
    ASSERT_EQ(s.edited_files[0], std::string("src/a.cpp"));
    ASSERT_TRUE(s.verified_ok);
    ASSERT_FALSE(s.verify_failed_after);
    ASSERT_EQ(s.verify_cmd, std::string("ctest --test-dir build"));
    ASSERT_EQ(s.outcome, std::string("Fixed: strip BOM before parse."));
}

TEST("ref_writeback: last verify failing marks failed_after") {
    std::string t = join({
        userText("make the parser accept trailing commas please"),
        toolUse("b1", "Write", R"({"file_path":"p.cpp","content":"z"})"),
        toolUse("b2", "Bash", R"({"command":"ctest"})"),
        toolResult("b2", false),
        toolUse("b3", "Bash", R"({"command":"ctest -R parser"})"),
        toolResult("b3", true),
    });
    TurnScan s = scanTranscriptTurn(t);
    ASSERT_TRUE(s.verify_failed_after);
}

TEST("ref_writeback: short follow-up prompt keeps the earlier problem") {
    std::string t = join({
        userText("refactor the cache eviction to use LRU with a size cap"),
        assistantText("plan ready"),
        userText("lanjut"),
        toolUse("c1", "MultiEdit", R"({"file_path":"cache.cpp","edits":[]})"),
        toolUse("c2", "Bash", R"({"command":"npm test"})"),
        toolResult("c2", false),
    });
    TurnScan s = scanTranscriptTurn(t);
    ASSERT_EQ(s.problem, std::string("refactor the cache eviction to use LRU with a size cap"));
    ASSERT_EQ(s.edited_files.size(), (size_t)1);
    ASSERT_TRUE(s.verified_ok);
}

TEST("ref_writeback: garbage lines are ignored") {
    TurnScan s = scanTranscriptTurn("not json\n{broken\n\n");
    ASSERT_TRUE(s.problem.empty());
    ASSERT_TRUE(s.edited_files.empty());
}

TEST("ref_writeback: end to end stores verified ref, skips unverified") {
    fs::path dir = fs::temp_directory_path() / "icmg_ref_wb_e2e";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    fs::path tr = dir / "t.jsonl";
    {
        std::ofstream f(tr, std::ios::binary);
        f << join({
            userText("hook reinstall empties settings local json on failure"),
            toolUse("d1", "Edit", R"({"file_path":"src/init.cpp"})"),
            toolUse("d2", "Bash", R"({"command":"ctest -R atomic"})"),
            toolResult("d2", false),
            assistantText("Atomic validated write now."),
        });
    }
    icmg::core::Db db(":memory:");
    WritebackContext ctx;
    ctx.transcript_path = tr.string();
    ctx.project = "demo";
    ctx.ref_db = &db;
    ctx.stored_this_session = 0;
    ASSERT_EQ(std::string(writebackDecisionName(runRefWriteback(ctx))), std::string("store"));
    icmg::imem::AiRefStore st(db);
    ASSERT_EQ(st.count(), 1);
    auto hits = st.search("settings local json empties", 3);
    ASSERT_TRUE(!hits.empty());
    ASSERT_TRUE(hits[0].solution.find("src/init.cpp") != std::string::npos);
    ASSERT_EQ(hits[0].verified_by, std::string("ctest -R atomic"));

    // same transcript again -> duplicate, no second row
    ASSERT_EQ(std::string(writebackDecisionName(runRefWriteback(ctx))), std::string("skip-duplicate"));
    ASSERT_EQ(st.count(), 1);

    // unverified turn -> nothing stored
    {
        std::ofstream f(tr, std::ios::binary);
        f << join({userText("totally different problem about fonts rendering"),
                   toolUse("e1", "Edit", R"({"file_path":"font.cpp"})")});
    }
    ASSERT_EQ(std::string(writebackDecisionName(runRefWriteback(ctx))), std::string("skip-unverified"));
    ASSERT_EQ(st.count(), 1);
    fs::remove_all(dir, ec);
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
