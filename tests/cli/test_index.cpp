// Phase 32 T3 — `icmg index` pipeline ordering + dry-run safety.
// Tests the sequence-builder logic, not subprocess execution.
#include "../test_main.hpp"
#include <vector>
#include <string>

struct Step { std::string name; std::string cmd; bool mutating; };

static std::vector<Step> buildPipeline(bool apply, bool skip_embed, const std::string& since) {
    std::vector<Step> steps;
    steps.push_back({"graph update", "graph update --since " + since, true});
    if (!skip_embed) {
        steps.push_back({"embed memory", "embed memory", true});
        steps.push_back({"embed graph",  "embed graph",  true});
    }
    std::string apply_flag = apply ? "" : " --dry-run";
    steps.push_back({"consolidate",     "memory consolidate"      + apply_flag, apply});
    steps.push_back({"extract-patterns","memory extract-patterns" + apply_flag, apply});
    steps.push_back({"decay",           "memory decay --dry-run", false});
    return steps;
}

TEST("index: pipeline order is deterministic") {
    auto s = buildPipeline(false, false, "1d");
    ASSERT_EQ(s[0].name, std::string("graph update"));
    ASSERT_EQ(s[1].name, std::string("embed memory"));
    ASSERT_EQ(s[2].name, std::string("embed graph"));
    ASSERT_EQ(s[3].name, std::string("consolidate"));
    ASSERT_EQ(s[4].name, std::string("extract-patterns"));
    ASSERT_EQ(s[5].name, std::string("decay"));
}

TEST("index: --skip-embed removes embed steps") {
    auto s = buildPipeline(false, true, "1d");
    ASSERT_EQ((int)s.size(), 4);
    ASSERT_EQ(s[0].name, std::string("graph update"));
    ASSERT_EQ(s[1].name, std::string("consolidate"));
}

TEST("index: --dry-run propagates to consolidate + patterns; decay always dry") {
    auto s = buildPipeline(false, false, "1d");
    auto consolidate = s[3];
    auto patterns    = s[4];
    auto decay       = s[5];
    ASSERT_TRUE(consolidate.cmd.find("--dry-run") != std::string::npos);
    ASSERT_TRUE(patterns.cmd.find("--dry-run") != std::string::npos);
    ASSERT_TRUE(decay.cmd.find("--dry-run") != std::string::npos);
}

TEST("index: --apply lifts dry-run from consolidate + patterns; decay still dry") {
    auto s = buildPipeline(true, false, "1d");
    auto consolidate = s[3];
    auto patterns    = s[4];
    auto decay       = s[5];
    ASSERT_TRUE(consolidate.cmd.find("--dry-run") == std::string::npos);
    ASSERT_TRUE(patterns.cmd.find("--dry-run") == std::string::npos);
    ASSERT_TRUE(decay.cmd.find("--dry-run") != std::string::npos);
}

TEST("index: since window passed to graph update") {
    auto s = buildPipeline(false, false, "7d");
    ASSERT_TRUE(s[0].cmd.find("--since 7d") != std::string::npos);
}

int main() {
    std::cout << "=== index tests ===\n";
    return icmg::test::run_all();
}
