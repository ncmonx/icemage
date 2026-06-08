// err126 module-load crash hint -- pure self-diagnosing helper.
#include "../test_main.hpp"
#include "../../src/core/crash_hint.hpp"
#include <string>
using namespace icmg::core;

TEST("crash_hint: err126 message -> non-empty actionable hint") {
    std::string h = moduleLoadHint("The specified module could not be found.");
    ASSERT_TRUE(!h.empty());
    ASSERT_TRUE(h.find("ggml-vulkan") != std::string::npos);
    ASSERT_TRUE(h.find("NAME NOT") != std::string::npos);  // points to procmon capture
}

TEST("crash_hint: sysCode 126 -> hint even if message differs") {
    ASSERT_TRUE(!moduleLoadHint("some other text", 126).empty());
}

TEST("crash_hint: unrelated error -> empty") {
    ASSERT_EQ(moduleLoadHint("database is locked", 5).size(), (size_t)0);
}

TEST("crash_hint: isModuleLoadError detects both signals") {
    ASSERT_TRUE(isModuleLoadError("x specified module could not be found y"));
    ASSERT_TRUE(isModuleLoadError("anything", 126));
    ASSERT_TRUE(!isModuleLoadError("disk full", 28));
}
