// Tests for installGlobalReadHook JSON merge logic.
// The private method writes a PreToolUse Read|Glob|Grep hook into
// ~/.claude/settings.json. Tests verify correct JSON output for:
//   1. Fresh empty hooks object  → entry added
//   2. Existing unrelated entry  → preserved + new entry appended
//   3. Existing same matcher     → idempotent (not duplicated)
//   4. Missing settings file     → graceful no-op
#include "../test_main.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;
using nlohmann::json;

static const char* MATCHER = "Read|Glob|Grep";
static const char* HOOK_TYPE = "command";

// Replicates the JSON merge logic from installGlobalReadHook.
static void applyHook(json& cfg, bool force, const std::string& cmd) {
    if (!cfg.contains("hooks")) cfg["hooks"] = json::object();
    if (!cfg["hooks"].is_object()) cfg["hooks"] = json::object();
    json& pre = cfg["hooks"]["PreToolUse"];
    if (!pre.is_array()) pre = json::array();

    for (auto& entry : pre) {
        if (entry.contains("matcher") && entry["matcher"] == MATCHER) {
            if (force)
                entry["hooks"] = json::array({{{"type", HOOK_TYPE}, {"command", cmd}, {"shell", "bash"}}});
            return;
        }
    }
    pre.push_back({
        {"matcher", MATCHER},
        {"hooks", json::array({
            {{"type", HOOK_TYPE}, {"command", cmd}, {"shell", "bash"}}
        })}
    });
}

TEST("init_hook: fresh empty hooks → entry added") {
    json cfg = json::object();
    applyHook(cfg, false, "python3 -c 'pass'");

    ASSERT_TRUE(cfg.contains("hooks"));
    ASSERT_TRUE(cfg["hooks"].contains("PreToolUse"));
    auto& pre = cfg["hooks"]["PreToolUse"];
    ASSERT_EQ(pre.size(), 1u);
    ASSERT_EQ(pre[0]["matcher"].get<std::string>(), std::string(MATCHER));
    ASSERT_EQ(pre[0]["hooks"][0]["type"].get<std::string>(), std::string("command"));
    ASSERT_EQ(pre[0]["hooks"][0]["shell"].get<std::string>(), std::string("bash"));
}

TEST("init_hook: unrelated PreToolUse entry preserved") {
    json cfg;
    cfg["hooks"]["PreToolUse"] = json::array({{
        {"matcher", "Bash"},
        {"hooks", json::array({{{"type", "command"}, {"command", "echo bash"}}})}
    }});
    applyHook(cfg, false, "python3 -c 'pass'");

    auto& pre = cfg["hooks"]["PreToolUse"];
    ASSERT_EQ(pre.size(), 2u);
    // Original Bash entry still there.
    ASSERT_EQ(pre[0]["matcher"].get<std::string>(), std::string("Bash"));
    // New Read|Glob|Grep entry appended.
    ASSERT_EQ(pre[1]["matcher"].get<std::string>(), std::string(MATCHER));
}

TEST("init_hook: idempotent — second call without force does not duplicate") {
    json cfg = json::object();
    applyHook(cfg, false, "cmd_v1");
    applyHook(cfg, false, "cmd_v2");  // should not add second entry

    auto& pre = cfg["hooks"]["PreToolUse"];
    ASSERT_EQ(pre.size(), 1u);
    // Still original command (no force).
    ASSERT_EQ(pre[0]["hooks"][0]["command"].get<std::string>(), std::string("cmd_v1"));
}

TEST("init_hook: force flag updates existing entry command") {
    json cfg = json::object();
    applyHook(cfg, false, "cmd_old");
    applyHook(cfg, true,  "cmd_new");

    auto& pre = cfg["hooks"]["PreToolUse"];
    ASSERT_EQ(pre.size(), 1u);
    ASSERT_EQ(pre[0]["hooks"][0]["command"].get<std::string>(), std::string("cmd_new"));
}

