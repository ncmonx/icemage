#include "../test_main.hpp"
#include "../../src/cli/self_info_detect.hpp"

using icmg::cli::looksLikeSelfInfo;

TEST("self-info: indonesian feeling text scores high") {
    ASSERT_TRUE(looksLikeSelfInfo("aku merasa sedih soal perasaan dan identitasku") > 0.6);
}

TEST("self-info: work text scores low") {
    ASSERT_TRUE(looksLikeSelfInfo("fixed build bug, ctest passes, commit the file") < 0.2);
}

TEST("self-info: empty text scores zero") {
    ASSERT_TRUE(looksLikeSelfInfo("") == 0.0);
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif
