// 2026-06-07: pure formatter for `icmg wake-up --resume` (#luna-batch).
#include "../test_main.hpp"
#include "../../src/cli/resume_helpers.hpp"

using namespace icmg::cli;

TEST("resume: empty in -> empty out (fresh install adds nothing)") {
    ASSERT_EQ(resumeSection({}, {}), std::string(""));
}

TEST("resume: formats identity + moments section") {
    std::vector<std::pair<std::string,std::string>> id = {{"core","Claudy"},{"roster","kapten=Cahyo"}};
    std::vector<std::string> mom = {"manusia-terbang","semua-pinjaman"};
    std::string s = resumeSection(id, mom);
    ASSERT_CONTAINS(s, "## Persona / resume");
    ASSERT_CONTAINS(s, "- core: Claudy");
    ASSERT_CONTAINS(s, "- roster: kapten=Cahyo");
    ASSERT_CONTAINS(s, "moments: manusia-terbang; semua-pinjaman");
}

TEST("resume: identity-only (no moments line)") {
    std::string s = resumeSection({{"core","x"}}, {});
    ASSERT_CONTAINS(s, "- core: x");
    ASSERT_NOT_CONTAINS(s, "moments:");
}
