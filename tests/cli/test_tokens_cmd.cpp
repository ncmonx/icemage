// test_tokens_cmd — unit tests for `icmg tokens` heuristic token counter.
#include "../test_main.hpp"
#include "../../src/core/token_counter.hpp"
#include "../../src/core/registry.hpp"
#include "../../src/cli/base_command.hpp"
#include <string>

TEST("tokens counts non-empty content") {
    size_t count = icmg::core::estimateTokens("hello world\n");
    ASSERT_TRUE(count > 0);
    ASSERT_TRUE(count < 12);
}

TEST("tokens heuristic in expected band for English") {
    size_t count = icmg::core::estimateTokens("the quick brown fox jumps over the lazy dog\n");
    ASSERT_TRUE(count >= 6);
    ASSERT_TRUE(count <= 16);
}

TEST("tokens with empty input returns 0") {
    size_t count = icmg::core::estimateTokens("");
    ASSERT_EQ(count, (size_t)0);
}

TEST("tokens registered") {
    auto cmd = icmg::core::Registry<icmg::cli::BaseCommand>::instance().create("tokens");
    ASSERT_TRUE(cmd != nullptr);
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