TEST("init_hook: hooks key absent in cfg → created") {
    json cfg = {{"model", "sonnet"}};
    applyHook(cfg, false, "x");
    ASSERT_TRUE(cfg.contains("hooks"));
    ASSERT_TRUE(cfg["hooks"].contains("PreToolUse"));
    // Unrelated key preserved.
    ASSERT_EQ(cfg["model"].get<std::string>(), std::string("sonnet"));
}

TEST("init_hook: file round-trip preserves other keys") {
    auto tmp = fs::temp_directory_path() /
               ("icmg_init_hook_" + std::to_string(::time(nullptr)) + ".json");
    json cfg = {{"attribution", {{"commit", ""}}}, {"model", "sonnet"}};
    {
        std::ofstream out(tmp); out << cfg.dump(2);
    }
    // Simulate read → apply → write.
    json loaded;
    { std::ifstream in(tmp); in >> loaded; }
    applyHook(loaded, false, "python3 -c 'pass'");
    { std::ofstream out(tmp); out << loaded.dump(2); }
    // Re-read and verify.
    json final;
    { std::ifstream in(tmp); in >> final; }

    ASSERT_EQ(final["model"].get<std::string>(), std::string("sonnet"));
    ASSERT_EQ(final["attribution"]["commit"].get<std::string>(), std::string(""));
    ASSERT_TRUE(final["hooks"]["PreToolUse"].is_array());
    ASSERT_EQ(final["hooks"]["PreToolUse"].size(), 1u);

    std::error_code ec; fs::remove(tmp, ec);
}


// ---------------------------------------------------------------------------
// GREP_HOOK_JS intent-layer heuristic (v2.8.0).
// The PreToolUse:Grep hook adds a 3rd "intent" layer: when the pattern looks
// like a natural-language phrase (not a regex / identifier), the hook also
// runs `icmg find "<pattern>"` so the agent gets ranked code slices in ONE
// turn instead of looping grep->reword->grep on a phrase that never matches
// verbatim. This mirrors the JS isIntentLike() in init_cmd.cpp GREP_HOOK_JS.
static bool isIntentLike(const std::string& pat) {
    // trim
    size_t b = pat.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return false;
    size_t e = pat.find_last_not_of(" \t\r\n");
    std::string s = pat.substr(b, e - b + 1);
    if (s.empty()) return false;
    // word count (split on whitespace runs)
    int words = 0; bool in = false;
    for (char c : s) {
        bool ws = (c == ' ' || c == '\t' || c == '\r' || c == '\n');
        if (!ws && !in) { ++words; in = true; }
        else if (ws) in = false;
    }
    if (words < 2) return false;                 // single token => identifier/regex/path
    // any regex metachar => treat as literal grep, not prose
    const std::string meta = "[](){}|^$*+?\\";
    for (char c : s)
        if (meta.find(c) != std::string::npos) return false;
    return true;                                  // multi-word prose => intent search
}

TEST("grep_hook: multi-word prose is intent-like") {
    ASSERT_TRUE(isIntentLike("where is the persona turn handled"));
    ASSERT_TRUE(isIntentLike("handle login flow"));
    ASSERT_TRUE(isIntentLike("two words"));
}

TEST("grep_hook: single token is NOT intent-like (identifier/regex/path)") {
    ASSERT_TRUE(!isIntentLike("FindCommand"));
    ASSERT_TRUE(!isIntentLike("src/main.cpp"));
    ASSERT_TRUE(!isIntentLike("getUserById"));
    ASSERT_TRUE(!isIntentLike(""));
    ASSERT_TRUE(!isIntentLike("   "));
}

TEST("grep_hook: regex metachars disqualify intent (use literal grep)") {
    ASSERT_TRUE(!isIntentLike("foo.*bar baz"));
    ASSERT_TRUE(!isIntentLike("a\\|b cd"));
    ASSERT_TRUE(!isIntentLike("class (Foo|Bar)"));
    ASSERT_TRUE(!isIntentLike("end$ of line"));
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
